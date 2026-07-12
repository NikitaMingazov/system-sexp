// the definitions for the language's intrinsic functions
// the build process parses this file to generate the default primitive initialisation
// so extending the language by appending something here is trivial

#include "lps.h"
#include "primitives.h"
#include "interpreter.h"
#include "sexp.h"
#include "reader.h"
#include <assert.h>
#include <string.h>

// macros are first-class, functions are not (this is a "fexpr" language)
// this is because macros are type agnostic functions that operate on raw sexps
// and which take argc,argv as params without unwrapping the type monad
// which is true for everything written outside the runtime
// functions are implemented in terms of macros in the preamble

// key: B := uintptr_t, S := sexp
// atom is a union: { str, B, Bs, Bd, void*, S* }
// atom's Sexp* is a reference to a single sexp
// the normal reader produces str, reader macros produce the others
// list is a struct: { size_t, Sexp* }
// sexp is a union: { atom, list }
// B_ = B interpreted as _ with the host C's endianness
// Bs := Bintptr_t
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

#define DEFPRIM(TODO_STRLIT, prim_name) \
Sexp prim_name(size_t argc, Sexp *argv, Interpreter *I, CallTree *at)

// evaluate the given argument into the symbol
#define EVAL(symbol, argi) \
	Sexp symbol; \
	if (!sexp_is_null(at->state.call_results[argi])) { \
		symbol = at->state.call_results[argi]; \
	} else { \
		CallTree *child = calltree_child_at(at, &argv[argi], 1, argi, &I->arenapool); \
		symbol = p_exec(1, &argv[argi], I, child); \
		calltree_destroy(*child, &I->arenapool); \
	} \

// calls eval with its args as two sexps corresponding to ptr and length of eval args
// (sym size_t Sexp*) -> S
Sexp p_slice_eval(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 3);
	EVAL(slice_len, 1)
	EVAL(slice_start, 2)
	Sexp *to_call = calltree_alloc(at, sizeof(Sexp));
	*to_call = sexp_list_reserved(at, 1+sexp_read_u64(slice_len));
	*sexp_list_nth(*to_call, 0) = argv[0];
	memcpy(&to_call->qword.children[1], sexp_read_ptr(slice_start), sexp_read_u64(slice_len)*sizeof(Sexp));
	CallTree *child_call = calltree_child_at(at, to_call, sexp_read_u64(slice_len), 0, &I->arenapool);
	return p_exec(1, to_call, I, child_call);
}

// S* -> S
// progn but first arg is (argc argv)
Sexp p_macrobody(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc > 1);
	Sexp cur;
	for (size_t i = 1; i < argc; ++i) {
		if (sexp_is_null(at->state.call_results[i])) {
			CallTree *child = calltree_child_at(at, &argv[i], argc, i, &I->arenapool);
			at->state.call_results[i] = p_exec(1, &argv[i], I, child);
			calltree_destroy(*child, &I->arenapool);
		}
	}
	return at->state.call_results[argc-1];
}

Sexp p_read(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 0);
	Sexp s = reads(I);
	if (sexp_is_null(s)) {
		// TODO: message passing
		if (!I->error_flags) {
			exit(0);
		} else {
			exit(1);
		}
	}
	return s;
}

// S -> S
DEFPRIM("p-quote", p_quote) {
	assert(argc == 1);
	return argv[0];
}

// evaluates its first argument and then again for every other argument (their values are ignored)
// (S ()*) -> S
Sexp p_multi_eval(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc > 1);
	Sexp prev;
	for (size_t i = 0; i < argc; ++i) {
		if (!sexp_is_null(at->state.call_results[i])) {
			prev = at->state.call_results[i];
		} else {
			CallTree *child = calltree_child_at(at, &argv[i], 1, i, &I->arenapool);
			if (i == 0)
				prev = p_exec(1, &argv[i], I, child);
			else
				prev = p_exec(1, &prev, I, child);
			at->state.call_results[i] = prev;
			calltree_destroy(*child, &I->arenapool);
		}
	}
	return prev;
}

