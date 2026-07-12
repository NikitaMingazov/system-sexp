// the primitives that are built into the interpreter
// the build process parses this file to generate the default primitive initialisation
// so extending the language by appending something here is trivial

#include "include/nob.h"
#include "lps.h"
#include "primitives.h"
#include "interpreter.h"
#include "sexp.h"
#include "reader.h"
#include <assert.h>
#include <string.h>

// macros are first-class, functions are not (this is a "fexpr" language)
// functions are implemented in terms of macros in the preamble

// key: B := uintptr_t, S := sexp
// atom is a union: { str, B, Bs, Bd, void* }
// the normal reader produces str, reader macros produce the others
// list is a struct: { size_t, Sexp* }
// sexp is a union: { atom, list }
// B_ = B interpreted as _ with the host C's endianness
// Bs := Bintptr_t
// Bd := Bdouble  TODO: Bf

// extern thread_local jmp_buf unwind_point;

typedef struct {
	Sexp **items;
	uint count;
	uint capacity;
} Sexps;

// replaces symbols with values in a sexp
static Sexp *replace_symbol(Sexp *macrobody, lps replaced_symbol, Sexp val) {
	Sexps to_check = {0};
	nob_da_append(&to_check, macrobody);
	while (to_check.count > 0) {
		Sexp *cur = nob_da_pop(&to_check);
		if (cur->is_list) {
			for (size_t i = 0; i < sexp_num_children(*cur); ++i) {
				nob_da_append(&to_check, sexp_list_nth(*cur, i));
			}
		} else if (sexp_atom_type(*cur) == A_SYM) {
			if (lps_cmp(replaced_symbol, sexp_read_str(*cur)) == 0)
				*cur = val;
		}
	}
	return macrobody;
}

// the build system uses these to generate the intrinsic table
#define DEFINE_PRIMITIVE(IGNORED, prim_name) \
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

static CallTree *parent_ctx(CallTree *at) {
	CallTree *parent = at->parent->parent->parent->parent;
	Sexp called_by = sexp_children(*parent->state.sexp_called)[0];
	while (!called_by.is_list && sexp_atom_type(called_by) == A_SYM && lps_cmp(sexp_read_str(called_by), lps_from_cstr("eval")) == 0) {
		parent = parent->parent;
	}
	return parent;
}

// execution fundamentals =============================================================

