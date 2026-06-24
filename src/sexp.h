#ifndef SEXP_H_
#define SEXP_H_

#include "interpreter_t.h"
#include "lps.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

// this is for sexp_free and is not exposed to ss
// I'd like to remove this with scoped arenas later
enum atom_type {
	// Used to represent an invalid sexp (on 0-init)
	A_NULL = 0,
	// lps
	A_STR  = 1,
	// uintptr_t
	A_UVAL = 2,
	// intptr_t
	A_SVAL = 3,
	// double
	A_FVAL = 4,
	// void*
	A_PTR  = 5,
};

typedef union atom_val {
	// by default atoms are strings
	lps as_str;
	// but they might also be used as values
	uintptr_t uval;
	intptr_t sval;
	double fval;
	// or a memory address
	void *as_ptr;
} AtomVal;

typedef struct atom {
	AtomVal val;
	enum atom_type type;
} Atom;

struct sexp;
typedef struct list {
	struct sexp *children;
	u16 num_children;
} List;

union sexp_data {
	Atom atom;
	List list;
};

typedef struct sexp {
	union sexp_data val;
	u16 row;
	u16 col;
	bool is_list;
} Sexp;

// for the reader
Sexp sexp_new_source_atom(lps str, u16 row, u16 col);
Sexp sexp_new_source_list(u16 row, u16 col);
// uses the ctx global for row/col (for primitive macros)
Sexp sexp_new_list(CallInst at);
Sexp sexp_new_atom_str(lps str, CallInst at);
Sexp sexp_new_atom_ptr(void *ptr, CallInst at);
Sexp sexp_new_atom_int(intptr_t value, CallInst at);
Sexp sexp_new_atom_uint(uintptr_t value, CallInst at);
Sexp sexp_new_atom_float(double value, CallInst at);

lps sexp_format(Sexp sexp);

// deep copy of a sexp
Sexp sexp_dup(Sexp sexp);
// deep free of a sexp's arrays
// does not free the ptr atoms
void sexp_free(Sexp sexp);

void sexp_list_append(Sexp *list, Sexp addition);
bool sexp_is_nil(Sexp s);

// a zeroed sexp to represent internal errors/end of stream/etc.
Sexp sexp_null();
bool sexp_is_null(Sexp s);

#endif // ifndef SEXP_H_

