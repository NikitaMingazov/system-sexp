#ifndef SYMBOLTABLE_H_
#define SYMBOLTABLE_H_

#include "sexp.h"
#include "lps.h"

typedef uint32_t u32;

typedef struct entry {
	lps key;
	Sexp val;
} Entry;

typedef struct symboltable {
	Entry *hashtable;
	u32 capacity;
	u32 size;
} Symboltable;


Symboltable *symboltable_new();
void symboltable_free(Symboltable *st);

int symboltable_set(Symboltable *st, const lps key, Sexp val);
Sexp symboltable_remove(Symboltable *st, const lps key);
Sexp symboltable_peek(Symboltable *st, const lps key);
// mutable reference
Sexp *symboltable_get_ref(Symboltable *st, const lps key);

#endif // ifndef SYMBOLTABLE_H_

