// the language's intrinsics
// I call them primitives because (p-name arg) is better than (i-name arg)
// , as concerning these having a 'namespace'. also I is taken by "interpreter"
#define HT_IMPLEMENTATION
#include "include/ht.h"

#include "primitives.h"
#include "interpreter.h"
#include "buffer.h"
#include "reader.h"
#include "readtable.h"
#include "lps.h"
#include "sexp.h"
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

#include "intrinsics.h"
#include "exported_intrinsics.h"

