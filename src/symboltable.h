#ifndef SYMBOLTABLE_H_
#define SYMBOLTABLE_H_

#include "allocators/allocator.h"
#include "include/ht.h"
#include "sexp.h"
#include "lps.h"

typedef Ht(lps, Sexp) inner;

typedef struct symboltable {
	inner table;
} Symboltable;

Symboltable *symboltable_new(Allocator a);
void symboltable_free(Symboltable *st);

int symboltable_set(Symboltable *st, const lps key, Sexp val, Allocator a);
Sexp symboltable_remove(Symboltable *st, const lps key);
Sexp symboltable_peek(Symboltable *st, const lps key);
// mutable reference
Sexp *symboltable_get_ref(Symboltable *st, const lps key);

#endif // ifndef SYMBOLTABLE_H_

