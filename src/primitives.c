#define HT_IMPLEMENTATION
#include "include/ht.h"

#include "master_slave_channel.h"
#include "primitives_t.h"
#include "primitives.h"
#include "interpreter.h"
#include "buffer.h"
#include "reader.h"
#include "readtable.h"
#include "lps.h"
#include "sexp.h"
#include <setjmp.h>
#include <threads.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <alloca.h>

static size_t lps_hasheq(Ht_Op op, void const* a_, void const* b_, size_t n)
{
	const lps *a = a_;
	const lps *b = b_;
	HT_ASSERT(n == sizeof(*a));
	switch (op) {
	case HT_HASH: return lps_hash(*a);
	case HT_EQ:   return lps_cmp(*a, *b) == 0;
	}
	return 0;
}

static size_t char_hasheq(Ht_Op op, void const* a_, void const* b_, size_t n)
{
	const char *a = a_;
	const char *b = b_;
	HT_ASSERT(n == sizeof(*a));
	switch (op) {
	case HT_HASH: return *a;
	case HT_EQ:   return *a == *b;
	}
	return 0;
}

Primitives *primitives_new() {
	Primitives *new = malloc(sizeof(Primitives));
	new->macros = (macro_table) { .hasheq = lps_hasheq };
	new->char_reader_macros = (reader_table) { .hasheq = char_hasheq };
	new->pred_reader_macros = NULL;
	new->num_predicates = 0;
	return new;
}

p_macro primitives_get_macro(const Primitives *prims, const lps fn) {
	if (ht_find(&prims->macros, fn))
		return *ht_find(&prims->macros, fn);
	else
		return NULL;
}

bool primitives_contains_macro(const Primitives *prims, const lps fn) {
	if (ht_find(&prims->macros, fn))
		return true;
	else
		return false;
}

void primitives_set_macro(Primitives *prims, const lps fn, p_macro macro) {
	if (ht_put(&prims->macros, fn))
		*ht_put(&prims->macros, fn) = macro;
}

void primitives_unset_macro(Primitives *prims, const lps fn) {
	ht_delete(&prims->macros, fn);
}

// the numeral reader macros must be primitive for ints to exist
// they can be reassigned at runtime
void primitives_export_readers(const Primitives *prims, Readtable *rt) {
	ht_foreach(value, &prims->char_reader_macros) {
		readtable_add_macro(rt, ht_key(&prims->char_reader_macros, value), *value);
	}
}

void primitives_set_reader(Primitives *prims, char c, reader_macro macro) {
	if (ht_put(&prims->char_reader_macros, c))
		*ht_put(&prims->char_reader_macros, c) = macro;
}

void primitives_unset_reader(Primitives *prims, char c) {
	ht_find_and_delete(&prims->char_reader_macros, c);
}

Sexp quote_reader_macro(Interpreter *I, char c, u16 row, u16 col) {
	Sexp quoted = sexp_new_source_list(row, col);
	Sexp quote = sexp_new_source_atom(lps_from_cstr("p-quote"), row, col);
	sexp_list_append(&quoted, quote);
	sexp_list_append(&quoted, read(I));
	return quoted;
}

