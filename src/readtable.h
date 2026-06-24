#ifndef READTABLE_H_
#define READTABLE_H_

#include "sexp.h"
#include <stdio.h>

typedef struct readtable Readtable;

Readtable *readtable_new();
void readtable_free(Readtable *rt);

// struct reader_macro {
// 	Sexp *fn;
// 	char c;
// };
// the sexp is interpreted
typedef Sexp (*reader_macro)(Interpreter*, char, u16, u16);

reader_macro *readtable_get_macro(const Readtable *rt, char c);
void readtable_add_macro(Readtable *rt, char c, reader_macro macro);

#endif // ifndef READTABLE_H_