Sexp p_is_primitive(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	EVAL(a, 0)
	bool result_v = primitives_contains_macro(I->prims, sexp_read_str(a));
	return sexp_new_atom_int(result_v, at);
}
// (str, S) -> ()
Sexp p_let(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 2);
	EVAL(val, 1)
		// TODO: expose call_tree and symboltable primitives
	CallTree *parent = at->parent->parent->parent->parent;
	Sexp called_by = sexp_children(*parent->state.sexp_called)[0];
	while (!called_by.is_list && sexp_atom_type(called_by) == A_SYM && lps_cmp(sexp_read_str(called_by), lps_from_cstr("eval")) == 0) {
		parent = parent->parent;
	}
	int err = calltree_set_symbol_val(
	                                  parent,
                                      sexp_read_str(argv[0]),
                                      sexp_dup(val)
	                                 );
	assert(!err);
	return sexp_new_list(at);
}
// str -> S
Sexp p_unlet(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	return calltree_remove_symbol_val(at->parent, sexp_read_str(argv[0]));
}
// str -> &mut S
Sexp p_get(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	EVAL(val, 0)
	return sexp_new_atom_ptr(calltree_get_symbol_val_ref(at, sexp_read_str(val)), at);
}

Sexp p_path_at(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 0);
	return sexp_new_atom_ptr(at, at);
}

// str -> ()
Sexp p_print(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	EVAL(post, 0)
	fprintf(stdout, ""LPS_Fmt"", LPS_Arg(sexp_read_str(post)));
	return sexp_new_list(at);
}

// S -> str
Sexp p_format(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	EVAL(a, 0)
	return sexp_new_atom_str(sexp_format(a), at);
}

// S -> ()
Sexp p_sendmsg(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	Sexp *msg = malloc(sizeof(Sexp));
	memcpy(msg, argv, sizeof(Sexp));
	slave_send_msg(I->parent_channel, msg);
	return sexp_new_list(at);
}
// () -> S
Sexp p_awaitmsg(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 0);
	Sexp msg;
	Sexp *in = slave_await_msg(I->parent_channel);
	memcpy(&msg, in, sizeof(Sexp));
	free(in);
	return msg;
}

// Sexp API ===============================================

// (Sexp* size_t) -> List
Sexp p_list(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 2);
	EVAL(start, 0)
	EVAL(len, 1)
	Sexp result = sexp_new_list(at);
	result.qword.children = sexp_read_ptr(start);
	result.word.num_children = sexp_read_u64(len);
	return result;
}

// returns the Sexp node type
// S -> Bu
Sexp p_typeof(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	EVAL(val, 0)
	if (val.is_list)
		return sexp_new_atom_uint(LIST_T, at);
	return sexp_new_atom_uint(sexp_atom_type(val), at);
}

// Sexp memory primitives
// S -> S*
Sexp p_adrof(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	EVAL(val, 0)
	Sexp *result = calltree_alloc(at->parent, sizeof(Sexp));
	*result = val;
	return sexp_new_atom_ptr(result, at);
}
// S* -> S
// TODO: possible remove this, replace with alloca+memcpy in ss
Sexp p_deref(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	EVAL(ptr, 0)
	return * (Sexp*) sexp_read_ptr(ptr);
}
// () -> size_t
Sexp p_sexp_size(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 0);
	return sexp_new_atom_uint(sizeof(Sexp), at);;
}
// (Sexp*, size_t) -> S
Sexp p_list_from_sexp_slice(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 2);
	EVAL(start, 0)
	EVAL(count, 1)
	Sexp* start_v = sexp_read_ptr(start);
	size_t count_v = sexp_read_u64(count);
	Sexp result = sexp_list_reserved(at, count_v);
	for (size_t i = 0; i < count_v; ++i) {
		*sexp_list_nth(result, i) = start_v[i];
	}
	return result;
}

