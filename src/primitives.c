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
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <alloca.h>

size_t lps_hasheq(Ht_Op op, void const* a_, void const* b_, size_t n)
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

size_t char_hasheq(Ht_Op op, void const* a_, void const* b_, size_t n)
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
	Sexp quote = sexp_new_source_atom(lps_from_cstr("quote"), row, col);
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
	// sexp_list_append(&strlit, sexp_new_source_atom(str, row, col));
	// return strlit;
	return sexp_new_source_atom(str, row, col);
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
					ilit.val.atom.type = A_SVAL;
					ilit.val.atom.val.sval = val;
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
					flit.val.atom.type = A_FVAL;
					flit.val.atom.val.fval = val;
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

Sexp p_read(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
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
// otherwise it is freed when the macro ends
// S -> S
Sexp p_quote(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	return sexp_dup(argv[0]);
}

// the primitive form of eval
Sexp p_exec(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	if (!argv[0].is_list)
		return argv[0];
	List to_exec = argv[0].val.list;
	assert(to_exec.num_children > 0);
	assert(! to_exec.children[0].is_list);
	Atom macro_sym = to_exec.children[0].val.atom;
	assert(macro_sym.type == A_STR);
	lps macro_str = macro_sym.val.as_str;
	Sexp result;
	size_t child_argc = to_exec.num_children-1;
	Sexp *child_argv = child_argc > 0 ? &to_exec.children[1] : NULL;
	if (primitives_contains_macro(I->prims, macro_str)) {
		result = primitives_get_macro
		            (I->prims, macro_str)
		            (child_argc, child_argv, I, at);
	} else {
		// (p-st-push name
		//   (()
		//    ((argc_bind argv_bind)
		//     (S0 (\ p-st-peek argc_bind))
		//     (S1 (\ p-st-peek argv_bind)))))
		// the body is interpreted as a progn
unwrapping_symbols:
		// (() name) is a macro alias in the symboltable
		Sexp *st_entry = calltree_get_symbol_val_ref(at.node, macro_str);
		assert(st_entry);
		assert(st_entry->is_list);
		assert(st_entry->val.list.num_children == 2);
		assert(sexp_is_nil(st_entry->val.list.children[0]));
		if (! st_entry->val.list.children[1].is_list) {
			// symbol was an alias of a macro
			assert(st_entry->val.list.children[1].val.atom.type == A_STR);
			macro_str = st_entry->val.list.children[1].val.atom.val.as_str;
			if (primitives_contains_macro(I->prims, macro_str)) {
				result = primitives_get_macro
		            		(I->prims, macro_str)
		            		(child_argc, child_argv, I, at);
				goto done;
			} else {
				goto unwrapping_symbols;
			}
		}
		List child_macro_list = st_entry->val.list.children[1].val.list;
		assert(child_macro_list.num_children > 0);
		assert(child_macro_list.children[0].is_list);
		List arg_binds = child_macro_list.children[0].val.list;
		assert(arg_binds.num_children == 2);
		assert(!arg_binds.children[0].is_list);
		assert(arg_binds.children[0].val.atom.type == A_STR);
		assert(!arg_binds.children[1].is_list);
		assert(arg_binds.children[1].val.atom.type == A_STR);
		lps argc_bind = arg_binds.children[0].val.atom.val.as_str;
		Sexp argc_node = sexp_new_atom_uint(0, at);
		argc_node.val.atom.val.uval = child_argc;
		lps argv_bind = arg_binds.children[1].val.atom.val.as_str;
		Sexp argv_node = sexp_new_atom_ptr(NULL, at);
		argc_node.val.atom.val.as_ptr = child_argv;
		// these Sexp* can be accessed via alloca and memcpy
		calltree_set_symbol_val(at.node, argc_bind, argc_node);
		calltree_set_symbol_val(at.node, argv_bind, argv_node);
		for (size_t i = 0; i+1 < child_macro_list.num_children; ++i) {
			Sexp cur_sexp = child_macro_list.children[i];
			assert(cur_sexp.is_list);
			assert(cur_sexp.val.list.num_children > 0);
			assert(! cur_sexp.val.list.children[0].is_list);
			List cur_list = cur_sexp.val.list;
			size_t child_argc = cur_list.num_children-1;
			Sexp *child_argv = child_argc > 0 ? &cur_list.children[1] : NULL;
			if (i+2 < st_entry->val.list.num_children)
				p_exec(argc, argv, I, at);
			else
				result = p_exec(argc, argv, I, at);
		}
		calltree_remove_symbol_val(at.node, argc_bind);
		calltree_remove_symbol_val(at.node, argv_bind);
	}
done:
	return result;
}

Sexp p_is_primitive(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	assert(! argv[0].is_list);
	assert(argv[0].val.atom.type == A_STR);
	bool result_v = primitives_contains_macro(I->prims, argv[0].val.atom.val.as_str);
	Sexp result = sexp_new_atom_uint(0, at);
	result.val.atom.val.uval = result_v;
	return result;
}

Sexp p_symboltable_push(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 2);
	assert(! argv[0].is_list);
	assert(argv[0].val.atom.type == A_STR);
	int err = calltree_set_symbol_val(
                                      at.node,
                                      argv[0].val.atom.val.as_str,
                                      argv[1]
	                                 );
	assert(!err);
	return sexp_new_list(at);
}
// str -> S
Sexp p_symboltable_pop(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	assert(!argv[0].is_list);
	assert(argv[0].val.atom.type == A_STR);
	return calltree_remove_symbol_val(at.node, argv[0].val.atom.val.as_str);
}
// str -> S*
Sexp p_symboltable_peek(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	assert(!argv[0].is_list);
	assert(argv[0].val.atom.type == A_STR);
	Sexp ptr = sexp_new_atom_ptr(NULL, at);
	ptr.val.atom.val.as_ptr = calltree_get_symbol_val_ref(at.node, argv[0].val.atom.val.as_str);
	return ptr;
}

