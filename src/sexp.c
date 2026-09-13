#include "sexp.h"
#include "allocators/allocator.h"
#include "allocators/std-alloc.h"
#include "buffer.h"
#include "interpreter.h"
#include "lps.h"
#include <stdarg.h>
#include <assert.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
	assert(sexp_atom_type(s) == A_PTR || sexp_atom_type(s) == A_UVAL);
	return s.qword.atom.as_ptr;
}
lps sexp_read_str(Sexp s) {
	if (!(sexp_atom_type(s) == A_STR || sexp_atom_type(s) == A_SYM))
		fprintf(stderr, "for sexp |"LPS_Fmt"|", LPS_Arg(sexp_format(s, std_allocator())));
	assert(sexp_atom_type(s) == A_STR || sexp_atom_type(s) == A_SYM);
	return s.qword.atom.as_str;
}
// list_reads
// pointer to the array within a list
Sexp* sexp_children(Sexp s) {
	assert(s.is_list);
	return s.qword.children;
}
// number of elements within a list
size_t sexp_num_children(Sexp s) {
	assert(s.is_list);
	return s.word.num_children;
}
// returns reference to nth element, or NULL if OOB
Sexp *sexp_list_nth(Sexp list, size_t n) {
	if (sexp_num_children(list) > n)
		return &sexp_children(list)[n];
	else
		return NULL;
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

SEXP_ATOM_BUILDER(sym, lps, A_SYM)
SEXP_ATOM_BUILDER(str, lps, A_STR)
SEXP_ATOM_BUILDER(ptr, void*, A_PTR)
SEXP_ATOM_BUILDER(uint, uintptr_t, A_UVAL)
SEXP_ATOM_BUILDER(int, intptr_t, A_SVAL)
SEXP_ATOM_BUILDER(float, double, A_FVAL)

// TODO: switch this and list_at around
Sexp sexp_new_list(CallTree *at) {
	u16 row = at->state.sexp_called[0].row;
	u16 col = at->state.sexp_called[0].col;
	return sexp_new_list_at(row, col);
}

void sexp_list_append(Sexp *target_list, Sexp addition, Allocator a) {
	Sexp *children = sexp_children(*target_list);
	target_list->word.num_children++;
	size_t num_children = sexp_num_children(*target_list);
	size_t old_size = sizeof(Sexp) * (num_children-1);
	size_t new_size = sizeof(Sexp) * num_children;
	children = a.realloc(a.ctx, children, old_size, new_size, alignof(Sexp));
	children[num_children-1] = addition;
	target_list->qword.children = children;
}

// create an uninit list of a len
// TODO: parametric allocator
Sexp sexp_list_reserved(CallTree *at, size_t len) {
	Sexp new = sexp_new_list(at);
	new.qword.children = calltree_alloc(at, len * sizeof(Sexp));
	memset(new.qword.children, 0, len * sizeof(Sexp));
	new.word.num_children = len;
	return new;
}

// deep copy of a sexp
Sexp sexp_dup(Sexp sexp, Allocator a) {
	Sexp dup;
	memcpy(&dup, &sexp, sizeof(Sexp));
	if (sexp.is_list) {
		dup.qword.children = a.alloc(a.ctx, sizeof(Sexp) * sexp.word.num_children, alignof(Sexp));
		for (size_t i = 0; i < sexp_num_children(sexp); ++i) {
			dup.qword.children[i] = sexp_dup(sexp_children(sexp)[i], a);
		}
	}
	return dup;
}

// deep free of a sexp
void sexp_destroy(Sexp sexp, Allocator a) {
	if (sexp.is_list) {
		for (size_t i = 0; i < sexp_num_children(sexp); ++i) {
			sexp_destroy(sexp_children(sexp)[i], a);
		}
		a.free(a.ctx, sexp_children(sexp), sizeof(Sexp) * sexp_num_children(sexp));
	} else {
		// TODO: figure out the lifetime of strings
		// if (sexp_atom_type(sexp) == A_STR)
		// 	lps_free(sexp.qword.atom.as_str, std_allocator());
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

static char *format(const char *fmt, size_t *len, Allocator a, ...) {
	va_list args;
	va_start(args, a);
	*len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);
	if (*len < 0)
		return NULL;
	char *buffer = a.alloc(a.ctx, *len + 1, alignof(char));
	memset(buffer, 0, *len + 1);
	if (!buffer)
		return NULL;
	va_start(args, a);
	vsnprintf(buffer, *len + 1, fmt, args);
	va_end(args);
	return buffer;
}

void sexp_format_into_buffer(const Sexp s, Buffer *buf, Allocator a) {
	if (s.is_list) {
		buffer_append_char(buf, '(');
		for (size_t i = 0; i < sexp_num_children(s); ++i) {
			sexp_format_into_buffer(sexp_children(s)[i], buf, a);
			if (i+1 < sexp_num_children(s))
				buffer_append_char(buf, ' ');
		}
		buffer_append_char(buf, ')');
	} else {
		char *tmp = NULL;
		size_t len;
		switch (sexp_atom_type(s)) {
		case A_STR: case A_SYM: {
			buffer_append_chars(buf, (char*)sexp_read_str(s), lps_len(sexp_read_str(s)));
		} break;
		case A_UVAL: {
			tmp = format("%lu", &len, a, sexp_read_u64(s));
			buffer_append_chars(buf, tmp, len);
			a.free(a.ctx, tmp, alignof(typeof(*tmp)));
		} break;
		case A_SVAL: {
			tmp = format("%ld", &len, a, sexp_read_s64(s));
			buffer_append_chars(buf, tmp, len);
			a.free(a.ctx, tmp, alignof(typeof(*tmp)));
		} break;
		case A_FVAL: {
			tmp = format("%f", &len, a, sexp_read_f64(s));
			buffer_append_chars(buf, tmp, len);
			a.free(a.ctx, tmp, alignof(typeof(*tmp)));
		} break;
		case A_PTR: {
			tmp = format("%p", &len, a, sexp_read_ptr(s));
			buffer_append_chars(buf, tmp, len);
			a.free(a.ctx, tmp, alignof(typeof(*tmp)));
		} break;
		case A_NULL: {
			// TODO: panic here?
			buffer_append_chars(buf, "(NULL)", 6);
		} break;
		}
	}
}

lps sexp_format(const Sexp sexp, Allocator a) {
	Buffer *stringbuilder = buffer_new();
	// TODO: thread-local scratch arena
	sexp_format_into_buffer(sexp, stringbuilder, std_allocator());
	lps new = lps_with_reserved_len(stringbuilder->len, a);
	memcpy(new, stringbuilder->data, stringbuilder->len);
	buffer_free(stringbuilder);
	return new;
}