Sexp str_reader_macro(Interpreter *I, char c, u16 row, u16 col) {
	// Sexp strlit = sexp_new_source_list(row, col);
	// Sexp string = sexp_new_source_atom(lps_from_cstr("string"), row, col);
	// sexp_list_append(&strlit, string);
	Buffer *string_builder = buffer_new();
	int cur_c;
	while ((cur_c = readc(I))) {
		if (cur_c == '"')
			break;
		if (cur_c == EOF) {
			fprintf(stderr, "str_reader: unterminated string");
			I->error_flag ^= ERR_OTHER;
			abort();
			return sexp_null();
		}
		if (cur_c != '\\') {
			buffer_append_char(string_builder, cur_c);
		} else {
			cur_c = readc(I);
			#define ESCAPE(CHAR, ESCAPED) \
				case CHAR: \
					buffer_append_char(string_builder, ESCAPED); \
					break;
			switch (cur_c) {
				ESCAPE('a', '\a')
				ESCAPE('b', '\b')
				ESCAPE('f', '\f')
				ESCAPE('n', '\n')
				ESCAPE('r', '\r')
				ESCAPE('t', '\t')
				ESCAPE('v', '\v')
				ESCAPE('\\', '\\')
				ESCAPE('\'', '\'')
				ESCAPE('\"', '"')
				ESCAPE('?', '?')

				/* Octal escape: \0 to \377 (1-3 octal digits) */
				case '0': case '1': case '2': case '3':
				case '4': case '5': case '6': case '7': {
					int val = cur_c - '0';
					int digits = 1;
					int next = readc(I);
					while (digits < 3 && next >= '0' && next <= '7') {
		    			val = (val << 3) + (next - '0');
		    			++digits;
		    			next = readc(I);
					}
					if (next != EOF)
						unreadc(I, c);
					buffer_append_char(string_builder, (char)val);
					break;
				}
			}
		}
	}
	lps str = lps_from_cstr(buffer_to_cstr_move(string_builder));
	Sexp result = sexp_new_source_atom(str, row, col);
	result.word.atom_type = A_STR;
	return result;
}

// reader macro primitives:
// error, get, set, remove

Sexp numeral_reader_macro(Interpreter *I, char c, u16 row, u16 col) {
	Buffer *string_builder = buffer_new();
	buffer_append_char(string_builder, c);
	int cur_c;
	enum state {
		S_INT,
		S_INTDOT,
		S_FLOAT
	} state = S_INT;
	while ((cur_c = readc(I))) {
		switch (state) {
		case S_INT: {
			if (isdigit(cur_c)) {
				buffer_append_char(string_builder, cur_c);
			} else if (c == '.') {
				buffer_append_char(string_builder, cur_c);
				state = S_INTDOT;
			} else {
				unreadc(I, cur_c);
				errno = 0; // bounds check
				buffer_null_terminate(string_builder);
				long long val = strtoll(string_builder->data, NULL, 10);
				if (errno != ERANGE) {
					Sexp ilit = sexp_new_source_atom(NULL, row, col);
					ilit.word.atom_type = A_SVAL;
					ilit.qword.atom.sval = val;
					return ilit;
				} else {
					fprintf(stderr, "integer literal cannot be converted to a long long\n");
					I->error_flag ^= ERR_OTHER;
					return sexp_null();
				}
			}
		} break;
		case S_INTDOT: { // dot after numeral
			if (isdigit(cur_c)) { // match for [0-9]
				state = S_FLOAT;
				buffer_append_char(string_builder, cur_c);
			} else {
				unreadc(I, cur_c);
				fprintf(stderr, "\"[0-9]*.%c \" is a rejected token\n", cur_c);
				I->error_flag ^= ERR_OTHER;
				return sexp_null();
			}
		} break;
		case S_FLOAT: {
			if (isdigit(cur_c)) {
				buffer_append_char(string_builder, cur_c);
			} else {
				errno = 0; // bounds check
				buffer_null_terminate(string_builder);
				double val = strtod(string_builder->data, NULL);
				if (errno != ERANGE) {
					Sexp flit = sexp_new_source_atom(NULL, row, col);
					flit.word.atom_type = A_FVAL;
					flit.qword.atom.fval = val;
					return flit;
				} else {
					fprintf(stderr, "float literal cannot be converted to a double\n");
					I->error_flag ^= ERR_OTHER;
					return sexp_null();
				}
			}
		} break;
		}
	}
	buffer_free(string_builder);
	return sexp_null();
}

Sexp comment_reader_macro(Interpreter *I, char c, u16 row, u16 col) {
	if (c == ';') {
		while (c != '\n' && c != EOF) {
			c = readc(I);
		}
	}
	return read(I);
}

