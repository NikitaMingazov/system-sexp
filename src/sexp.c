#include "sexp.h"
#include "buffer.h"
#include "interpreter.h"
#include "lps.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static Sexp atom_new(AtomVal inner, enum atom_type type, u16 row, u16 col) {
	return (Sexp) {
		.val.atom.val = inner,
		.val.atom.type = type,
		.is_list = false,
		.row = row,
		.col = col,
	};
}

static Sexp sexp_new_list_at(u16 row, u16 col) {
	union sexp_data inner = (union sexp_data) (List) {
		.children = NULL,
		.num_children = 0,
	};
	return (Sexp) {
		.val = inner,
		.is_list = true,
		.row = row,
		.col = col,
	};
}

Sexp sexp_new_source_atom(lps val, u16 row, u16 col) {
	return atom_new((AtomVal) val, A_STR, row, col);
}

Sexp sexp_new_source_list(u16 row, u16 col) {
	return sexp_new_list_at(row, col);
}

#define SEXP_ATOM_BUILDER(FSUFFIX, TYPE, DISCRIMINANT) \
Sexp sexp_new_atom_##FSUFFIX(TYPE value, CallInst at) { \
	u16 row = callinst_row_at_call(at); \
	u16 col = callinst_col_at_call(at); \
	return atom_new((AtomVal)value, DISCRIMINANT, row, col); \
}

SEXP_ATOM_BUILDER(str, lps, A_STR)
SEXP_ATOM_BUILDER(ptr, void*, A_PTR)
SEXP_ATOM_BUILDER(uint, uintptr_t, A_UVAL)
SEXP_ATOM_BUILDER(int, intptr_t, A_SVAL)
SEXP_ATOM_BUILDER(float, double, A_FVAL)

Sexp sexp_new_list(CallInst at) {
	u16 row = callinst_row_at_call(at); \
	u16 col = callinst_col_at_call(at); \
	return sexp_new_list_at(row, col);
}

void sexp_list_append(Sexp *target_list, Sexp addition) {
	List *list = &target_list->val.list;
	list->num_children++;
	list->children = realloc(list->children, sizeof(*list->children) * list->num_children);
	list->children[list->num_children-1] = addition;
}

Sexp sexp_dup(Sexp sexp) {
	Sexp dup;
	memcpy(&dup, &sexp, sizeof(dup));
	if (sexp.is_list) {
		for (size_t i = 0; i < dup.val.list.num_children; ++i) {
			dup.val.list.children[i] = sexp_dup(sexp.val.list.children[i]);
		}
	}
	return dup;
}

void sexp_free(Sexp sexp) {
	if (sexp.is_list) {
		for (size_t i = 0; i < sexp.val.list.num_children; ++i) {
			sexp_free(sexp.val.list.children[i]);
		}
		free(sexp.val.list.children);
	} else {
		if (sexp.val.atom.type == A_STR)
			lps_free(sexp.val.atom.val.as_str);
	}
}

bool sexp_is_nil(Sexp s) {
	return s.is_list && s.val.list.num_children == 0;
}

Sexp sexp_null() {
	return (Sexp){0};
}
bool sexp_is_null(Sexp s) {
	Sexp zero = {0};
	return memcmp(&s, &zero, sizeof(Sexp)) == 0;
}

static char *format(const char *fmt, size_t *len, ...) {
	va_list args;
	va_start(args, len);
	*len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);
	if (*len < 0)
		return NULL;
	char *buffer = malloc(*len + 1);
	if (!buffer)
		return NULL;
	va_start(args, len);
	vsnprintf(buffer, *len + 1, fmt, args);
	va_end(args);
	return buffer;
}

void sexp_format_into_buffer(const Sexp sexp, Buffer *buf) {
	if (sexp.is_list) {
		List as_list = sexp.val.list;
		buffer_append_char(buf, '(');
		for (size_t i = 0; i < as_list.num_children; ++i) {
			sexp_format_into_buffer(as_list.children[i], buf);
		}
		buffer_append_char(buf, ')');
	} else {
		Atom as_atom = sexp.val.atom;
		char *tmp = NULL;
		size_t len;
		switch (as_atom.type) {
		case A_STR: {
			buffer_append_chars(buf, as_atom.val.as_str, lps_len(as_atom.val.as_str));
		} break;
		case A_UVAL: {
			tmp = format("%ul", &len, as_atom.val.uval);
			buffer_append_chars(buf, tmp, len);
			free(tmp);
		} break;
		case A_SVAL: {
			tmp = format("%l", &len, as_atom.val.sval);
			buffer_append_chars(buf, tmp, len);
			free(tmp);
		} break;
		case A_FVAL: {
			tmp = format("%f", &len, as_atom.val.fval);
			buffer_append_chars(buf, tmp, len);
			free(tmp);
		} break;
		case A_PTR: {
			tmp = format("%p", &len, as_atom.val.as_ptr);
			buffer_append_chars(buf, tmp, len);
			free(tmp);
		} break;
		case A_NULL: abort();
		}
	}
}

lps sexp_format(const Sexp sexp) {
	Buffer *stringbuilder = buffer_new();
	sexp_format_into_buffer(sexp, stringbuilder);
	lps new = lps_with_reserved_len(stringbuilder->len);
	memcpy(new, stringbuilder->data, stringbuilder->len);
	buffer_free(stringbuilder);
	return new;
}