// string API ==============================================
// (the other methods are reducible with memcpy)
// str -> size_t
Sexp p_strlen(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	EVAL(str, 0)
	size_t result_v = lps_len(sexp_read_str(str));
	return sexp_new_atom_uint(result_v, at);
}
// size_t -> str
Sexp p_uninit_str_from_len(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	lps result_v = lps_with_reserved_len(sexp_read_u64(argv[0]));
	Sexp result = sexp_new_atom_str(result_v, at);
	return result;
}
// str -> ()
Sexp p_str_free(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	lps_free(sexp_read_str(argv[0]));
	return sexp_new_list(at);
}


/*
   this is no longer a primitive, this comment remains as reference for the ss impl
the type system uses a monad to represent types as (T, V)
where T is a type and V is a value
usage (assuming int literals are s32 by default)
(let inc
  (fn (s32 k) s32
	(+ k 1)))
; by the way, 'fn' expands to:
; ((s32 -> s32)
;  ((s32 k) s32
;   (+ k 1)))
; processing example (abridged, not step by step)
; 0:
(let n 3)
(eval (quote (inc n)))
; 1:
(s32                    ; eval is a macro, s32 is not a fn
 (+ (& (s32 3))
	(s32 1)))
; 2:
((((s32 s32) -> s32)
  (p-s+32))             ; in reality, + is evaluated from within C for s32
 (& (s32 3))
 (s32 1))
; 3:  [here's the monad]
(s32 (p-s+32 3 1))      ; 3 is not freed by eval as it is not owned
; 4:
(s32 4)
*/
// eval doesn't free borrows, including the first arg which is always one
// & and quote's arguments are read without freeing them
/*
Sexp eval(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	Sexp result = NULL;
	if (argc != 1) {
		fprintf(stderr, "eval takes only one argument\n");
		return NULL;
	}
	// evaluating non-nil atom (symbol)
	if (!argv[0].is_list) {
		result = symboltable_peek(I->st, argv[0].val.atom.val.as_str);
		sexp_free(argv[0]);
		return result;
	}
	// evaluating nil (no-op)
	// TODO: remove nil?
	if (argv[0].val.list.num_children == 0) {
		return argv[0];
	}
	List to_eval = argv[0].val.list;
	Sexp fn_atom = to_eval.children[0];
	lps fn_name = fn_atom->val.atom.val.as_str;
	if (fn_atom->is_list) {
		fprintf(stderr, "leftmost list element cannot be a list\n");
		return NULL;
	}
	// get return type
	Sexp st_entry = symboltable_peek(I->st, fn_name);
	assert(st_entry);
	assert(st_entry->is_list);
	Sexp return_type = st_entry->val.list.children[0];
	bool is_macro = false;
	lps macro_str = lps_from_cstr("macro");
	if (return_type->is_list || lps_cmp(macro_str, return_type->val.atom.val.as_str) != 0) {
		is_macro = true;
	}
	lps_free(macro_str);
	// eval args if not macro
	if (!is_macro) {
		for (size_t i = 1; i < to_eval.num_children; ++i) {
			Sexp cur = to_eval.children[i];
			cur = eval(1, &cur);
		}
	}
	if (primitives_contains(prims, fn_name)) {
		Sexp val = primitives_call
		                (prims, fn_atom->val.atom.val.as_str)
			            (to_eval.num_children-1, &to_eval.children[1]);
		if (!is_macro) {
			// result is (type, val) pair for a fn
			result = sexp_new_list();
			sexp_list_append(&result, return_type);
			sexp_list_append(&result, val);
		} else
			result = val;
	} else {
		// TODO: interpret non-primitive fns
		abort();
	}
	// free the moved in args for a fn
	if (return_type) {
		for (size_t i = 0; i < to_eval.num_children; ++i) {
			sexp_free(argv[i]);
		}
	}
	return result;
}
*/

// C-derived primitives