void primitive_reader_macros(Primitives *prims) {
	primitives_set_reader(prims, ';', comment_reader_macro);
	primitives_set_reader(prims, '\'', quote_reader_macro);
	primitives_set_reader(prims, '\"', str_reader_macro);
	primitives_set_reader(prims, '-', numeral_reader_macro);
	primitives_set_reader(prims, '0', numeral_reader_macro);
	primitives_set_reader(prims, '1', numeral_reader_macro);
	primitives_set_reader(prims, '2', numeral_reader_macro);
	primitives_set_reader(prims, '3', numeral_reader_macro);
	primitives_set_reader(prims, '4', numeral_reader_macro);
	primitives_set_reader(prims, '5', numeral_reader_macro);
	primitives_set_reader(prims, '6', numeral_reader_macro);
	primitives_set_reader(prims, '7', numeral_reader_macro);
	primitives_set_reader(prims, '8', numeral_reader_macro);
	primitives_set_reader(prims, '9', numeral_reader_macro);
}

extern thread_local jmp_buf unwind_point;

// evaluate the given argument into the symbol
#define EVAL(symbol, argi) \
	Sexp symbol; \
	if (!sexp_is_null(at->state.call_results[argi+1])) { \
		symbol = at->state.call_results[argi+1]; \
	} else { \
		CallInst child = (CallInst) { .node = at, .call_idx = argi+1 }; \
		symbol = eval(argv[argi], I, child); \
	} \

Sexp p_read(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 0);
	Sexp s = read(I);
	if (sexp_is_null(s)) {
		// TODO: message passing
		if (!I->error_flag) {
			exit(0);
		} else {
			exit(1);
		}
	}
	return s;
}

// this exists to move the lifetime of a sexp to that of its parent
// otherwise it is freed when the macro ends, but this uses malloc
// S -> S
Sexp p_quote(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	return sexp_dup(argv[0]);
}

// the primitive form of eval
// S -> S
Sexp p_exec(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	if (!argv[0].is_list) {
		switch (sexp_atom_type(argv[0])) {
		case A_SYM: {
			if (!calltree_get_symbol_val_ref(at, sexp_read_str(argv[0]))) {
				fprintf(stderr, "p-exec: could not find symbol "LPS_Fmt"\n", LPS_Arg(sexp_read_str(argv[0])));
				abort();
			}
			return *calltree_get_symbol_val_ref(at, sexp_read_str(argv[0]));
		} break;
		default: {
			return argv[0];
		} break;
		}
	}
	Sexp *children = sexp_children(argv[0]);
	size_t num_children = sexp_num_children(argv[0]);
	// (eval ()) panics
	assert(num_children > 0);
	lps macro_str = sexp_read_str(children[0]);
	Sexp result;
	size_t child_argc = num_children-1;
	Sexp *child_argv = child_argc > 0 ? &children[1] : NULL;
	if (primitives_contains_macro(I->prims, macro_str)) {
		return primitives_get_macro
		         (I->prims, macro_str)
		         (child_argc, child_argv, I, at);
	} else {
		// (p-st-push name
		//   (()
		//    ((argc_bind argv_bind)
		//     (S0 (\ p-st-peek argc_bind))
		//     (S1 (\ p-st-peek argv_bind)))))
		// the body is interpreted as a progn
		// unwrapping symbol
		// (() name) defines a macro in the symboltable
		Sexp *st_entry = calltree_get_symbol_val_ref(at, macro_str);
		if (!st_entry) {
			fprintf(stderr, "p-exec: could not find symbol "LPS_Fmt"\n", LPS_Arg(macro_str));
			abort();
		}
		Sexp *st_children = sexp_children(*st_entry);
		assert(sexp_num_children(*st_entry) == 2);
		if (!sexp_is_nil(st_children[0])) {
			fprintf(stderr, "p-exec: symbol "LPS_Fmt" is not a macro\n", LPS_Arg(macro_str));
			abort();
		}
		Sexp macro_to_execute = sexp_children(*st_entry)[1];
		if (!macro_to_execute.is_list) {
			// symbol is an alias of a macro, call if intrinsic, panic otherwise
			macro_str = sexp_read_str(macro_to_execute);
			if (primitives_contains_macro(I->prims, macro_str)) {
				return primitives_get_macro
					     (I->prims, macro_str)
					     (child_argc, child_argv, I, at);
			} else {
				abort();
			}
		}
		assert(sexp_num_children(macro_to_execute) > 0);
		Sexp arg_binds = sexp_children(macro_to_execute)[0];
		assert(arg_binds.is_list);
		assert(sexp_num_children(arg_binds) == 2);
		CallTree *child_tree = calltree_child_at(at, &macro_to_execute, at->origin_idx);
		// CallInst interpreted_call = callinst_virtual_child_of(at, &macro_to_execute);
		// skip over the arg bindings
		CallInst interpreted_call = (CallInst) { .node = child_tree, .call_idx = 1 };
		Sexp argc_node = sexp_new_atom_uint(child_argc, at);
		lps argc_bind_to = sexp_read_str(sexp_children(arg_binds)[0]);
		Sexp argv_node = sexp_new_atom_ptr(child_argv, at);
		lps argv_bind_to = sexp_read_str(sexp_children(arg_binds)[1]);
		calltree_set_symbol_val(interpreted_call.node, argc_bind_to, argc_node);
		calltree_set_symbol_val(interpreted_call.node, argv_bind_to, argv_node);
		interpreter_push_callinst(I, interpreted_call, 0);
		// the dispatcher will step through, insert the result and call this again
		longjmp(unwind_point, 1);
	}
}

