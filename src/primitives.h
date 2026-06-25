#ifndef PRIMITIVES_H_
#define PRIMITIVES_H_

#include "include/ht.h"
#include "primitives_t.h"
#include "interpreter.h"
#include "sexp.h"
#include "readtable.h"
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

Primitives *primitives_default();
p_macro primitives_get_macro(const Primitives *prims, const lps fn);
bool primitives_contains_macro(const Primitives *prims, const lps fn);
void primitives_set_macro(Primitives *prims, const lps fn, p_macro macro);
void primitives_unset_macro(Primitives *prims, const lps fn);

// macros are first-class, functions are not (this is a "fexpr" language)
// this is because macros are type agnostic functions that operate on raw sexps
// and which take argc,argv as params without unwrapping the type monad
// which is true for everything written outside the runtime
// functions are implemented in terms of macros in the preamble

// key: B := u8[sizeof(void*)], S := sexp
// atom is a union: { str, B, void* }
// the normal reader produces str, reader macros produce the others
// list is { size_t, atom* }
// sexp is a union: { atom, list }
// B_ = B interpreted as _ with the host C's endianness
// Bu := Buintptr_t, Bs := Bintptr_t
// Bd := Bdouble  TODO: Bf

#define DEF_PRIM(prim_name) \
Sexp prim_name(size_t argc, Sexp *argv, Interpreter *I, CallTree *at);

// lisp semantics
// () -> S
DEF_PRIM(p_read)
// S -> S
DEF_PRIM(p_quote)
// (size_t S*) -> S
DEF_PRIM(p_exec)
// str -> Bint
DEF_PRIM(p_is_primitive)
// (str S) -> ()
DEF_PRIM(p_symboltable_push)
// str -> S
DEF_PRIM(p_symboltable_pop)
// str -> S
DEF_PRIM(p_symboltable_peek)

// str -> ()
// for now uses stdout, this is temporary
DEF_PRIM(p_printstr)

// S -> ()
DEF_PRIM(p_sendmsg)
// () -> S
DEF_PRIM(p_awaitmsg)

// used for (malloc (u* 5 (p-DEF_PRIM(-)
// () -> Bu
DEF_PRIM(p_sexp_size)

// an unstable lps is used to represent symbols
// this is the api
// str -> Bu
DEF_PRIM(p_strlen)
// memcpy the bytes into the blank one from this
// this uses malloc, so the user needs to free afterwards
// Bu -> str
DEF_PRIM(p_uninit_str_from_len)
// str -> ()
DEF_PRIM(p_str_free)

// imperative control flow manipulation
// () -> AstPath
DEF_PRIM(p_get_path)
// returns the walked to DEF_PRIM(or)
// AstPath -> S*
DEF_PRIM(p_walk_to_path)
// used for (return V)
// (AstPath, S) -> ()
DEF_PRIM(p_write_to_call_stack_idx)

// C semantics

// Bu -> void*
DEF_PRIM(p_alloca)
// Bu -> void*
DEF_PRIM(p_malloc)
// (void* Bu) -> void*
DEF_PRIM(p_realloc)
// void* -> ()
DEF_PRIM(p_free)
// (void* void* Bu) -> Bint
DEF_PRIM(p_memcmp)
// (void* void* Bu) -> void*
DEF_PRIM(p_memcpy)
// (void* Bint Bu) -> void*
DEF_PRIM(p_memset)
// S -> void*
// Sexp *p_adrof(size_t argc, Sexp *argv, I *I);
// returns the taken branch
// (Bint S S) -> S
DEF_PRIM(p_if)
// (Bint Bint) -> Bint
DEF_PRIM(p_and)
DEF_PRIM(p_or)
// Bint -> Bint
DEF_PRIM(p_not)
// (B B) -> Bint
DEF_PRIM(p_biteq)
DEF_PRIM(p_bitneq)
// (B B) -> B
DEF_PRIM(p_bitand)
DEF_PRIM(p_bitor)
DEF_PRIM(p_bitxor)
DEF_PRIM(p_bitnot)
// there are only sizeof(void*) arithmetic operations in the runtime
// you could of course make a u8 type with memset
// but when you use these ops they'll be partially zeroed u64s
// uintptr_t
// (Bu Bu) -> Bu
DEF_PRIM(p_uadd)
DEF_PRIM(p_usub)
DEF_PRIM(p_umul)
DEF_PRIM(p_udiv)
DEF_PRIM(p_umod)
DEF_PRIM(p_ushl)
DEF_PRIM(p_ushr)
DEF_PRIM(p_ult)
DEF_PRIM(p_ulte)
DEF_PRIM(p_ugt)
DEF_PRIM(p_ugte)
// intptr_t
// (Bs Bs) -> Bs
DEF_PRIM(p_sadd)
DEF_PRIM(p_ssub)
DEF_PRIM(p_smul)
DEF_PRIM(p_sdiv)
DEF_PRIM(p_smod)
DEF_PRIM(p_sshl)
DEF_PRIM(p_sshr)
DEF_PRIM(p_slt)
DEF_PRIM(p_slte)
DEF_PRIM(p_sgt)
DEF_PRIM(p_sgte)
// todo: f32
// double
// (Bd Bd) -> Bd
DEF_PRIM(p_fadd)
DEF_PRIM(p_fsub)
DEF_PRIM(p_fmul)
DEF_PRIM(p_fdiv)
DEF_PRIM(p_flt)
DEF_PRIM(p_flte)
DEF_PRIM(p_fgt)
DEF_PRIM(p_fgte)

#endif

