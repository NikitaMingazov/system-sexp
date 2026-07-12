// lexer and parser in one (sexps are simple enough)

#ifndef READER_H_
#define READER_H_

#include "sexp.h"
#include "interpreter.h"
#include <stdio.h>

// returns the next sexp in the interpreter's stream
// returns sexp_null (no flag) if EOF reached
// if parsing error (only mismatched parentheses) returns sexp_null and
//  sets an error flag in the interpreter
Sexp reads(Interpreter *I);

// reads a single char
int readc(Interpreter *I);
int peekc(Interpreter *I);
// unreads a single char
int unreadc(Interpreter *I, int c);

#endif // ifndef READER_H_