Sexp p_is_primitive(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	EVAL(a, 0)
	bool result_v = primitives_contains_macro(I->prims, sexp_read_str(a));
	return sexp_new_atom_int(result_v, at);
}

Sexp p_symboltable_push(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 2);
	EVAL(val, 1)
	int err = calltree_set_symbol_val(
                                      at->parent,
                                      sexp_read_str(argv[0]),
                                      val
	                                 );
	assert(!err);
	return sexp_new_list(at);
}
// str -> S
Sexp p_symboltable_pop(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	return calltree_remove_symbol_val(at->parent, sexp_read_str(argv[0]));
}
// str -> S*
Sexp p_symboltable_peek(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	return sexp_new_atom_ptr(calltree_get_symbol_val_ref(at, sexp_read_str(argv[0])), at);
}

Sexp p_path_at(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 0);
	CallInst *heaped = calltree_alloc(at, sizeof(CallInst));
	memcpy(heaped, &at, sizeof(CallInst));
	return sexp_new_atom_ptr(heaped, at);
}

// str -> ()
Sexp p_print(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	EVAL(post, 0)
	fprintf(stdout, ""LPS_Fmt"", LPS_Arg(sexp_read_str(post)));
	return sexp_new_list(at);
}

// S -> str
Sexp p_format(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	EVAL(a, 0)
	return sexp_new_atom_str(sexp_format(a), at);
}

// S -> ()
Sexp p_sendmsg(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	Sexp *msg = malloc(sizeof(Sexp));
	memcpy(msg, argv, sizeof(Sexp));
	slave_send_msg(I->parent_channel, msg);
	return sexp_new_list(at);
}
// () -> S
Sexp p_awaitmsg(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 0);
	Sexp msg;
	Sexp *in = slave_await_msg(I->parent_channel);
	memcpy(&msg, in, sizeof(Sexp));
	free(in);
	return msg;
}

// Sexp API ===============================================

// returns the Sexp node type
// S -> Bu
Sexp p_typeof(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	if (argv[0].is_list)
		return sexp_new_atom_uint(LIST_T, at);
	return sexp_new_atom_uint(sexp_atom_type(argv[0]), at);
}

// Sexp memory primitives
// S -> S*
Sexp p_adrof(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	return sexp_new_atom_ptr(&argv[0], at);
}
// S* -> S
// TODO: possible remove this, replace with alloca+memcpy in ss
Sexp p_deref(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	return * (Sexp*) sexp_read_ptr(argv[0]);
}
// () -> size_t
Sexp p_sexp_size(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 0);
	return sexp_new_atom_uint(sizeof(Sexp), at);;
}