Sexp p_path_at(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 0);
	CallInst *heaped = malloc(sizeof(CallInst));
	memcpy(heaped, &at, sizeof(CallInst));
	return sexp_new_atom_ptr(heaped, at);
}

// str -> ()
Sexp p_print(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	Sexp post = eval(argv[0], I, at);
	fprintf(stdout, ""LPS_Fmt"", LPS_Arg(post.val.atom.val.as_str));
	return sexp_new_list(at);
}

// S -> str
Sexp p_format(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	Sexp a = eval(argv[0], I, at);
	return sexp_new_atom_str(sexp_format(a), at);
}

// S -> ()
Sexp p_sendmsg(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	Sexp *msg = malloc(sizeof(Sexp));
	memcpy(msg, argv, sizeof(Sexp));
	slave_send_msg(I->parent_channel, msg);
	return sexp_new_list(at);
}
// () -> S
Sexp p_awaitmsg(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 0);
	Sexp msg;
	Sexp *in = slave_await_msg(I->parent_channel);
	memcpy(&msg, in, sizeof(Sexp));
	free(in);
	return msg;
}

// Sexp memory primitives
// S -> S*
Sexp p_adrof(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	return sexp_new_atom_ptr(&argv[0], at);
}
// S* -> S
Sexp p_deref(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	return * (Sexp*) argv[0].val.atom.val.as_ptr;
}
// () -> size_t
Sexp p_sexp_size(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 0);
	return sexp_new_atom_uint(sizeof(Sexp), at);;
}

// string API (the other methods are reducible with memcpy)
// str -> size_t
Sexp p_strlen(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	assert(! argv[0].is_list);
	assert(argv[0].val.atom.type == A_STR);
	size_t result_v = lps_len(argv[0].val.atom.val.as_str);
	return sexp_new_atom_uint(result_v, at);
}
// size_t -> str
Sexp p_uninit_str_from_len(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	assert(! argv[0].is_list);
	assert(argv[0].val.atom.type == A_UVAL);
	size_t a_v;
	a_v = argv[0].val.atom.val.uval;
	lps result_v = lps_with_reserved_len(a_v);
	Sexp result = sexp_new_atom_str(result_v, at);
	return result;
}
// str -> ()
Sexp p_str_free(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	assert(! argv[0].is_list);
	assert(argv[0].val.atom.type == A_STR);
	lps_free(argv[0].val.atom.val.as_str);
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
Sexp eval(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
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

#define DEFINE_UNARY_FN(NAME, FN, IN_TYPE, IN_FIELD, OUT_TYPE, OUT_FIELD) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallInst at) { \
	assert(argc == 1); \
	Sexp a = argv[0]; \
	assert(sizeof(IN_TYPE) <= sizeof(void*)); \
	assert(sizeof(OUT_TYPE) <= sizeof(void*)); \
	IN_TYPE a_v = a.val.atom.val.IN_FIELD; \
	OUT_TYPE result_v = FN(a_v); \
	Sexp result = sexp_new_atom_uint(0, at); \
	result.val.atom.val.OUT_FIELD = result_v; \
	return result; \
}

#define DEFINE_TERNARY_FN(NAME, FN, IN1_TYPE, IN1_FIELD, IN2_TYPE, IN2_FIELD, IN3_TYPE, IN3_FIELD, OUT_TYPE, OUT_FIELD) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallInst at) { \
	assert(argc == 3); \
	Sexp a = argv[0]; \
	Sexp b = argv[1]; \
	Sexp c = argv[2]; \
	assert(sizeof(IN1_TYPE) <= sizeof(void*)); \
	assert(sizeof(IN2_TYPE) <= sizeof(void*)); \
	assert(sizeof(IN3_TYPE) <= sizeof(void*)); \
	assert(sizeof(OUT_TYPE) <= sizeof(void*)); \
	IN1_TYPE a_v = a.val.atom.val.IN1_FIELD; \
	IN2_TYPE b_v = b.val.atom.val.IN2_FIELD; \
	IN3_TYPE c_v = c.val.atom.val.IN3_FIELD; \
	OUT_TYPE result_v = FN(a_v, b_v, c_v); \
	Sexp result = sexp_new_atom_uint(0, at); \
	result.val.atom.val.OUT_FIELD = result_v; \
	return result; \
}

