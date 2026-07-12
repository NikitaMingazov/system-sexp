#ifndef PRIMITIVES_H_
#define PRIMITIVES_H_

#include "primitives_t.h"
#include "interpreter.h"
#include "sexp.h"
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

Primitives *primitives_default();
p_macro primitives_get_macro(const Primitives *prims, const lps fn);
bool primitives_contains_macro(const Primitives *prims, const lps fn);
void primitives_set_macro(Primitives *prims, const lps fn, p_macro macro);
void primitives_unset_macro(Primitives *prims, const lps fn);

// interpreter.c uses this one
extern Sexp p_exec(size_t argc, Sexp *argv, Interpreter *I, CallTree *at);

#endif