// string API ==============================================
// (the other methods are reducible with memcpy)
// str -> size_t
Sexp p_strlen(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	size_t result_v = lps_len(sexp_read_str(argv[0]));
	return sexp_new_atom_uint(result_v, at);
}
// size_t -> str
Sexp p_uninit_str_from_len(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	lps result_v = lps_with_reserved_len(sexp_read_u64(argv[0]));
	Sexp result = sexp_new_atom_str(result_v, at);
	return result;
}
// str -> ()
Sexp p_str_free(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	lps_free(sexp_read_str(argv[0]));
	return sexp_new_list(at);
}


/*
   this is no longer a primitive, this comment remains as reference for the ss impl
the type system uses a monad to represent types as (T, V)
where T is a type and V is a value
usage (assuming int literals are s32 by default)
(let inc
  (fn (s32 k) s32
	(+ k 1)))
; by the way, 'fn' expands to:
; ((s32 -> s32)
;  ((s32 k) s32
;   (+ k 1)))
; processing example (abridged, not step by step)
; 0:
(let n 3)
(eval (quote (inc n)))
; 1:
(s32                    ; eval is a macro, s32 is not a fn
 (+ (& (s32 3))
	(s32 1)))
; 2:
((((s32 s32) -> s32)
  (p-s+32))             ; in reality, + is evaluated from within C for s32
 (& (s32 3))
 (s32 1))
; 3:  [here's the monad]
(s32 (p-s+32 3 1))      ; 3 is not freed by eval as it is not owned
; 4:
(s32 4)
*/
// eval doesn't free borrows, including the first arg which is always one
// & and quote's arguments are read without freeing them
/*
Sexp eval(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	Sexp result = NULL;
	if (argc != 1) {
		fprintf(stderr, "eval takes only one argument\n");
		return NULL;
	}
	// evaluating non-nil atom (symbol)
	if (!argv[0].is_list) {
		result = symboltable_peek(I->st, argv[0].val.atom.val.as_str);
		sexp_free(argv[0]);
		return result;
	}
	// evaluating nil (no-op)
	// TODO: remove nil?
	if (argv[0].val.list.num_children == 0) {
		return argv[0];
	}
	List to_eval = argv[0].val.list;
	Sexp fn_atom = to_eval.children[0];
	lps fn_name = fn_atom->val.atom.val.as_str;
	if (fn_atom->is_list) {
		fprintf(stderr, "leftmost list element cannot be a list\n");
		return NULL;
	}
	// get return type
	Sexp st_entry = symboltable_peek(I->st, fn_name);
	assert(st_entry);
	assert(st_entry->is_list);
	Sexp return_type = st_entry->val.list.children[0];
	bool is_macro = false;
	lps macro_str = lps_from_cstr("macro");
	if (return_type->is_list || lps_cmp(macro_str, return_type->val.atom.val.as_str) != 0) {
		is_macro = true;
	}
	lps_free(macro_str);
	// eval args if not macro
	if (!is_macro) {
		for (size_t i = 1; i < to_eval.num_children; ++i) {
			Sexp cur = to_eval.children[i];
			cur = eval(1, &cur);
		}
	}
	if (primitives_contains(prims, fn_name)) {
		Sexp val = primitives_call
		                (prims, fn_atom->val.atom.val.as_str)
			            (to_eval.num_children-1, &to_eval.children[1]);
		if (!is_macro) {
			// result is (type, val) pair for a fn
			result = sexp_new_list();
			sexp_list_append(&result, return_type);
			sexp_list_append(&result, val);
		} else
			result = val;
	} else {
		// TODO: interpret non-primitive fns
		abort();
	}
	// free the moved in args for a fn
	if (return_type) {
		for (size_t i = 0; i < to_eval.num_children; ++i) {
			sexp_free(argv[i]);
		}
	}
	return result;
}
*/

// C-derived primitives

#define DEFINE_UNARY_FN(NAME, FN, IN_TYPE, READAS, OUT_ATOM) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) { \
	assert(argc == 1); \
	Sexp a = argv[0]; \
	assert(sizeof(IN_TYPE) <= sizeof(void*)); \
	IN_TYPE a_v = sexp_read_##READAS(a); \
	Sexp result = sexp_new_atom_##OUT_ATOM(FN(a_v), at); \
	return result; \
}

