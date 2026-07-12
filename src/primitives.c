// the language's intrinsics
// I call them primitives because (p-name arg) is better than (i-name arg)
// as concerning these having a 'namespace'. also I is taken by "interpreter"
#include "include/nob.h"
#define HT_IMPLEMENTATION
#include "include/ht.h"

#include "primitives.h"
#include "master_slave_channel.h"
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
	sexp_list_append(&quoted, quote, &I->call_root->state.memory);
	sexp_list_append(&quoted, reads(I), &I->call_root->state.memory);
	return quoted;
}

static int xchar_to_int(int c) {
	if (isdigit(c))
		return (c - '0');
	else // [a-fA-F]
		return (10 + toupper(c) - 'A');
}

Sexp str_reader_macro(Interpreter *I, char entry_c, const u16 row, const u16 col) {
	Buffer *string_builder = buffer_new();
	int c;
	while ((c = readc(I))) {
		if (c == '"')
			break;
		if (c == EOF) {
			fprintf(stderr, "str_reader: unterminated string at %d:%d", row, col);
			I->error_flags ^= ERR_OTHER;
			return sexp_null();
		}
		if (c != '\\') {
			buffer_append_char(string_builder, c);
		} else {
			c = readc(I);
			#define ESCAPE(CHAR, ESCAPED) \
				case CHAR: \
					buffer_append_char(string_builder, ESCAPED); \
					break;
			switch (c) {
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
					int val = c - '0';
					int digits = 1;
					c = readc(I);
					while (digits < 3 && c >= '0' && c <= '7') {
		    			val = (val << 3) + (c - '0');
		    			++digits;
		    			c = readc(I);
					}
					if (c != EOF)
						unreadc(I, c);
					else {
						abort();
					}
					buffer_append_char(string_builder, (char)val);
					break;
				}
				/* hex excape: \xYY */
				case 'x': {
					int val = 0;
					val = xchar_to_int(entry_c);
					c = readc(I);
					val = (val << 4) + xchar_to_int(entry_c);
					if (entry_c != EOF)
						unreadc(I, entry_c);
					else {
						abort();
					}
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

// TODO: hex deserialisation
Sexp numeral_reader_macro(Interpreter *I, char entry_c, const u16 row, const u16 col) {
	Buffer *string_builder = buffer_new();
	buffer_append_char(string_builder, entry_c);
	int c;
	enum state {
		S_INT,
		S_INTDOT,
		S_FLOAT,
		S_HEX,
	} state = S_INT;
	while ((c = readc(I))) {
		switch (state) {
		case S_INT: {
			if (isdigit(c)) {
				buffer_append_char(string_builder, c);
			} else if (c == '.') {
				buffer_append_char(string_builder, c);
				state = S_INTDOT;
			} else if (c == 'x' && string_builder->len == 1 && string_builder->data[0] == '0') {
				buffer_clear(string_builder);
				state = S_HEX;
			} else if (c == ' ' || c == '\n' || c == ')') {
				unreadc(I, c);
				errno = 0; // bounds check
				buffer_null_terminate(string_builder);
				long long val = strtoll(string_builder->data, NULL, 10);
				if (errno != ERANGE) {
					Sexp ilit = sexp_new_source_atom(NULL, row, col);
					ilit.word.atom_type = A_SVAL;
					ilit.qword.atom.sval = val;
					return ilit;
				} else {
					fprintf(stderr, "integer literal cannot be converted to a long long at %d:%d\n", row, col);
					I->error_flags ^= ERR_OTHER;
					return sexp_null();
				}
			} else {
				fprintf(stderr, "numeral_reader: expected digit or whitespace at %d:%d\n", I->tail_row, I->tail_col);
				I->error_flags ^= ERR_OTHER;
				return sexp_null();
			}
		} break;
		case S_INTDOT: { // dot after numeral
			if (isdigit(c)) { // match for [0-9]
				state = S_FLOAT;
				buffer_append_char(string_builder, c);
			} else {
				fprintf(stderr, "numeral_reader: \"[0-9]*.%c \" is a rejected token at %d:%d\n", c, row, col);
				I->error_flags ^= ERR_OTHER;
				return sexp_null();
			}
		} break;
		case S_FLOAT: {
			if (isdigit(c)) {
				buffer_append_char(string_builder, c);
			} else if (c == ' ' || c == '\n' || c == ')') {
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
					I->error_flags ^= ERR_OTHER;
					return sexp_null();
				}
			} else {
				fprintf(stderr, "numeral_reader: expected digit or whitespace at %d:%d\n", I->tail_row, I->tail_col);
				I->error_flags ^= ERR_OTHER;
				return sexp_null();
			}
		} break;
		case S_HEX: {
			if (isxdigit(c)) {
				buffer_append_char(string_builder, c);
			} else if (c == ' ' || c == '\n' || c == ')') {
				unreadc(I, c);
				if (string_builder->len > 2*sizeof(uintptr_t)) {
					fprintf(stderr, "numeral_reader: hex literal exceeds word size at %d:%d\n", row, col);
					I->error_flags ^= ERR_OTHER;
					return sexp_null();
				}
				uintptr_t val = 0;
				for (size_t i = 0; i < string_builder->len; ++i) {
					char cur = string_builder->data[i];
					val = (val << 4) + xchar_to_int(cur);
				}
				Sexp hexlit = sexp_new_source_atom(NULL, row, col);
				hexlit.word.atom_type = A_UVAL;
				hexlit.qword.atom.uval = val;
				return hexlit;
			} else {
				fprintf(stderr, "numeral_reader: expected hex char or whitespace at %d:%d\n", I->tail_row, I->tail_col);
				I->error_flags ^= ERR_OTHER;
				return sexp_null();
			}
		} break;
		}
	}
	buffer_free(string_builder);
	return sexp_null();
}

Sexp comment_reader_macro(Interpreter *I, char c, u16 row, u16 col) {
	// TODO: multi-line comments
	if (c == ';') {
		while (c != '\n' && c != EOF) {
			c = readc(I);
		}
	}
	return reads(I);
}

void primitive_reader_macros(Primitives *prims) {
	primitives_set_reader(prims, ';', comment_reader_macro);
	// for shebangs
	primitives_set_reader(prims, '#', comment_reader_macro);
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

typedef struct {
	Sexp **items;
	uint count;
	uint capacity;
} Sexps;

// replaces symbols with values in a sexp
static void replace_bindings(Sexp *macrobody, lps argc_bind, Sexp argc_val, lps argv_bind, Sexp argv_val) {
	Sexps to_check = {0};
	nob_da_append(&to_check, macrobody);
	while (to_check.count > 0) {
		Sexp *cur = nob_da_pop(&to_check);
		if (cur->is_list) {
			for (size_t i = 0; i < sexp_num_children(*cur); ++i) {
				nob_da_append(&to_check, sexp_list_nth(*cur, i));
			}
		} else if (sexp_atom_type(*cur) == A_SYM) {
			if (lps_cmp(argc_bind, sexp_read_str(*cur)) == 0)
				*cur = argc_val;
			else if (lps_cmp(argv_bind, sexp_read_str(*cur)) == 0)
				*cur = argv_val;
		}
	}
}

extern thread_local jmp_buf unwind_point;

// the primitive form of eval
// S -> S
Sexp p_exec(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	// atom: fetch symbol, do nothing for literals
	if (!argv[0].is_list) {
		switch (sexp_atom_type(argv[0])) {
		case A_SYM: {
			if (primitives_contains_macro(I->prims, sexp_read_str(argv[0]))) {
				return argv[0];
			}
			if (!calltree_get_symbol_val_ref(at, sexp_read_str(argv[0]))) {
				fprintf(stderr, "p-exec: could not find symbol "LPS_Fmt"\n", LPS_Arg(sexp_read_str(argv[0])));
				abort();
			}
			return *calltree_get_symbol_val_ref(at, sexp_read_str(argv[0]));
		} break;
		default: {
			return argv[0];
		} abort();
		}
	}
	Sexp *children = sexp_children(argv[0]);
	size_t num_children = sexp_num_children(argv[0]);
	// (p-exec ()) returns ()
	if (num_children == 0)
		return argv[0];
	// TODO: macro literal support (eval this rather than look in symboltable)
	lps macro_str = sexp_read_str(children[0]);
	size_t child_argc = num_children-1;
	Sexp *child_argv = child_argc > 0 ? &children[1] : NULL;
	if (primitives_contains_macro(I->prims, macro_str)) {
		Sexp *to_eval = calltree_alloc(at, sizeof(Sexp));
		*to_eval = sexp_dup(argv[0]);
		CallTree *call = calltree_child_at(at, to_eval, child_argc, 0, &I->arenapool);
		Sexp result = sexp_dup(primitives_get_macro
		                        (I->prims, macro_str)
		                        (child_argc, child_argv, I, call));
		calltree_destroy(*call, &I->arenapool);
		return result;
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
		// primitive macros as symbols evaluate to themselves, if something returned one from the symboltable
		// that is symbol is an alias of a macro, be it a prim symbol or a list
		if (!st_entry->is_list && sexp_atom_type(*st_entry) == A_SYM) {
			if (primitives_contains_macro(I->prims, sexp_read_str(*st_entry))) {
				Sexp *to_eval = calltree_alloc(at, sizeof(Sexp));
				*to_eval = sexp_dup(argv[0]);
				CallTree *call = calltree_child_at(at, to_eval, child_argc, 0, &I->arenapool);
				Sexp result = sexp_dup(primitives_get_macro
				                        (I->prims, sexp_read_str(*st_entry))
		                                (child_argc, child_argv, I, call));
				calltree_destroy(*call, &I->arenapool);
				return result;
			} else {
				abort();
			}
		}
		Sexp *st_children = sexp_children(*st_entry);
		assert(sexp_num_children(*st_entry) == 2);
		assert(sexp_is_nil(st_children[0]));
		Sexp st_val = st_children[1];
		assert(sexp_num_children(st_val) >= 2);
		assert(lps_cmp(sexp_read_str(sexp_children(st_val)[0]), lps_from_cstr("p-macrobody")) == 0);
		Sexp arg_binds = sexp_children(st_val)[1];
		assert(arg_binds.is_list);
		assert(sexp_num_children(arg_binds) == 2);
		Sexp *alloced_macro = calltree_alloc(at, sizeof(Sexp));
		*alloced_macro = sexp_dup(st_val);
		CallTree *child_call = calltree_child_at(at, alloced_macro, child_argc, 0, &I->arenapool);
		Sexp argc_node = sexp_new_atom_uint(child_argc, child_call);
		lps argc_bind_to = sexp_read_str(sexp_children(arg_binds)[0]);
		Sexp argv_node = sexp_new_atom_ptr(child_argv, child_call);
		lps argv_bind_to = sexp_read_str(sexp_children(arg_binds)[1]);
		replace_bindings(alloced_macro, argc_bind_to, argc_node, argv_bind_to, argv_node);
		if (!sexp_is_null(at->state.call_results[0])) {
			return at->state.call_results[0];
		} else {
			Sexp result = sexp_dup(p_exec(1, alloced_macro, I, child_call));
			calltree_destroy(*child_call, &I->arenapool);
			return result;
		}
	}
}

#include "intrinsics.h"

#define PRIM_ENTRY(primname) \
primitives_set_macro(prims, lps_from_cstr("p-"#primname), p_##primname);

#include "include/foreach.h"
Primitives *primitives_default() {
	Primitives *prims = primitives_new();
	FOREACH(PRIM_ENTRY,
	        exec, slice_eval, macrobody, is_primitive, multi_eval,
	        read, quote,
	        let, unlet, get,
	        print, format,
	        sendmsg, awaitmsg,
	        list, adrof, deref, sexp_size, typeof, list_from_sexp_slice,
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

