// the primitives that are built into the interpreter
// the build process parses this file to generate the default primitive initialisation
// so extending the language by appending something here is trivial

#include "allocators/std-alloc.h"
#include "include/nob.h"
#include "lps.h"
#include "primitives.h"
#include "interpreter.h"
#include "sexp.h"
#include "reader.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stddefer.h>

#ifdef DEBUG
#define LOG(X) X
#else
#define LOG(X)
#endif

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
	nob_da_free(to_check);
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

// returns the source-level parent call, skipping intermediate calls
static CallTree *parent_ctx(CallTree *at) {
	CallTree *parent = at->parent;
	Sexp called_by;
try_parent:
		// () for root-level call
		called_by = sexp_num_children(*parent->state.sexp_called) > 0
			? sexp_children(*parent->state.sexp_called)[0]
			: sexp_new_list(at);
		while (!called_by.is_list && sexp_atom_type(called_by) == A_SYM && lps_cmp(sexp_read_str(called_by), lps_from_cstr("eval", std_allocator())) == 0) {
			parent = parent->parent;
			goto try_parent;
		}
		parent = parent->parent;
	return parent;
}

// execution fundamentals =============================================================

LOG(uint call_depth = 0;)
// the primitive form of eval
// S -> S
DEFINE_PRIMITIVE("p-exec", p_exec) {
	assert(argc == 1);
	LOG(++call_depth;)
	LOG(fprintf(stderr, "%*s", call_depth, " ");)
	LOG(fprintf(stderr, "Called exec on "LPS_Fmt"\n", LPS_Arg(sexp_format(argv[0])));)
	// atom: fetch symbol, do nothing for literals
	if (!argv[0].is_list) {
		switch (sexp_atom_type(argv[0])) {
		case A_SYM: {
			if (primitives_contains_macro(I->prims, sexp_read_str(argv[0]))) {
				Sexp rval = argv[0];
				LOG(fprintf(stderr, "%*s", call_depth, " ");)
				LOG(--call_depth;)
				LOG(fprintf(stderr, "Returned "LPS_Fmt"\n", LPS_Arg(sexp_format(rval)));)
				return argv[0];
			}
			Sexp *sym_ref = calltree_get_symbol_val_ref_walk_up(at, sexp_read_str(argv[0]));
			if (!sym_ref) {
				fprintf(stderr, "p-exec: could not find symbol |"LPS_Fmt"|\n", LPS_Arg(sexp_read_str(argv[0])));
				abort();
			}
			Sexp sym_val = *sym_ref;
			LOG(fprintf(stderr, "%*s", call_depth, " ");)
			LOG(--call_depth;)
			LOG(fprintf(stderr, "Returned "LPS_Fmt"\n", LPS_Arg(sexp_format(sym_val)));)
			return sym_val;
		} break;
		default: {
			Sexp rval = argv[0];
			LOG(fprintf(stderr, "%*s", call_depth, " ");)
			LOG(--call_depth;)
			LOG(fprintf(stderr, "Returned "LPS_Fmt"\n", LPS_Arg(sexp_format(rval)));)
			return argv[0];
		} abort();
		}
	}
	Sexp *children = sexp_children(argv[0]);
	size_t num_children = sexp_num_children(argv[0]);
	// (p-exec ()) returns ()
	LOG(if (num_children == 0) {
		LOG(fprintf(stderr, "%*s", call_depth, " ");)
		LOG(--call_depth;)
		fprintf(stderr, "Returned "LPS_Fmt"\n", LPS_Arg(sexp_format(argv[0])));
	})
	if (num_children == 0)
		return argv[0];
	// TODO: macro literal support (eval this rather than look in symboltable)
	lps macro_str = sexp_read_str(children[0]);
	size_t child_argc = num_children-1;
	Sexp *child_argv = child_argc > 0 ? &children[1] : NULL;
	if (primitives_contains_macro(I->prims, macro_str)) {
		Sexp *to_eval = calltree_alloc(at, sizeof(Sexp));
		// TODO: use arena allocation here
		*to_eval = sexp_dup(argv[0], std_allocator());
		CallTree *call = calltree_child_at(at, to_eval, child_argc, 0, &I->arenapool);
		Sexp result = sexp_dup(primitives_get_macro
		                        (I->prims, macro_str)
		                        (child_argc, child_argv, I, call),
		                       std_allocator());
		calltree_destroy(*call, &I->arenapool);
		LOG(fprintf(stderr, "%*s", call_depth, " ");)
		LOG(--call_depth;)
		LOG(fprintf(stderr, "Returned "LPS_Fmt"\n", LPS_Arg(sexp_format(result)));)
		return result;
	} else {
		// (() name) defines a macro in the symboltable
		Sexp *st_entry = calltree_get_symbol_val_ref_walk_up(at, macro_str);
		if (!st_entry) {
			fprintf(stderr, "p-exec: could not find symbol "LPS_Fmt"\n", LPS_Arg(macro_str));
			abort();
		}
		LOG(fprintf(stderr, "%*s", call_depth, " ");)
		LOG(fprintf(stderr, "macro symbol value: "LPS_Fmt"\n", LPS_Arg(sexp_format(*st_entry)));)
		// primitive macros as symbols evaluate to themselves, if something returned one from the symboltable
		// that is symbol is an alias of a macro, be it a prim symbol or a list
		if (!st_entry->is_list && sexp_atom_type(*st_entry) == A_SYM) {
			if (primitives_contains_macro(I->prims, sexp_read_str(*st_entry))) {
				Sexp *to_eval = calltree_alloc(at, sizeof(Sexp));
				*to_eval = sexp_dup(argv[0], std_allocator());
				CallTree *call = calltree_child_at(at, to_eval, child_argc, 0, &I->arenapool);
				Sexp result = sexp_dup(primitives_get_macro
				                        (I->prims, sexp_read_str(*st_entry))
		                                (child_argc, child_argv, I, call),
				                       std_allocator());
				calltree_destroy(*call, &I->arenapool);
				LOG(fprintf(stderr, "%*s", call_depth, " ");)
				LOG(--call_depth;)
				LOG(fprintf(stderr, "Returned "LPS_Fmt"\n", LPS_Arg(sexp_format(result)));)
				return result;
			} else {
				abort();
			}
		}
		Sexp *st_children = sexp_children(*st_entry);
		assert(sexp_num_children(*st_entry) == 2);
		Sexp st_val = st_children[1];
		assert(sexp_num_children(st_val) >= 2);
		assert(lps_cmp(sexp_read_str(sexp_children(st_val)[0]),
		               lps_from_cstr("p-macrobody", std_allocator())
					  ) == 0);
		Sexp arg_binds = sexp_children(st_val)[1];
		assert(arg_binds.is_list);
		assert(sexp_num_children(arg_binds) == 2);
		Sexp *alloced_macro = calltree_alloc(at, sizeof(Sexp));
		*alloced_macro = sexp_dup(st_val, std_allocator());
		CallTree *child_call = calltree_child_at(at, alloced_macro, child_argc, 0, &I->arenapool);
		Sexp argc_node = sexp_new_atom_uint(child_argc, child_call);
		lps argc_bind_to = sexp_read_str(sexp_children(arg_binds)[0]);
		Sexp argv_node = sexp_new_atom_ptr(child_argv, child_call);
		lps argv_bind_to = sexp_read_str(sexp_children(arg_binds)[1]);
		replace_symbol(replace_symbol(alloced_macro, argc_bind_to, argc_node), argv_bind_to, argv_node);
		if (!sexp_is_null(at->state.call_results[0])) {
			LOG(fprintf(stderr, "%*s", call_depth, " ");)
			LOG(--call_depth;)
			LOG(fprintf(stderr, "Returned "LPS_Fmt"\n", LPS_Arg(sexp_format(at->state.call_results[0])));)
			return at->state.call_results[0];
		} else {
			Sexp result = sexp_dup(p_exec(1, alloced_macro, I, child_call), std_allocator());
			calltree_destroy(*child_call, &I->arenapool);
			LOG(fprintf(stderr, "%*s", call_depth, " ");)
			LOG(--call_depth;)
			LOG(fprintf(stderr, "Returned "LPS_Fmt"\n", LPS_Arg(sexp_format(result)));)
			return result;
		}
	}
}