#define DEFINE_TERNARY_FN(NAME, FN, IN1_TYPE, IN1_READAS, IN2_TYPE, IN2_READAS, IN3_TYPE, IN3_READAS, OUT_TYPE, OUT_ATOM) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) { \
	assert(argc == 3); \
	Sexp a = argv[0]; \
	Sexp b = argv[1]; \
	Sexp c = argv[2]; \
	assert(sizeof(IN1_TYPE) <= sizeof(void*)); \
	assert(sizeof(IN2_TYPE) <= sizeof(void*)); \
	assert(sizeof(IN3_TYPE) <= sizeof(void*)); \
	assert(sizeof(OUT_TYPE) <= sizeof(void*)); \
	IN1_TYPE a_v = sexp_read_##IN1_READAS(a); \
	IN2_TYPE b_v = sexp_read_##IN2_READAS(b); \
	IN3_TYPE c_v = sexp_read_##IN3_READAS(c); \
	Sexp result = sexp_new_atom_##OUT_ATOM(FN(a_v, b_v, c_v), at); \
	return result; \
}

// ptr -> ()
Sexp p_free(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	free(sexp_read_ptr(argv[0]));
	Sexp result = sexp_new_list(at);
	return result;
}

// (ptr, size_t) -> ptr
Sexp p_realloc(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 2);
	void* a_v = sexp_read_ptr(argv[0]);
	size_t b_v = sexp_read_u64(argv[1]);
	void* result_v = realloc(a_v, b_v);
	return sexp_new_atom_ptr(result_v, at);
}

Sexp p_alloca(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	void *result_v = calltree_alloc(at, sexp_read_u64(argv[0]));
	return sexp_new_atom_ptr(result_v, at);
}
DEFINE_UNARY_FN(p_malloc, malloc, size_t, u64, ptr)
DEFINE_TERNARY_FN(p_memcmp, memcmp, void*, ptr, void*, ptr, size_t, u64, int, int)
DEFINE_TERNARY_FN(p_memcpy, memcpy, void*, ptr, void*, ptr, size_t, u64, void*, ptr)
DEFINE_TERNARY_FN(p_memset, memset, void*, ptr, int, s64, size_t, u64, void*, ptr)

// (Bint S S) -> S
Sexp p_if(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 3);
	EVAL(cond, 0)
	int cond_v = sexp_read_s64(cond);
	Sexp branch;
	if (cond_v)
		branch = argv[1];
	else
		branch = argv[2];
	return sexp_dup(branch);
}

// returns the final expr's result
// does not use a dispatcher, performs the loop in C
// (Bint S) -> S
Sexp p_while(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc > 0);
	Sexp result = sexp_new_list(at);
	while (1) {
		EVAL(cond, 0)
		if (!sexp_read_s64(cond))
			break;
		for (size_t i = 1; i < argc; ++i) {
			if (i+1 == argc) {
				EVAL(ignored, i)
			} else {
				EVAL(result, i)
			}
		}
		// after the iteration, wipe eval state for the next
		memset(
		        at->state.call_results,
		        0,
		        sexp_num_children(*at->state.sexp_called) * sizeof(Sexp)
		      );
	}
	return sexp_dup(result);
}

#define DEFINE_BINARY_OP(NAME, OP, IN_TYPE, READAS, OUT_TYPE, OUT_ATOM) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) { \
	assert(argc == 2); \
	EVAL(a, 0) \
	EVAL(b, 1) \
	assert(sizeof(IN_TYPE) <= sizeof(void*)); \
	assert(sizeof(OUT_TYPE) <= sizeof(void*)); \
	IN_TYPE a_v = sexp_read_##READAS(a); \
	IN_TYPE b_v = sexp_read_##READAS(b); \
	OUT_TYPE result_v = a_v OP b_v; \
	Sexp result = sexp_new_atom_##OUT_ATOM(result_v, at); \
	return result; \
}