#define DEFINE_UNARY_FN(NAME, FN, IN_TYPE, READAS, OUT_ATOM) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) { \
	assert(argc == 1); \
	EVAL(a, 0); \
	assert(sizeof(IN_TYPE) <= sizeof(void*)); \
	IN_TYPE a_v = sexp_read_##READAS(a); \
	Sexp result = sexp_new_atom_##OUT_ATOM(FN(a_v), at); \
	return result; \
}

#define DEFINE_TERNARY_FN(NAME, FN, IN1_TYPE, IN1_READAS, IN2_TYPE, IN2_READAS, IN3_TYPE, IN3_READAS, OUT_TYPE, OUT_ATOM) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) { \
	assert(argc == 3); \
	EVAL(a, 0); \
	EVAL(b, 1); \
	EVAL(c, 2); \
	assert(sizeof(IN1_TYPE) <= sizeof(void*)); \
	assert(sizeof(IN2_TYPE) <= sizeof(void*)); \
	assert(sizeof(IN3_TYPE) <= sizeof(void*)); \
	assert(sizeof(OUT_TYPE) <= sizeof(void*)); \
	IN1_TYPE a_v = sexp_read_##IN1_READAS(a); \
	IN2_TYPE b_v = sexp_read_##IN2_READAS(b); \
	IN3_TYPE c_v = sexp_read_##IN3_READAS(c); \
	Sexp result = sexp_new_atom_##OUT_ATOM(FN(a_v, b_v, c_v), at); \
	return result; \
}

// ptr -> ()
Sexp p_free(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	free(sexp_read_ptr(argv[0]));
	Sexp result = sexp_new_list(at);
	return result;
}

// (ptr, size_t) -> ptr
Sexp p_realloc(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 2);
	void* a_v = sexp_read_ptr(argv[0]);
	size_t b_v = sexp_read_u64(argv[1]);
	void* result_v = realloc(a_v, b_v);
	return sexp_new_atom_ptr(result_v, at);
}

Sexp p_alloca(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 1);
	void *result_v = calltree_alloc(at, sexp_read_u64(argv[0]));
	return sexp_new_atom_ptr(result_v, at);
}
DEFINE_UNARY_FN(p_malloc, malloc, size_t, u64, ptr)
DEFINE_TERNARY_FN(p_memcmp, memcmp, void*, ptr, void*, ptr, size_t, u64, int, int)
DEFINE_TERNARY_FN(p_memcpy, memcpy, void*, ptr, void*, ptr, size_t, u64, void*, ptr)
DEFINE_TERNARY_FN(p_memset, memset, void*, ptr, int, s64, size_t, u64, void*, ptr)

// (Bint S S) -> S
Sexp p_if(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 2 || argc == 3);
	EVAL(cond, 0)
	int cond_v = sexp_read_s64(cond);
	Sexp t_branch = argv[1];
	Sexp f_branch = argc == 3 ? argv[2] : sexp_new_list(at);
	Sexp branch;
	if (cond_v)
		branch = t_branch;
	else
		branch = f_branch;
	return sexp_dup(branch);
}

// returns the final expr's result
// does not use a dispatcher, performs the loop in C
// (Bint S) -> S
Sexp p_while(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc > 0);
	Sexp result = sexp_new_list(at);
	while (1) {
		EVAL(cond, 0)
		if (!sexp_read_s64(cond))
			break;
		for (size_t i = 1; i < argc; ++i) {
			if (i+1 < argc) {
				EVAL(ignored, i)
			} else {
				EVAL(result, i)
			}
		}
		// after the iteration, wipe eval state for the next
		memset(
		        at->state.call_results,
		        0,
		        at->state.num_call_results * sizeof(Sexp)
		      );
	}
	return sexp_dup(result);
}

// (Bint Bint) -> Bint
Sexp p_and(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 2);
	EVAL(a, 0)
	if (!sexp_read_s64(a))
		return sexp_new_atom_int(0, at);
	EVAL(b, 1)
	if (!sexp_read_s64(b))
		return sexp_new_atom_int(0, at);
	else
		return sexp_new_atom_int(1, at);
}

