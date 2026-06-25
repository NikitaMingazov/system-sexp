#include "sexp.h"
#include "buffer.h"
#include "interpreter.h"
#include "lps.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static Sexp atom_new(union atom_val inner, enum atom_type type, u16 row, u16 col) {
	return (Sexp) {
		.qword.atom = inner,
		.word.atom_type = type,
		.row = row,
		.col = col,
		.is_list = false,
	};
}

static Sexp sexp_new_list_at(u16 row, u16 col) {
	return (Sexp) {
		.qword.children = NULL,
		.word.num_children = 0,
		.row = row,
		.col = col,
		.is_list = true,
	};
}

// atom reads
enum atom_type sexp_atom_type(Sexp s) {
	assert(!s.is_list);
	return s.word.atom_type;
}
uintptr_t sexp_read_u64(Sexp s) {
	// assert(sexp_atom_type(s) == A_UVAL);
	return s.qword.atom.uval;
}
intptr_t sexp_read_s64(Sexp s) {
	// assert(sexp_atom_type(s) == A_SVAL);
	return s.qword.atom.sval;
}

double sexp_read_f64(Sexp s) {
	assert(sexp_atom_type(s) == A_FVAL);
	return s.qword.atom.fval;
}
void* sexp_read_ptr(Sexp s) {
	assert(sexp_atom_type(s) == A_PTR);
	return s.qword.atom.as_ptr;
}
lps sexp_read_str(Sexp s) {
	assert(sexp_atom_type(s) == A_STR || sexp_atom_type(s) == A_SYM);
	return s.qword.atom.as_str;
}
// list_reads
Sexp* sexp_children(Sexp s) {
	assert(s.is_list);
	return s.qword.children;
}
size_t sexp_num_children(Sexp s) {
	assert(s.is_list);
	return s.word.num_children;
}

Sexp sexp_new_source_atom(lps val, u16 row, u16 col) {
	return atom_new((union atom_val) val, A_SYM, row, col);
}

Sexp sexp_new_source_list(u16 row, u16 col) {
	return sexp_new_list_at(row, col);
}

#define SEXP_ATOM_BUILDER(FSUFFIX, TYPE, DISCRIMINANT) \
Sexp sexp_new_atom_##FSUFFIX(TYPE value, CallTree *at) { \
	u16 row = at->state.sexp_called[0].row; \
	u16 col = at->state.sexp_called[0].col; \
	return atom_new((union atom_val)value, DISCRIMINANT, row, col); \
}

SEXP_ATOM_BUILDER(str, lps, A_STR)
SEXP_ATOM_BUILDER(ptr, void*, A_PTR)
SEXP_ATOM_BUILDER(uint, uintptr_t, A_UVAL)
SEXP_ATOM_BUILDER(int, intptr_t, A_SVAL)
SEXP_ATOM_BUILDER(float, double, A_FVAL)

Sexp sexp_new_list(CallTree *at) {
	u16 row = at->state.sexp_called[0].row;
	u16 col = at->state.sexp_called[0].col;
	return sexp_new_list_at(row, col);
}

void sexp_list_append(Sexp *target_list, Sexp addition) {
	Sexp *children = sexp_children(*target_list);
	target_list->word.num_children++;
	size_t num_children = sexp_num_children(*target_list);
	children = realloc(children, sizeof(*children) * num_children);
	children[num_children-1] = addition;
	target_list->qword.children = children;
}

Sexp sexp_dup(Sexp sexp) {
	Sexp dup;
	memcpy(&dup, &sexp, sizeof(dup));
	if (sexp.is_list) {
		for (size_t i = 0; i < sexp_num_children(sexp); ++i) {
			dup.qword.children[i] = sexp_dup(sexp_children(sexp)[i]);
		}
	}
	return dup;
}

void sexp_free(Sexp sexp) {
	if (sexp.is_list) {
		for (size_t i = 0; i < sexp_num_children(sexp); ++i) {
			sexp_free(sexp_children(sexp)[i]);
		}
		free(sexp_children(sexp));
	} else {
		if (sexp_atom_type(sexp) == A_STR)
			lps_free(sexp.qword.atom.as_str);
	}
}

bool sexp_is_nil(Sexp s) {
	return s.is_list && sexp_num_children(s) == 0;
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

void sexp_format_into_buffer(const Sexp s, Buffer *buf) {
	if (s.is_list) {
		buffer_append_char(buf, '(');
		for (size_t i = 0; i < sexp_num_children(s); ++i) {
			sexp_format_into_buffer(sexp_children(s)[i], buf);
			if (i+1 < sexp_num_children(s))
				buffer_append_char(buf, ' ');
		}
		buffer_append_char(buf, ')');
	} else {
		char *tmp = NULL;
		size_t len;
		switch (sexp_atom_type(s)) {
		case A_STR: case A_SYM: {
			buffer_append_chars(buf, sexp_read_str(s), lps_len(sexp_read_str(s)));
		} break;
		case A_UVAL: {
			tmp = format("%lu", &len, sexp_read_u64(s));
			buffer_append_chars(buf, tmp, len);
			free(tmp);
		} break;
		case A_SVAL: {
			tmp = format("%ld", &len, sexp_read_s64(s));
			buffer_append_chars(buf, tmp, len);
			free(tmp);
		} break;
		case A_FVAL: {
			tmp = format("%f", &len, sexp_read_f64(s));
			buffer_append_chars(buf, tmp, len);
			free(tmp);
		} break;
		case A_PTR: {
			tmp = format("%p", &len, sexp_read_ptr(s));
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