#define DEFINE_UNARY_OP(NAME, OP, IN_TYPE, READAS, OUT_TYPE, OUT_ATOM) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) { \
	assert(argc == 1); \
	EVAL(a, 0) \
	assert(sizeof(IN_TYPE) <= sizeof(void*)); \
	assert(sizeof(OUT_TYPE) <= sizeof(void*)); \
	IN_TYPE a_v = sexp_read_##READAS(a); \
	OUT_TYPE result_v = OP a_v; \
	Sexp result = sexp_new_atom_##OUT_ATOM(result_v, at); \
	return result; \
}

DEFINE_BINARY_OP(p_and, &&, int, s64, int, int)
DEFINE_BINARY_OP(p_or, ||, int, s64, int, int)
DEFINE_UNARY_OP(p_not, !, int, s64, int, int)
DEFINE_BINARY_OP(p_biteq, ==, uintptr_t, u64, int, int) // technically UB union usage
DEFINE_BINARY_OP(p_bitneq, !=, uintptr_t, u64, int, int)
DEFINE_BINARY_OP(p_bitand, &, uintptr_t, u64, uintptr_t, int)
DEFINE_BINARY_OP(p_bitor, |, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_bitxor, ^, uintptr_t, u64, uintptr_t, uint)
DEFINE_UNARY_OP(p_bitnot, ~, uintptr_t, u64, uintptr_t, uint)

DEFINE_BINARY_OP(p_uadd, +, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_usub, -, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_umul, *, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_udiv, /, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_umod, &, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_ushl, <<, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_ushr, >>, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_ult, <, uintptr_t, u64, int, int)
DEFINE_BINARY_OP(p_ulte, <=, uintptr_t, u64, int, int)
DEFINE_BINARY_OP(p_ugt, >, uintptr_t, u64, int, int)
DEFINE_BINARY_OP(p_ugte, >=, uintptr_t, u64, int, int)
DEFINE_BINARY_OP(p_sadd, +, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_ssub, -, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_smul, *, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_sdiv, /, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_smod, &, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_sshl, <<, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_sshr, >>, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_slt, <, intptr_t, s64, int, int)
DEFINE_BINARY_OP(p_slte, <=, intptr_t, s64, int, int)
DEFINE_BINARY_OP(p_sgt, >, intptr_t, s64, int, int)
DEFINE_BINARY_OP(p_sgte, >=, intptr_t, s64, int, int)
DEFINE_BINARY_OP(p_fadd, +, double, f64, double, float)
DEFINE_BINARY_OP(p_fsub, -, double, f64, double, float)
DEFINE_BINARY_OP(p_fmul, *, double, f64, double, float)
DEFINE_BINARY_OP(p_fdiv, /, double, f64, double, float)
DEFINE_BINARY_OP(p_flt, <, double, f64, int, int)
DEFINE_BINARY_OP(p_flte, <=, double, f64, int, int)
DEFINE_BINARY_OP(p_fgt, >, double, f64, int, int)
DEFINE_BINARY_OP(p_fgte, >=, double, f64, int, int)

#define PRIM_ENTRY(primname) \
primitives_set_macro(prims, lps_from_cstr("p-"#primname), p_##primname);

#include "include/foreach.h"

Primitives *primitives_default() {
	Primitives *prims = primitives_new();
	FOREACH(PRIM_ENTRY,
exec, is_primitive,
read, quote,
symboltable_push, symboltable_pop, symboltable_peek,
print, format,
sendmsg, awaitmsg,
adrof, deref, sexp_size, typeof,
path_at,
strlen, uninit_str_from_len, str_free,
malloc, free, realloc, alloca,
memcmp, memcpy, memset,
if, while,
and, or, not,
biteq, bitneq, bitand, bitor, bitxor, bitnot,
uadd, usub, umul, udiv,
ult, ulte, ugt, ugte,
umod, ushl, ushr,
sadd, ssub, smul, sdiv,
slt, slte, sgt, sgte,
smod, sshl, sshr,
fadd, fsub, fmul, fdiv,
flt, flte, fgt, fgte
	)
	prims->default_eval_binding = lps_from_cstr("p-exec");
	primitive_reader_macros(prims);
	return prims;
}