// takes the environment as a parameter
// first evals arg 0 in this environment, then the target one
// third arg is unused, is used to store result
// (S, CallTree*, ()) -> S
DEFINE_PRIMITIVE("p-eval-at", p_eval_at) {
	assert(argc == 2);
	EVAL(to_eval, 0)
	EVAL(env, 1)
	// EVAL(result, 2) unwrapped and tweaked
	Sexp result;
	if (!sexp_is_null(at->state.call_results[2])) {
		result = at->state.call_results[2];
	} else {
		CallTree *child = calltree_child_at(sexp_read_ptr(env), &to_eval, 1, 2, &I->arenapool);
		result = eval(to_eval, I, child);
		calltree_destroy(*child, &I->arenapool);
	};
	return result;
}

// calls eval with its args as two sexps corresponding to ptr and length of eval args
// (fn size_t Sexp*) -> S
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
DEFINE_PRIMITIVE("p-macrobody", p_macrobody) {
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

// TODO take in file arg
// // (str, FILE*) -> ()
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

// (CallTree* sym S) -> ()
DEFINE_PRIMITIVE("p-st-set", p_symboltable_set) {
	assert(argc == 3);
	EVAL(call, 0)
	EVAL(sym, 1)
	EVAL(val, 2)
	calltree_set_symbol_val(sexp_read_ptr(call), sexp_read_str(sym), val);
	return sexp_new_list(at);
}
// (CallTree* sym) -> ()
DEFINE_PRIMITIVE("p-st-remove", p_symboltable_remove) {
	assert(argc == 2);
	EVAL(call, 0)
	EVAL(sym, 1)
	calltree_remove_symbol_val(sexp_read_ptr(call), sexp_read_str(sym));
	return sexp_new_list(at);
}
// (CallTree* sym) -> S*
DEFINE_PRIMITIVE("p-st-get", p_symboltable_get) {
	assert(argc == 2);
	EVAL(call, 0)
	EVAL(sym, 1)
	Sexp *adr = calltree_get_symbol_val_ref_no_walk(sexp_read_ptr(call), sexp_read_str(sym));
	return sexp_new_atom_ptr(adr, at);
}

// returns a ptr to the env it was called from
// () -> void*
DEFINE_PRIMITIVE("p-scope", p_path_at) {
	assert(argc == 0);
	return sexp_new_atom_ptr(parent_ctx(at), at);
}

// TODO: walk until macrobody?
// second arg is for levels up to go
// (CallTree* uint) -> CallTree*
DEFINE_PRIMITIVE("p-parent-scope", p_path_of_parent) {
	assert(argc == 2);
	EVAL(scope, 0)
	EVAL(layers, 1)
	CallTree *parent = sexp_read_ptr(scope);
	for (int i = 0; i < sexp_read_u64(layers); ++i)
		parent = parent_ctx(parent);
	return sexp_new_atom_ptr(parent, at);
}

// () -> CallTree*
DEFINE_PRIMITIVE("p-root-scope", p_root) {
	assert(argc == 0);
	return sexp_new_atom_ptr(I->call_root, at);
}

// str -> &mut S
DEFINE_PRIMITIVE("p-get", p_get) {
	assert(argc == 1);
	EVAL(val, 0)
	// TODO: call eval-symbol on val
	return sexp_new_atom_ptr(calltree_get_symbol_val_ref_walk_up(at, sexp_read_str(val)), at);
}

// S -> ()
DEFINE_PRIMITIVE("p-sendmsg", p_sendmsg) {
	assert(argc == 1);
	Sexp *out = std_allocator().alloc(NULL, sizeof(Sexp), alignof(Sexp));
	*out = sexp_dup(argv[0], std_allocator());
	slave_send_msg(I->parent_channel, out);
	return sexp_new_list(at);
}
// () -> S
DEFINE_PRIMITIVE("p-awaitmsg", p_awaitmsg) {
	assert(argc == 0);
	Sexp *in = slave_await_msg(I->parent_channel);
	Sexp msg = *in;
	free(in);
	return msg;
}

// Sexp API ===============================================

// converts a sexp into a printable string (maybe should not actually be primitive)
// S -> str
DEFINE_PRIMITIVE("p-format", p_format) {
	assert(argc == 1);
	EVAL(a, 0)
	// TODO: calltree-local allocator
	return sexp_new_atom_str(sexp_format(a, std_allocator()), at);
}

// returns length of a list
// list -> u64
DEFINE_PRIMITIVE("p-length", p_length) {
	assert(argc == 1);
	EVAL(list, 0)
	return sexp_new_atom_uint(sexp_num_children(list), at);
}

// returns a pointer to the nth element of a list
// (S, u64) -> S*
DEFINE_PRIMITIVE("p-nth", p_nth) {
	assert(argc == 2);
	EVAL(list, 0)
	EVAL(index, 1)
	return sexp_new_atom_ptr(&sexp_children(list)[sexp_read_u64(index)], at);
}

// creates a list from an array of sexps and its length
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

// replaces symbols recursively found in a sexp with an evaluated form
// (S, symbol, S) -> S
DEFINE_PRIMITIVE("p-replace", p_replace) {
	assert(argc == 3);
	EVAL(body, 0)
	EVAL(sym, 1)
	EVAL(replacement, 2)
	// TODO: calltree-local allocator
	Sexp result = sexp_dup(body, std_allocator());
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

// given a sexp, returns a pointer to it
// S -> S*
DEFINE_PRIMITIVE("p-adrof", p_adrof) {
	assert(argc == 1);
	EVAL(val, 0)
	Sexp *result = calltree_alloc(at->parent, sizeof(Sexp));
	*result = val;
	return sexp_new_atom_ptr(result, at);
}

// given a pointer to a sexp, returns the sexp at the address
// S* -> S
// TODO: possibly remove this, replace with alloca+memcpy in ss
DEFINE_PRIMITIVE("p-deref", p_deref) {
	assert(argc == 1);
	EVAL(ptr, 0)
	return * (Sexp*) sexp_read_ptr(ptr);
}

// returns the size of a sexp in memory, in bytes
// needed because I may use a NaN-packed implementation
// () -> size_t
DEFINE_PRIMITIVE("p-sexp-size", p_sexp_size) {
	assert(argc == 0);
	return sexp_new_atom_uint(sizeof(Sexp), at);;
}

// string API ================================================================
// an unstable lps is used as a string representation, this is its api
// (the other methods are reducible with memcpy etc.)

// given a str, returns the length of its payload
// str -> size_t
DEFINE_PRIMITIVE("p-strlen", p_strlen) {
	assert(argc == 1);
	EVAL(str, 0)
	size_t result_v = lps_len(sexp_read_str(str));
	return sexp_new_atom_uint(result_v, at);
}

// given a length, create a string of its length, with an uninitialised payload
// memcpy the bytes into the blank one from this
// this uses malloc, so the user needs to free afterwards
// TODO: custom allocator
// size_t -> str
DEFINE_PRIMITIVE("p-uninit-str", p_uninit_str_from_len) {
	assert(argc == 1);
	EVAL(a, 0)
	lps result_v = lps_with_reserved_len(sexp_read_u64(a), std_allocator());
	Sexp result = sexp_new_atom_str(result_v, at);
	return result;
}

// frees a length-prefixed-string
// str -> ()
DEFINE_PRIMITIVE("p-str-free", p_str_free) {
	assert(argc == 1);
	EVAL(a, 0)
	lps_free(sexp_read_str(a), std_allocator());
	return sexp_new_list(at);
}

// IO / C-semantics primitives ===============================================

// reads a sexp from the interpreter's in-stream
// () -> S
DEFINE_PRIMITIVE("p-read", p_read) {
	assert(argc == 0);
	Sexp s = reads(I, true);
	if (sexp_is_null(s)) {
		// TODO: message passing
		if (I->error_flags != ERR_NONE) {
			abort();
		}
	}
	return s;
}

// executes all of the sexps in a file as if at the current location
// str -> S
DEFINE_PRIMITIVE("p-load", p_load) {
	EVAL(path, 0)
	char* path_cstr = lps_to_cstr(sexp_read_str(path), std_allocator());
	defer { std_allocator().free(std_allocator().ctx, path_cstr, sizeof(path_cstr)); }
	FILE* src = fopen(path_cstr, "r");
	assert(src);
	defer { fclose(src); }
	// set interpreter's input to the opened file
	FILE* prev_src = I->in_stream;
	I->in_stream = src;
	defer { I->in_stream = prev_src; }
	Sexp result;
	while (1) {
		Sexp cur = reads(I, true);
		if (sexp_is_null(cur)) {
			if (I->error_flags != ERR_NONE)
				abort();
			break;
		}
		result = eval(cur, I, parent_ctx(at));
	}
	return result;
}

// returns 1 if p-read returned null, 0 otherwise
// S -> Bint
DEFINE_PRIMITIVE("p-is-eof", p_is_eof) {
	assert(argc == 1);
	EVAL(a, 0)
	return sexp_new_atom_int(sexp_is_null(a), at);
}

extern char **environ;

// returns a pointer to the environment variables
// cstr, needs conversion
// () -> char**
DEFINE_PRIMITIVE("p-environ", p_getenv) {
	// for (char **envar = environ; *envar != NULL; envar++) {
        // printf("%s\n", *envar);
    // }
	assert(argc == 0);
	return sexp_new_atom_ptr(environ, at);
}

// returns the interpreter's current source
// TODO: wrapper obj for distributed-agnosticism () -> chariter*
// () -> FILE*
DEFINE_PRIMITIVE("p-get-in", p_get_in) {
	assert(argc == 0);
	return sexp_new_atom_ptr(I->in_stream, at);
}

// sets the interpreter's current source, returning the previous one
// returns previous in-iterator
// FILE* -> FILE*
DEFINE_PRIMITIVE("p-set-in", p_set_in) {
	assert(argc == 1);
	EVAL(new, 0)
	void* old = I->in_stream;
	I->in_stream = sexp_read_ptr(new);
	return sexp_new_atom_ptr(old, at);
}

// returns the runtime's stdin
// () -> FILE*
DEFINE_PRIMITIVE("p-stdin", p_stdin) {
	assert(argc == 0);
	return sexp_new_atom_ptr(stdin, at);
}

// returns the runtime's stdout
// () -> FILE*
DEFINE_PRIMITIVE("p-stdout", p_stdout) {
	assert(argc == 0);
	return sexp_new_atom_ptr(stdout, at);
}

// returns the runtime's stderr
// () -> FILE*
DEFINE_PRIMITIVE("p-stderr", p_stderr) {
	assert(argc == 0);
	return sexp_new_atom_ptr(stderr, at);
}

// C binding
// TODO: ffi
// (str, str) -> FILE*
DEFINE_PRIMITIVE("p-fopen", p_fopen) {
	assert(argc == 2);
	EVAL(filename, 0)
	EVAL(mode, 1)
	char *a_cstr = lps_to_cstr(sexp_read_str(filename), std_allocator());
	defer { free(a_cstr); }
	char *b_cstr = lps_to_cstr(sexp_read_str(mode), std_allocator());
	defer { free(b_cstr); }
	Sexp result = sexp_new_atom_ptr(fopen(a_cstr, b_cstr), at);
	return result;
}

// C binding
// FILE* -> ()
DEFINE_PRIMITIVE("p-fclose", p_fclose) {
	assert(argc == 1);
	EVAL(a, 0)
	fclose(sexp_read_ptr(a));
	return sexp_new_list(at);
}

// arena allocation with the lifetime of the provided reference
// (CallTree*, Bu) -> void*
DEFINE_PRIMITIVE("p-ct-alloc", p_ct_alloc) {
	assert(argc == 2);
	EVAL(a, 0)
	EVAL(b, 1)
	void *result_v = calltree_alloc(sexp_read_ptr(a), sexp_read_u64(b));
	return sexp_new_atom_ptr(result_v, at);
}

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
// the macro is for atom return, but this is () return
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
DEFINE_UNARY_FN("p-malloc", p_malloc, malloc, size_t, u64, ptr)
// (void* void* Bu) -> Bint
DEFINE_TERNARY_FN("p-memcmp", p_memcmp, memcmp, void*, ptr, void*, ptr, size_t, u64, int, int)
// (void* void* Bu) -> void*
DEFINE_TERNARY_FN("p-memcpy", p_memcpy, memcpy, void*, ptr, void*, ptr, size_t, u64, void*, ptr)
// (void* Bint Bu) -> void*
DEFINE_TERNARY_FN("p-memset", p_memset, memset, void*, ptr, int, s64, size_t, u64, void*, ptr)

// executes the taken branch, ignoring the other
// third arg is optional, () is used if it is absent
// (Bint S S?) -> S
DEFINE_PRIMITIVE("p-if", p_if) {
	assert(argc == 2 || argc == 3);
	EVAL(cond, 0)
	int cond_v = sexp_read_s64(cond);
	Sexp t_branch = argv[1];
	Sexp f_branch = argc == 3 ? argv[2]
	                          : sexp_new_list(at);
	Sexp branch;
	if (cond_v)
		branch = t_branch;
	else
		branch = f_branch;
	CallTree *child = calltree_child_at(at, &argv[0], 1, 1, &I->arenapool);
	Sexp result = p_exec(1, &branch, I, child);
	calltree_destroy(*child, &I->arenapool);
	// TODO: calltree-local allocator
	return sexp_dup(result, std_allocator());
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
		// wipe body state for the iteration
		memset(
		        at->state.call_results+sizeof(Sexp),
		        0,
		        (at->state.num_call_results-1) * sizeof(Sexp)
		      );
		for (size_t i = 1; i < argc; ++i) {
			if (i+1 < argc) {
				EVAL(ignored, i)
			} else {
				EVAL(val, i)
				result = val;
			}
		}
		// wipe cond state for the iteration
		at->state.call_results[0] = sexp_null();
	}
	// TODO: calltree-local allocator
	return sexp_dup(result, std_allocator());
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
DEFINE_BINARY_OP("p-biteq", p_biteq, ==, uintptr_t, u64, int, int)
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

