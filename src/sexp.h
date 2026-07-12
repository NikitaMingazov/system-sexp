#ifndef SEXP_H_
#define SEXP_H_

#include "include/arena.h"
#include "interpreter_t.h"
#include "lps.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

// wasm semantics
// #define canonical_NaN (u64)0x7ff8000000000000;
// #define canonical_NaN_neg (u64)0xfff8000000000000;

// this is for sexp_free and is not exposed to ss
// I'd like to remove this with scoped arenas later
enum atom_type {
	// Used to represent an invalid sexp (on 0-init)
	A_NULL = 0,
	// a symbol is a string tagged by the reader
	A_SYM  = 1,
	// lps
	A_STR  = 2,
	// uintptr_t
	A_UVAL = 3,
	// intptr_t
	A_SVAL = 4,
	// double
	A_FVAL = 5,
	// void*
	A_PTR  = 6,
};
#define LIST_T 7

union atom_val {
	// by default atoms are strings
	lps as_str;
	// but they might also be used as values
	uintptr_t uval;
	intptr_t sval;
	double fval;
	// or a memory address
	void *as_ptr;
};

struct sexp;
typedef struct list {
	struct sexp *children;
	u16 num_children;
} List;

union qword_data {
	union atom_val atom;
	struct sexp *children;
};

union word_data {
	u8 atom_type;
	u16 num_children;
};

typedef struct sexp {
	union qword_data qword;
	union word_data word;
	u16 row;
	u16 col;
	u8 is_list;
	// u8 flags; // 1: is_list
} Sexp;

// for the reader
Sexp sexp_new_source_atom(lps str, u16 row, u16 col);
Sexp sexp_new_source_list(u16 row, u16 col);
// used in primitive macros TODO: move out of sexp
Sexp sexp_new_list(CallTree *at);
Sexp sexp_new_atom(union atom_val val, enum atom_type type, CallTree *at);
Sexp sexp_new_atom_str(lps str, CallTree *at);
Sexp sexp_new_atom_ptr(void *ptr, CallTree *at);
Sexp sexp_new_atom_ref(void *ptr, CallTree *at);
Sexp sexp_new_atom_int(intptr_t value, CallTree *at);
Sexp sexp_new_atom_uint(uintptr_t value, CallTree *at);
Sexp sexp_new_atom_float(double value, CallTree *at);

// because of the nasty structure for alignment, these methods
//  provide a stable and ergonomic interface
bool sexp_is_list(Sexp s);
// atom reads (type-safe)
enum atom_type sexp_atom_type(Sexp s);
uintptr_t sexp_read_u64(Sexp s);
intptr_t sexp_read_s64(Sexp s);
double sexp_read_f64(Sexp s);
void* sexp_read_ptr(Sexp s);
lps sexp_read_str(Sexp s);
// list_reads
Sexp* sexp_children(Sexp s);
size_t sexp_num_children(Sexp s);
void sexp_list_append(Sexp *list, Sexp addition, Arena *a);
// create an uninit list with a len
Sexp sexp_list_reserved(CallTree *at, size_t len);
// returns reference to nth element, or NULL if OOB
Sexp *sexp_list_nth(Sexp list, size_t n);

bool sexp_is_nil(Sexp s);
// convert to string
lps sexp_format(Sexp sexp);

// deep copy of a sexp
Sexp sexp_dup(Sexp sexp);
// deep free of a sexp's arrays
// does not free the ptr atoms
void sexp_destroy(Sexp sexp);

// a zeroed sexp to represent internal errors/end of stream/etc.
Sexp sexp_null();
bool sexp_is_null(Sexp s);

#endif // ifndef SEXP_H_