// ptr -> ()
Sexp p_free(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	Sexp a = argv[0];
	free(a.val.atom.val.as_ptr);
	Sexp result = sexp_new_list(at);
	return result;
}

// (ptr, size_t) -> ptr
Sexp p_realloc(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 2);
	Sexp a = argv[0];
	Sexp b = argv[1];
	void* a_v = a.val.atom.val.as_ptr;
	size_t b_v = b.val.atom.val.uval;
	void* result_v = realloc(a_v, b_v);
	Sexp result = sexp_new_atom_ptr(result_v, at);
	return result;
}

Sexp p_alloca(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 1);
	Sexp a = argv[0];
	size_t a_v = a.val.atom.val.uval;
	void *result_v = calltree_alloc(at.node, a_v);
	Sexp result = sexp_new_atom_uint(0, at);
	result.val.atom.val.as_ptr = result_v;
	return result;
}
DEFINE_UNARY_FN(p_malloc, malloc, size_t, uval, void*, as_ptr)
DEFINE_TERNARY_FN(p_memcmp, memcmp, void*, as_ptr, void*, as_ptr, size_t, uval, int, sval);
DEFINE_TERNARY_FN(p_memcpy, memcpy, void*, as_ptr, void*, as_ptr, size_t, uval, void*, as_ptr);
DEFINE_TERNARY_FN(p_memset, memset, void*, as_ptr, int, sval, size_t, uval, void*, as_ptr);

// (Bint S S) -> S
Sexp p_if(size_t argc, Sexp *argv, Interpreter *I, CallInst at) {
	assert(argc == 3);
	Sexp cond = argv[0];
	int cond_v = cond.val.atom.val.sval;
	Sexp branch;
	if (cond_v)
		branch = argv[1];
	else
		branch = argv[2];
	return sexp_dup(branch);
}

#define DEFINE_BINARY_OP(NAME, OP, IN_TYPE, IN_FIELD, OUT_TYPE, OUT_FIELD, OUT_ATOM) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallInst at) { \
	assert(argc == 2); \
	Sexp a = eval(argv[0], I, at); \
	Sexp b = eval(argv[1], I, at); \
	assert(sizeof(IN_TYPE) <= sizeof(void*)); \
	assert(sizeof(OUT_TYPE) <= sizeof(void*)); \
	IN_TYPE a_v = a.val.atom.val.IN_FIELD; \
	IN_TYPE b_v = b.val.atom.val.IN_FIELD; \
	OUT_TYPE result_v = a_v OP b_v; \
	Sexp result = sexp_new_atom_##OUT_ATOM(0, at); \
	result.val.atom.val.OUT_FIELD = result_v; \
	return result; \
}

#define DEFINE_UNARY_OP(NAME, OP, IN_TYPE, IN_FIELD, OUT_TYPE, OUT_FIELD) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallInst at) { \
	assert(argc == 1); \
	Sexp a = argv[0]; \
	assert(sizeof(IN_TYPE) <= sizeof(void*)); \
	assert(sizeof(OUT_TYPE) <= sizeof(void*)); \
	IN_TYPE a_v = a.val.atom.val.IN_FIELD; \
	OUT_TYPE result_v = OP a_v; \
	Sexp result = sexp_new_atom_uint(0, at); \
	result.val.atom.val.OUT_FIELD = result_v; \
	return result; \
}