// the primitive form of eval
// S -> S
DEFINE_PRIMITIVE("p-exec", p_exec) {
	assert(argc == 1);
	// atom: fetch symbol, do nothing for literals
	if (!argv[0].is_list) {
		switch (sexp_atom_type(argv[0])) {
		case A_SYM: {
			if (primitives_contains_macro(I->prims, sexp_read_str(argv[0]))) {
				return argv[0];
			}
			if (!calltree_get_symbol_val_ref(at, sexp_read_str(argv[0]))) {
				fprintf(stderr, "p-exec: could not find symbol "LPS_Fmt"\n", LPS_Arg(sexp_read_str(argv[0])));
				abort();
			}
			return *calltree_get_symbol_val_ref(at, sexp_read_str(argv[0]));
		} break;
		default: {
			return argv[0];
		} abort();
		}
	}
	Sexp *children = sexp_children(argv[0]);
	size_t num_children = sexp_num_children(argv[0]);
	// (p-exec ()) returns ()
	if (num_children == 0)
		return argv[0];
	// TODO: macro literal support (eval this rather than look in symboltable)
	lps macro_str = sexp_read_str(children[0]);
	size_t child_argc = num_children-1;
	Sexp *child_argv = child_argc > 0 ? &children[1] : NULL;
	if (primitives_contains_macro(I->prims, macro_str)) {
		Sexp *to_eval = calltree_alloc(at, sizeof(Sexp));
		*to_eval = sexp_dup(argv[0]);
		CallTree *call = calltree_child_at(at, to_eval, child_argc, 0, &I->arenapool);
		Sexp result = sexp_dup(primitives_get_macro
		                        (I->prims, macro_str)
		                        (child_argc, child_argv, I, call));
		calltree_destroy(*call, &I->arenapool);
		return result;
	} else {
		// (p-st-push name
		//   (()
		//    ((argc_bind argv_bind)
		//     (S0 (\ p-st-peek argc_bind))
		//     (S1 (\ p-st-peek argv_bind)))))
		// the body is interpreted as a progn
		// unwrapping symbol
		// (() name) defines a macro in the symboltable
		Sexp *st_entry = calltree_get_symbol_val_ref(at, macro_str);
		if (!st_entry) {
			fprintf(stderr, "p-exec: could not find symbol "LPS_Fmt"\n", LPS_Arg(macro_str));
			abort();
		}
		// primitive macros as symbols evaluate to themselves, if something returned one from the symboltable
		// that is symbol is an alias of a macro, be it a prim symbol or a list
		if (!st_entry->is_list && sexp_atom_type(*st_entry) == A_SYM) {
			if (primitives_contains_macro(I->prims, sexp_read_str(*st_entry))) {
				Sexp *to_eval = calltree_alloc(at, sizeof(Sexp));
				*to_eval = sexp_dup(argv[0]);
				CallTree *call = calltree_child_at(at, to_eval, child_argc, 0, &I->arenapool);
				Sexp result = sexp_dup(primitives_get_macro
				                        (I->prims, sexp_read_str(*st_entry))
		                                (child_argc, child_argv, I, call));
				calltree_destroy(*call, &I->arenapool);
				return result;
			} else {
				abort();
			}
		}
		Sexp *st_children = sexp_children(*st_entry);
		assert(sexp_num_children(*st_entry) == 2);
		Sexp st_val = st_children[1];
		assert(sexp_num_children(st_val) >= 2);
		assert(lps_cmp(sexp_read_str(sexp_children(st_val)[0]), lps_from_cstr("p-macrobody")) == 0);
		Sexp arg_binds = sexp_children(st_val)[1];
		assert(arg_binds.is_list);
		assert(sexp_num_children(arg_binds) == 2);
		Sexp *alloced_macro = calltree_alloc(at, sizeof(Sexp));
		*alloced_macro = sexp_dup(st_val);
		CallTree *child_call = calltree_child_at(at, alloced_macro, child_argc, 0, &I->arenapool);
		Sexp argc_node = sexp_new_atom_uint(child_argc, child_call);
		lps argc_bind_to = sexp_read_str(sexp_children(arg_binds)[0]);
		Sexp argv_node = sexp_new_atom_ptr(child_argv, child_call);
		lps argv_bind_to = sexp_read_str(sexp_children(arg_binds)[1]);
		replace_symbol(replace_symbol(alloced_macro, argc_bind_to, argc_node), argv_bind_to, argv_node);
		if (!sexp_is_null(at->state.call_results[0])) {
			return at->state.call_results[0];
		} else {
			Sexp result = sexp_dup(p_exec(1, alloced_macro, I, child_call));
			calltree_destroy(*child_call, &I->arenapool);
			return result;
		}
	}
}

