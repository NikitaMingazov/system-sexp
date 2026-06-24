#ifndef PRIMITIVES_T_H
#define PRIMITIVES_T_H

#include "include/ht.h"
#include "sexp.h"
#include "readtable.h"

typedef Sexp (*p_macro)(size_t argc, Sexp* argv, Interpreter* I, CallInst at);

typedef int (*char_pred)(char);

// used for numeral reader macro
typedef struct pred_reader {
	char_pred pred;
	reader_macro reader;
} PredReader;

typedef Ht(lps, p_macro) macro_table;
typedef Ht(char, reader_macro) reader_table;

typedef struct primitives {
	macro_table macros;
	reader_table char_reader_macros;
	PredReader *pred_reader_macros;
	size_t num_predicates;
	lps default_eval_binding;
} Primitives;

// the numeral reader macros must be primitive for ints to exist
// they can be reassigned at runtime
void primitives_export_readers(const Primitives *prims, Readtable *ht);
void primitives_set_reader(Primitives *prims, char c, reader_macro macro);
void primitives_unset_reader(Primitives *prims, char c);

#endif