DEFINE_BINARY_OP(p_and, &&, int, sval, int, sval, int)
DEFINE_BINARY_OP(p_or, ||, int, sval, int, sval, int)
DEFINE_UNARY_OP(p_not, !, int, sval, int, sval)
DEFINE_BINARY_OP(p_biteq, ==, uintptr_t, uval, int, sval, int) // technically UB union usage
DEFINE_BINARY_OP(p_bitneq, !=, uintptr_t, uval, int, sval, int)
DEFINE_BINARY_OP(p_bitand, &, uintptr_t, uval, uintptr_t, uval, int)
DEFINE_BINARY_OP(p_bitor, |, uintptr_t, uval, uintptr_t, uval, uint)
DEFINE_BINARY_OP(p_bitxor, ^, uintptr_t, uval, uintptr_t, uval, uint)
DEFINE_UNARY_OP(p_bitnot, ~, uintptr_t, uval, uintptr_t, uval)

DEFINE_BINARY_OP(p_uadd, +, uintptr_t, uval, uintptr_t, uval, uint)
DEFINE_BINARY_OP(p_usub, -, uintptr_t, uval, uintptr_t, uval, uint)
DEFINE_BINARY_OP(p_umul, *, uintptr_t, uval, uintptr_t, uval, uint)
DEFINE_BINARY_OP(p_udiv, /, uintptr_t, uval, uintptr_t, uval, uint)
DEFINE_BINARY_OP(p_umod, &, uintptr_t, uval, uintptr_t, uval, uint)
DEFINE_BINARY_OP(p_ushl, <<, uintptr_t, uval, uintptr_t, uval, uint)
DEFINE_BINARY_OP(p_ushr, >>, uintptr_t, uval, uintptr_t, uval, uint)
DEFINE_BINARY_OP(p_ult, <, uintptr_t, uval, int, sval, int)
DEFINE_BINARY_OP(p_ulte, <=, uintptr_t, uval, int, sval, int)
DEFINE_BINARY_OP(p_ugt, >, uintptr_t, uval, int, sval, int)
DEFINE_BINARY_OP(p_ugte, >=, uintptr_t, uval, int, sval, int)
DEFINE_BINARY_OP(p_sadd, +, intptr_t, sval, intptr_t, sval, int)
DEFINE_BINARY_OP(p_ssub, -, intptr_t, sval, intptr_t, sval, int)
DEFINE_BINARY_OP(p_smul, *, intptr_t, sval, intptr_t, sval, int)
DEFINE_BINARY_OP(p_sdiv, /, intptr_t, sval, intptr_t, sval, int)
DEFINE_BINARY_OP(p_smod, &, intptr_t, sval, intptr_t, sval, int)
DEFINE_BINARY_OP(p_sshl, <<, intptr_t, sval, intptr_t, sval, int)
DEFINE_BINARY_OP(p_sshr, >>, intptr_t, sval, intptr_t, sval, int)
DEFINE_BINARY_OP(p_slt, <, intptr_t, sval, int, sval, int)
DEFINE_BINARY_OP(p_slte, <=, intptr_t, sval, int, sval, int)
DEFINE_BINARY_OP(p_sgt, >, intptr_t, sval, int, sval, int)
DEFINE_BINARY_OP(p_sgte, >=, intptr_t, sval, int, sval, int)
DEFINE_BINARY_OP(p_fadd, +, double, fval, double, fval, float)
DEFINE_BINARY_OP(p_fsub, -, double, fval, double, fval, float)
DEFINE_BINARY_OP(p_fmul, *, double, fval, double, fval, float)
DEFINE_BINARY_OP(p_fdiv, /, double, fval, double, fval, float)
DEFINE_BINARY_OP(p_flt, <, double, fval, int, sval, int)
DEFINE_BINARY_OP(p_flte, <=, double, fval, int, sval, int)
DEFINE_BINARY_OP(p_fgt, >, double, fval, int, sval, int)
DEFINE_BINARY_OP(p_fgte, >=, double, fval, int, sval, int)

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
adrof, deref, sexp_size,
path_at,
strlen, uninit_str_from_len, str_free,
malloc, free, realloc, alloca,
memcmp, memcpy, memset,
if,
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

