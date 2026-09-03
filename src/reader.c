// lexer and parser are merged because sexps are simple
#include "reader.h"
#include "allocators/std-alloc.h"
#include "include/nob.h"
#include "readtable.h"
#include "interpreter.h"
#include "lps.h"
#include "sexp.h"
#include "buffer.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// reads a single char (doesn't trigger macros)
int readc(Interpreter *I) {
	int c = fgetc(I->in_stream);
	if (c == '\n') {
		I->tail_col = 0;
		++I->tail_row;
	} else {
		++I->tail_col;
	}
	return c;
}

int peekc(Interpreter *I) {
	int c = fgetc(I->in_stream);
	ungetc(c, I->in_stream);
	return c;
}

// unreads a single char
int unreadc(Interpreter *I, int c) {
	int r = ungetc(c, I->in_stream);
	if (c == '\n') {
		I->tail_col = UINT16_MAX; // TODO: recover real col (stack_pop(col_stack))
		--I->tail_row;
	} else {
		--I->tail_col;
	}
	return r;
}

typedef struct {
	Sexp *items;
	u32 count;
	u32 capacity;
} Sexps;

// returns the first sexp found in the context's stream
Sexp reads(Interpreter *I, bool do_reader_macros) {
	int c = readc(I);
	while (c == ' ' || c == '\n') {
		c = readc(I);
	}
	if (do_reader_macros && readtable_get_macro(I->rt, c)) {
		return (*readtable_get_macro(I->rt, c)) (I, c, I->tail_row, I->tail_col);
	}
	if (c == EOF) // TODO: send done msg
		return sexp_null(); // done, nothing to read, but no error either
	unreadc(I, c);

	Buffer *buf = buffer_new();
	u32 open_row = I->tail_row;
	u16 open_col = I->tail_col;
	if (c == ')') {
		fprintf(stderr, "reader: Unexpected ')' at %d:%d\n", open_row, open_col);
		I->error_flags ^= ERR_UNMATCED_RPAR;
		return sexp_null();
	}
	// returning an atom
	if (c != '(') {
		while ((c = readc(I)) != EOF) {
			if (!strchr(" \n()", c)) {
				buffer_append_char(buf, c);
			} else {
				unreadc(I, c);
				break;
			}
		}
		Sexp atom = sexp_new_source_atom(
		               lps_from_chars_with_len(buf->data, buf->len, std_allocator()),
		               open_row,
		               open_col
		             );
		buffer_free(buf);
		return atom;
	}
	// returning a list
	// pop the '('
	readc(I);
	// Sexp out_list = sexp_new_source_list(open_row, open_col);
	Sexps out_list = {0};
	uint child_start_row;
	uint child_start_col;
	while ((c = readc(I))) {
		if (do_reader_macros && readtable_get_macro(I->rt, c)) {
			nob_da_append(&out_list,
			              (*readtable_get_macro(I->rt, c)) (I, c, I->tail_row, I->tail_col)
			);
			continue;
		}
		switch (c) {
			case EOF: // TODO: send this message and block, wait for message to continue
				fprintf(stderr, "reader: Unmatched '(' at %d:%d\n", open_row, open_col);
				I->error_flags ^= ERR_UNCLOSED_LPAR;
				return sexp_null();
				break;
			case ' ': case '\n':
				break;
			case '(':
				unreadc(I, c);
				nob_da_append(&out_list,
			                  reads(I, true)
				);
				break;
			case ')':
				goto done;
			default: // atom
				// if macros contains c do macro (maybe EOF is bindable)
				// and append macro result to list
				child_start_row = I->tail_row;
				child_start_col = I->tail_col;
				buffer_append_char(buf, c);
				while ((c = readc(I)) != EOF) {
					if (!strchr(" \n()", c)) {
						buffer_append_char(buf, c);
					} else {
						unreadc(I, c);
						break;
					}
				}
				Sexp atom = sexp_new_source_atom(
				        lps_from_chars_with_len(buf->data, buf->len, std_allocator()),
				        child_start_row,
				        child_start_col
				);
				nob_da_append(&out_list,
			                  atom
				);
				buffer_clear(buf);
				break;
		}
	}
done:
	buffer_free(buf);
	Sexp result = sexp_new_source_list(open_row, open_col);
	result.word.num_children = out_list.count;
	if (out_list.count > 0) {
		result.qword.children = calltree_alloc(I->call_root, out_list.count * sizeof(Sexp));
		memcpy(result.qword.children, out_list.items, out_list.count * sizeof(Sexp));
	}
	nob_da_free(out_list); // commenting this out REDUCES memory usage???
	return result;
}