// calls eval with its args as two sexps corresponding to ptr and length of eval args
// (sym size_t Sexp*) -> S
DEFINE_PRIMITIVE("p-slice-eval", p_slice_eval) {
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
// progn but first arg is (argc argv) which is used text substition when called
DEFINE_PRIMITIVE("p-macrobody", macrobody) {
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

// evaluates its first argument and then again for every other argument (their values are ignored)
// (S ()*) -> S
DEFINE_PRIMITIVE("p-multi-eval", p_multi_eval) {
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

// lisp-inspired/misc =====================================================
// () -> S
DEFINE_PRIMITIVE("p-read", p_read) {
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

// str -> ()
DEFINE_PRIMITIVE("p-print", p_print) {
	assert(argc == 1);
	EVAL(post, 0)
	fprintf(stdout, ""LPS_Fmt"", LPS_Arg(sexp_read_str(post)));
	return sexp_new_list(at);
}

// S -> S
DEFINE_PRIMITIVE("p-quote", p_quote) {
	assert(argc == 1);
	return argv[0];
}

// str -> Bint
DEFINE_PRIMITIVE("p-is-primitive", p_is_primitive) {
	EVAL(a, 0)
	bool result_v = primitives_contains_macro(I->prims, sexp_read_str(a));
	return sexp_new_atom_int(result_v, at);
}
// symboltable primitives ===================================================
// TODO
// // (str S) -> ()
// DEFINE_PRIMITIVE("p-st-set", p_symboltable_set)
// // str -> S
// DEFINE_PRIMITIVE("p-st-remove", p_symboltable_remove)
// // str -> S
// DEFINE_PRIMITIVE("p-st-get", p_symboltable_get)
// (sym S) -> *S
DEFINE_PRIMITIVE("p-set", p_set) {
	assert(argc == 2);
	EVAL(sym, 0)
	EVAL(val, 1)
	int err = calltree_set_symbol_val(
	                                  I->call_root,
                                      sexp_read_str(sym),
                                      sexp_dup(val)
	                                 );
	assert(!err);
	return val;
}
// (str, S) -> S
DEFINE_PRIMITIVE("p-let", p_let) {
	assert(argc == 2);
	EVAL(val, 1)
	int err = calltree_set_symbol_val(
	                                  parent_ctx(at),
                                      sexp_read_str(argv[0]),
                                      sexp_dup(val)
	                                 );
	assert(!err);
	return val;
}
// str -> &mut S
DEFINE_PRIMITIVE("p-get", p_get) {
	assert(argc == 1);
	EVAL(val, 0)
	return sexp_new_atom_ptr(calltree_get_symbol_val_ref(at, sexp_read_str(val)), at);
}

// () -> void*
DEFINE_PRIMITIVE("p-ctx", p_path_at) {
	assert(argc == 0);
	return sexp_new_atom_ptr(at, at);
}

// S -> ()
DEFINE_PRIMITIVE("p-sendmsg", p_sendmsg) {
	assert(argc == 1);
	Sexp *msg = malloc(sizeof(Sexp));
	memcpy(msg, argv, sizeof(Sexp));
	slave_send_msg(I->parent_channel, msg);
	return sexp_new_list(at);
}
// () -> S
DEFINE_PRIMITIVE("p-awaitmsg", p_awaitmsg) {
	assert(argc == 0);
	Sexp msg;
	Sexp *in = slave_await_msg(I->parent_channel);
	memcpy(&msg, in, sizeof(Sexp));
	free(in);
	return msg;
}

// Sexp API ===============================================

// S -> str
DEFINE_PRIMITIVE("p-format", p_format) {
	assert(argc == 1);
	EVAL(a, 0)
	return sexp_new_atom_str(sexp_format(a), at);
}

// S -> u64
DEFINE_PRIMITIVE("p-length", p_length) {
	assert(argc == 1);
	EVAL(list, 0)
	return sexp_new_atom_uint(sexp_num_children(list), at);
}

// (S, u64) -> S*
DEFINE_PRIMITIVE("p-nth", p_nth) {
	assert(argc == 2);
	EVAL(list, 0)
	EVAL(index, 0)
	return sexp_new_atom_ptr(&sexp_children(list)[sexp_read_u64(index)], at);
}

// (Sexp* size_t) -> List
DEFINE_PRIMITIVE("p-list", p_list) {
	assert(argc == 2);
	EVAL(start, 0)
	EVAL(len, 1)
	Sexp result = sexp_new_list(at);
	result.qword.children = sexp_read_ptr(start);
	result.word.num_children = sexp_read_u64(len);
	return result;
}

DEFINE_PRIMITIVE("p-replace", p_replace) {
	assert(argc == 3);
	EVAL(body, 0)
	EVAL(sym, 1)
	EVAL(replacement, 2)
	Sexp result = sexp_dup(body);
	return *replace_symbol(&result, sexp_read_str(sym), replacement);
}

// returns the Sexp node type
// S -> Bu
DEFINE_PRIMITIVE("p-typeof", p_typeof) {
	assert(argc == 1);
	EVAL(val, 0)
	if (val.is_list)
		return sexp_new_atom_uint(LIST_T, at);
	return sexp_new_atom_uint(sexp_atom_type(val), at);
}

// Sexp memory primitives =================================================
// S -> S*
DEFINE_PRIMITIVE("p-adrof", p_adrof) {
	assert(argc == 1);
	EVAL(val, 0)
	Sexp *result = calltree_alloc(at->parent, sizeof(Sexp));
	*result = val;
	return sexp_new_atom_ptr(result, at);
}
// S* -> S
// TODO: possibly remove this, replace with alloca+memcpy in ss
DEFINE_PRIMITIVE("p-deref", p_deref) {
	assert(argc == 1);
	EVAL(ptr, 0)
	return * (Sexp*) sexp_read_ptr(ptr);
}
// () -> size_t
DEFINE_PRIMITIVE("p-sexp-size", p_sexp_size) {
	assert(argc == 0);
	return sexp_new_atom_uint(sizeof(Sexp), at);;
}

// string API ================================================================
// an unstable lps is used as a string representation, this is its api
// (the other methods are reducible with memcpy etc.)

// str -> size_t
DEFINE_PRIMITIVE("p-strlen", p_strlen) {
	assert(argc == 1);
	EVAL(str, 0)
	size_t result_v = lps_len(sexp_read_str(str));
	return sexp_new_atom_uint(result_v, at);
}
// memcpy the bytes into the blank one from this
// this uses malloc, so the user needs to free afterwards
// size_t -> str
DEFINE_PRIMITIVE("p-uninit-str", p_uninit_str_from_len) {
	assert(argc == 1);
	EVAL(a, 0)
	lps result_v = lps_with_reserved_len(sexp_read_u64(a));
	Sexp result = sexp_new_atom_str(result_v, at);
	return result;
}
// str -> ()
DEFINE_PRIMITIVE("p-str-free", p_str_free) {
	assert(argc == 1);
	EVAL(a, 0)
	lps_free(sexp_read_str(a));
	return sexp_new_list(at);
}

// C-semantics primitives ===============================================

#define DEFINE_UNARY_FN(IGNORE, NAME, FN, IN_TYPE, READAS, OUT_ATOM) \
Sexp NAME(size_t argc, Sexp *argv, Interpreter *I, CallTree *at) { \
	assert(argc == 1); \
	EVAL(a, 0); \
	assert(sizeof(IN_TYPE) <= sizeof(void*)); \
	IN_TYPE a_v = sexp_read_##READAS(a); \
	Sexp result = sexp_new_atom_##OUT_ATOM(FN(a_v), at); \
	return result; \
}

#define DEFINE_TERNARY_FN(IGNORED, NAME, FN, IN1_TYPE, IN1_READAS, IN2_TYPE, IN2_READAS, IN3_TYPE, IN3_READAS, OUT_TYPE, OUT_ATOM) \
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

// void* -> ()
DEFINE_PRIMITIVE("p-free", p_free) {
	assert(argc == 1);
	EVAL(a, 0)
	free(sexp_read_ptr(a));
	Sexp result = sexp_new_list(at);
	return result;
}

// (void* size_t) -> void*
DEFINE_PRIMITIVE("p-realloc", p_realloc) {
	assert(argc == 2);
	EVAL(a, 0)
	EVAL(b, 1)
	void* a_v = sexp_read_ptr(a);
	size_t b_v = sexp_read_u64(b);
	void* result_v = realloc(a_v, b_v);
	return sexp_new_atom_ptr(result_v, at);
}

// Bu -> void*
DEFINE_PRIMITIVE("p-alloca", p_alloca) {
	assert(argc == 1);
	EVAL(a, 0)
	void *result_v = calltree_alloc(parent_ctx(at), sexp_read_u64(a));
	return sexp_new_atom_ptr(result_v, at);
}

// Bu -> void*
DEFINE_UNARY_FN("p-malloc", p_malloc, malloc, size_t, u64, ptr)
// (void* void* Bu) -> Bint
DEFINE_TERNARY_FN("p-memcmp", p_memcmp, memcmp, void*, ptr, void*, ptr, size_t, u64, int, int)
// (void* void* Bu) -> void*
DEFINE_TERNARY_FN("p-memcpy", p_memcpy, memcpy, void*, ptr, void*, ptr, size_t, u64, void*, ptr)
// (void* Bint Bu) -> void*
DEFINE_TERNARY_FN("p-memset", p_memset, memset, void*, ptr, int, s64, size_t, u64, void*, ptr)

// returns the taken branch
// third arg is optional, () is used if it is absent
// (Bint S S) -> S
DEFINE_PRIMITIVE("p-if", p_if) {
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
DEFINE_PRIMITIVE("p-while", p_while) {
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
DEFINE_PRIMITIVE("p-and", p_and) {
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
DEFINE_PRIMITIVE("p-or", p_or) {
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

#define DEFINE_BINARY_OP(IGNORED, NAME, OP, IN_TYPE, READAS, OUT_TYPE, OUT_ATOM) \
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

#define DEFINE_UNARY_OP(IGNORED, NAME, OP, IN_TYPE, READAS, OUT_TYPE, OUT_ATOM) \
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

// there are only sizeof(void*) arithmetic operations in the runtime
// you could of course make a u8 type with memset
// but when you use these ops they'll be partially zeroed u64s

// Bint -> Bint
DEFINE_UNARY_OP("p-not", p_not, !, int, s64, int, int)
// (B B) -> Bint
DEFINE_BINARY_OP("p-biteq", p_biteq, ==, uintptr_t, u64, int, int) // technically UB union usage
DEFINE_BINARY_OP("p-bitneq", p_bitneq, !=, uintptr_t, u64, int, int)
// (B B) -> Bu
DEFINE_BINARY_OP("p-bitand", p_bitand, &, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP("p-bitor", p_bitor, |, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP("p-bitxor", p_bitxor, ^, uintptr_t, u64, uintptr_t, uint)
// p_compl for complement?
DEFINE_UNARY_OP("p-bitnot", p_bitnot, ~, uintptr_t, u64, uintptr_t, uint)
// uintptr_t
// (Bu Bu) -> Bu
DEFINE_BINARY_OP("p-uadd", p_uadd, +, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP("p-usub", p_usub, -, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP("p-umul", p_umul, *, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP("p-udiv", p_udiv, /, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP("p-umod", p_umod, &, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP("p-ushl", p_ushl, <<, uintptr_t, u64, uintptr_t, uint)
DEFINE_BINARY_OP("p-ushr", p_ushr, >>, uintptr_t, u64, uintptr_t, uint)
// (Bu Bu) -> Bs
DEFINE_BINARY_OP("p-ult", p_ult, <, uintptr_t, u64, int, int)
DEFINE_BINARY_OP("p-ulte", p_ulte, <=, uintptr_t, u64, int, int)
DEFINE_BINARY_OP("p-ugt", p_ugt, >, uintptr_t, u64, int, int)
DEFINE_BINARY_OP("p-ugte", p_ugte, >=, uintptr_t, u64, int, int)
// intptr_t
// (Bs Bs) -> Bs
DEFINE_BINARY_OP("p-sadd", p_sadd, +, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP("p-ssub", p_ssub, -, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP("p-smul", p_smul, *, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP("p-sdiv", p_sdiv, /, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP("p-smod", p_smod, &, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP("p-sshl", p_sshl, <<, intptr_t, s64, intptr_t, int)
DEFINE_BINARY_OP("p-sshr", p_sshr, >>, intptr_t, s64, intptr_t, int)
// (Bs Bs) -> Bs
DEFINE_BINARY_OP("p-slt", p_slt, <, intptr_t, s64, int, int)
DEFINE_BINARY_OP("p-slte", p_slte, <=, intptr_t, s64, int, int)
DEFINE_BINARY_OP("p-sgt", p_sgt, >, intptr_t, s64, int, int)
DEFINE_BINARY_OP("p-sgte", p_sgte, >=, intptr_t, s64, int, int)
// double
// (Bd Bd) -> Bd
DEFINE_BINARY_OP("p-fadd", p_fadd, +, double, f64, double, float)
DEFINE_BINARY_OP("p-fsub", p_fsub, -, double, f64, double, float)
DEFINE_BINARY_OP("p-fmul", p_fmul, *, double, f64, double, float)
DEFINE_BINARY_OP("p-fdiv", p_fdiv, /, double, f64, double, float)
// (Bd Bd) -> Bs
DEFINE_BINARY_OP("p-flt", p_flt, <, double, f64, int, int)
DEFINE_BINARY_OP("p-flte", p_flte, <=, double, f64, int, int)
DEFINE_BINARY_OP("p-fgt", p_fgt, >, double, f64, int, int)
DEFINE_BINARY_OP("p-fgte", p_fgte, >=, double, f64, int, int)