// (Bint Bint) -> Bint
Sexp p_or(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) {
	assert(argc == 2);
	EVAL(a, 0)
	if (sexp_read_s64(a))
		return sexp_new_atom_int(1, at);
	EVAL(b, 1)
	if (sexp_read_s64(b))
		return sexp_new_atom_int(1, at);
	else
		return sexp_new_atom_int(0, at);
}

#define DEFINE_BINARY_OP(NAME, OP, IN_TYPE, READAS, OUT_TYPE, OUT_ATOM) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) { \
	assert(argc == 2); \
	EVAL(a, 0) \
	EVAL(b, 1) \
	assert(sizeof(IN_TYPE) <= sizeof(void*)); \
	assert(sizeof(OUT_TYPE) <= sizeof(void*)); \
	IN_TYPE a_v = sexp_read_##READAS(a); \
	IN_TYPE b_v = sexp_read_##READAS(b); \
	OUT_TYPE result_v = a_v OP b_v; \
	Sexp result = sexp_new_atom_##OUT_ATOM(result_v, at); \
	return result; \
}

#define DEFINE_UNARY_OP(NAME, OP, IN_TYPE, READAS, OUT_TYPE, OUT_ATOM) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) { \
	assert(argc == 1); \
	EVAL(a, 0) \
	assert(sizeof(IN_TYPE) <= sizeof(void*)); \
	assert(sizeof(OUT_TYPE) <= sizeof(void*)); \
	IN_TYPE a_v = sexp_read_##READAS(a); \
	OUT_TYPE result_v = OP a_v; \
	Sexp result = sexp_new_atom_##OUT_ATOM(result_v, at); \
	return result; \
}

DEFINE_UNARY_OP(p_not, !, int, s64, int, int)
DEFINE_BINARY_OP(p_biteq, ==, uintptr_t, u64, int, int) // technically UB union usage
DEFINE_BINARY_OP(p_bitneq, !=, uintptr_t, u64, int, int)
DEFINE_BINARY_OP(p_bitand, &, uintptr_t, u64, uintptr_t, int)
DEFINE_BINARY_OP(p_bitor, |, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_bitxor, ^, uintptr_t, u64, uintptr_t, uint)
// p_compl for complement?
DEFINE_UNARY_OP(p_bitnot, ~, uintptr_t, u64, uintptr_t, uint)

DEFINE_BINARY_OP(p_uadd, +, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_usub, -, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_umul, *, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_udiv, /, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_umod, &, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_ushl, <<, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_ushr, >>, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP(p_ult, <, uintptr_t, u64, int, int)
DEFINE_BINARY_OP(p_ulte, <=, uintptr_t, u64, int, int)
DEFINE_BINARY_OP(p_ugt, >, uintptr_t, u64, int, int)
DEFINE_BINARY_OP(p_ugte, >=, uintptr_t, u64, int, int)
DEFINE_BINARY_OP(p_sadd, +, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_ssub, -, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_smul, *, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_sdiv, /, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_smod, &, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_sshl, <<, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_sshr, >>, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP(p_slt, <, intptr_t, s64, int, int)
DEFINE_BINARY_OP(p_slte, <=, intptr_t, s64, int, int)
DEFINE_BINARY_OP(p_sgt, >, intptr_t, s64, int, int)
DEFINE_BINARY_OP(p_sgte, >=, intptr_t, s64, int, int)
DEFINE_BINARY_OP(p_fadd, +, double, f64, double, float)
DEFINE_BINARY_OP(p_fsub, -, double, f64, double, float)
DEFINE_BINARY_OP(p_fmul, *, double, f64, double, float)
DEFINE_BINARY_OP(p_fdiv, /, double, f64, double, float)
DEFINE_BINARY_OP(p_flt, <, double, f64, int, int)
DEFINE_BINARY_OP(p_flte, <=, double, f64, int, int)
DEFINE_BINARY_OP(p_fgt, >, double, f64, int, int)
DEFINE_BINARY_OP(p_fgte, >=, double, f64, int, int)

