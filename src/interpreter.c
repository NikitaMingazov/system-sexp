
#include "allocators/std-alloc.h"
#include "include/arena.h"

#include "interpreter.h"
#include "arenapool.h"
#include "lps.h"
#include "master_slave_channel.h"
#include "primitives.h"
#include "reader.h"
#include "readtable.h"
#include "sexp.h"
#include "symboltable.h"
#include "readtable.h"
#include "primitives_t.h"
#include <pthread.h>
#include <stddefer.h>
#include <threads.h>
#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// define static lps strings (special symbols)
// assign to this to break dynamic scoping by designating a parent node to look for symbols in
static const char lps_env_body[] = { 3, 'e', 'n', 'v' };
static const lps LPS_ENV = (lps) (lps_env_body+1);
// the top-level execution function (wraps top-level expressions)
static const char lps_eval_body[] = { 4, 'e', 'v', 'a', 'l' };
static const lps LPS_EVAL = (lps) (lps_eval_body+1);

#define LOG(X)

StackState stackstate_new(Sexp *to_eval, size_t num_eval_tmps, ArenaPool *pool) {
	StackState ss = (StackState) {
		.memory = arena_pool_get(pool),
		.sexp_called = to_eval,
		.symboltable = symboltable_new(&ss.memory),
		.num_call_results = num_eval_tmps,
	};
	ss.call_results = arena_alloc(&ss.memory, num_eval_tmps * sizeof(Sexp));
	memset(ss.call_results, 0, num_eval_tmps * sizeof(Sexp));
	return ss;
}

void stackstate_destroy(StackState ss, ArenaPool *pool) {
	symboltable_free(ss.symboltable);
	arena_pool_return(pool, ss.memory);
}

CallTree calltree_new(CallTree *parent, Sexp *to_eval, size_t num_eval_tmps, uint call_idx, ArenaPool *pool) {
	CallTree ct = (CallTree) {
		.parent = parent,
		.state = stackstate_new(to_eval, num_eval_tmps, pool),
		.origin_idx = call_idx,
	};
	return ct;
}

// produces a call to an implicit progn at a call inst, the sexp may or may not
// actually be at the call location, but its result will be stored in its entry on unwind
// used for interpreted derivative functions
CallTree *calltree_child_at(CallTree *parent, Sexp *to_eval, size_t num_eval_tmps, size_t branch_idx, ArenaPool *pool) {
	CallTree *new = calltree_alloc(parent, sizeof(CallTree));
	*new = calltree_new(parent, to_eval, num_eval_tmps, branch_idx, pool);
	return new;
}

// produces a child call into the inside of the callinst
// CallInst calltree_branch(CallInst call) {
// }

void calltree_destroy(CallTree ct, ArenaPool *pool) {
	stackstate_destroy(ct.state, pool);
}

void interpreter_set_stream(Interpreter *I, FILE *in_stream) {
	I->in_stream = in_stream;
}
// void interpreter_set_root_symboltable(Interpreter *I, Symboltable *st);

int calltree_set_symbol_val(CallTree *at, const lps sym, Sexp val) {
	// if (calltree_get_symbol_val_ref(at, sym))
	// 	LOG(fprintf(stderr, "shadowed "LPS_Fmt"\n", LPS_Arg(sym)));
	// parent's lifetime exceeds child
	if (at->parent)
		return symboltable_set(at->state.symboltable, sym, val, &at->parent->state.memory);
	else // root has no parent but is permanent
		return symboltable_set(at->state.symboltable, sym, val, &at->state.memory);
}
Sexp calltree_remove_symbol_val(CallTree *at, const lps sym) {
	CallTree *cursor = at;
	while (cursor && !symboltable_get_ref(at->state.symboltable, sym)) {
		cursor = cursor->parent;
	}
	return symboltable_remove(at->state.symboltable, sym);
}
Sexp *calltree_get_symbol_val_ref_walk_up(CallTree *at, const lps sym) {
	CallTree *cursor = at;
	while (cursor && !symboltable_get_ref(cursor->state.symboltable, sym)) {
		// to implement lexical scoping, asign env to a CallTree* other than the call parent to
		// search for symbol values in
		Sexp *env_ref = symboltable_get_ref(cursor->state.symboltable, LPS_ENV);
		if (env_ref)
			cursor = sexp_read_ptr(*env_ref);
		else
			cursor = cursor->parent;
		if (!cursor)
			return NULL;
	}
	return symboltable_get_ref(cursor->state.symboltable, sym);
}
Sexp *calltree_get_symbol_val_ref_no_walk(CallTree *at, const lps sym) {
	return symboltable_get_ref(at->state.symboltable, sym);
}
void *calltree_alloc(CallTree *at, size_t size) {
	return arena_alloc(&at->state.memory, size);
}

Interpreter *interpreter_new(const Primitives *prims, uint num_threads) {
	Interpreter *new = malloc(sizeof(*new));
	*new = (Interpreter) {
		.prims = prims,
		.error_flags = ERR_NONE,
		.in_stream = NULL,
		.rt = readtable_new(),
		.sexp_root = sexp_new_source_list(0, 0),
		.tail_row = 1,
		.tail_col = 0, // increment happens before read
		.num_dispatchers = num_threads,
		.arenapool = arena_pool_new(),
	};
	primitives_export_readers(prims, new->rt);
	new->dispatcher_queues = calloc(num_threads, sizeof(ChannelPair*));
	for (size_t i = 0; i < num_threads; ++i)
		new->dispatcher_queues[i] = channelpair_new_one_to_one();
	new->call_root = malloc(sizeof(CallTree));
	new->call_root->parent = NULL;
	new->call_root->state = stackstate_new(&new->sexp_root, 0, &new->arenapool);
	new->call_root->origin_idx = -1; // underflow as a flag
	// initialise eval to the primitive's default
	Arena *root_arena = &new->call_root->state.memory;
	Sexp default_eval = sexp_new_source_atom(prims->default_eval_binding, 0, 0);
	calltree_set_symbol_val(new->call_root, lps_from_cstr("eval", std_allocator()), default_eval);
	return new;
}

void interpreter_free(Interpreter *I) {
	calltree_destroy(*I->call_root, &I->arenapool);
	free(I->call_root);
	free(I->dispatcher_queues);
	arena_pool_destroy(&I->arenapool);
	readtable_free(I->rt);
	free(I);
}

void interpreter_push_call(Interpreter *I, CallTree *job, int dispatcher) {
	Sexp *msg = malloc(sizeof(Sexp));
	*msg = sexp_new_source_atom(NULL, 0, 0);
	msg->word.atom_type = A_PTR;
	msg->qword.atom.as_ptr = job;
	master_send_msg(I->dispatcher_queues[dispatcher], msg);
}

// wrapper around whatever (eval S) is to enable interrupt and continue
// wraps a sexp in a calltree node and calls p-exec on (eval S)
Sexp eval(Sexp to_eval, Interpreter *I, CallTree *at) {
		LOG(fprintf(stderr, "Called eval on "LPS_Fmt"\n", LPS_Arg(sexp_format(to_eval)));)
		LOG(fprintf(stderr, "   at "); print_trace(at);)
	Sexp *with_eval = calltree_alloc(at, sizeof(Sexp));
	*with_eval = sexp_new_list(at);
	sexp_list_append(with_eval, sexp_new_atom_sym(LPS_EVAL, at), &at->state.memory);
	sexp_list_append(with_eval, to_eval, &at->state.memory);
	// 1 is to eval the macro into
	CallTree *child_call = calltree_child_at(at, with_eval, 1, 0, &I->arenapool);
	// p_exec can longjmp
	Sexp result = p_exec(1, with_eval, I, child_call);
		LOG(fprintf(stderr, "  Eval computed: "LPS_Fmt"\n", LPS_Arg(sexp_format(result)));)
	// longjmp moves the call to dispatcher_loop
	// if a cached value wasn't used and eval was not longjmped out, this owns the call
		LOG(fprintf(stderr, "Destroyed call: ");)
		LOG(print_trace(child_call);)
	calltree_destroy(*child_call, &I->arenapool);
		LOG(fprintf(stderr, "  Eval computed: "LPS_Fmt"\n", LPS_Arg(sexp_format(result)));)
		LOG(fprintf(stderr, "   at "); print_trace(at);)
	return result;
}

Sexp *calltree_sexp_at(CallTree *at) {
	return at->state.sexp_called;
}
u16 calltree_row_at(CallTree *at) {
	return calltree_sexp_at(at)->row;
}
u16 calltree_col_at(CallTree *at) {
	return calltree_sexp_at(at)->col;
}

void print_trace(CallTree *call) {
	CallTree *parent = call->parent;
	size_t call_count = 0;
	// size_t call_count = 1; // to include the root as '0'
	while (parent) {
		++call_count;
		parent = parent->parent;
	}
		fprintf(stderr, "[depth=%zu,idx=%d]\n", call_count, call->origin_idx);
		return;
	if (!call->parent) {
		fprintf(stderr, "[%d]\n", call->origin_idx);
		return;
	}
	uint *idxs = malloc(sizeof(*idxs) * call_count);
	defer free(idxs);
	idxs[call_count-1] = call->origin_idx;
	parent = call->parent;
	for (size_t i = 1; i < call_count; ++i) {
		idxs[call_count-1-i] = parent->origin_idx;
		parent = parent->parent;
	}
	fprintf(stderr, "[");
	fprintf(stderr, "%d", idxs[0]);
	for (size_t i = 1; i < call_count; ++i) {
		fprintf(stderr, ", %d", idxs[i]);
	}
	fprintf(stderr, "]\n");
}


struct dispatcher_config {
	size_t dispatcher_id;
	Interpreter *origin;
};
thread_local jmp_buf unwind_point;

void *dispatcher_loop(void *cfg_ptr) {
	struct dispatcher_config *cfg = cfg_ptr;
	Interpreter *I = cfg->origin;
	size_t dispatcher_id = cfg->dispatcher_id;
	ChannelPair *job_queue = I->dispatcher_queues[cfg->dispatcher_id];
	free(cfg_ptr);
next_job:
	while (1) {
		CallTree *job;
		// the zeroth dispatcher reads in sexps while not busy
		if (dispatcher_id == 0) {
			Sexp *job_msg = slave_receive_msg_if_exists(job_queue);
			if (!job_msg) {
				Sexp next = reads(I, true);
				// terminate on EOF/lexical error  TODO: lexical error handling
				if (sexp_is_null(next)) {
					Sexp *nil = malloc(sizeof(Sexp));
					*nil = sexp_new_source_list(0, 0);
					slave_send_msg(I->parent_channel, nil);
					// todo: queue a EOF () into each queue instead?
					return NULL;
				}
				// append new sexp to root
				sexp_list_append(&I->sexp_root, next, &I->call_root->state.memory);
				Sexp *next_alloced = calltree_alloc(I->call_root, sizeof(Sexp));
				*next_alloced = next;
				job = calltree_child_at(I->call_root, next_alloced, 1, sexp_num_children(I->sexp_root)-1, &I->arenapool);
			} else {
				job = (CallTree*)sexp_read_ptr(*job_msg);
				free(job_msg);
			}
		} else {
			Sexp *job_msg = slave_await_msg(job_queue);
			// if (sexp_is_nil(*job_msg)) {
			// 	return NULL;
			// }
			job = (CallTree*)sexp_read_ptr(*job_msg);
			free(job_msg);
		}
			// debugging
			LOG(print_trace(job);)
			LOG(fprintf(stderr, " "LPS_Fmt"\n", LPS_Arg(sexp_format(*calltree_sexp_at(job))));)
		// if eval longjmped out, that means it has queued a job
		if (setjmp(unwind_point)) {
			continue;
		} else {
			Sexp result = eval(*calltree_sexp_at(job), I, job);
			CallTree *call_parent = job->parent;
			if (call_parent->state.num_call_results > 0)
				call_parent->state.call_results[job->origin_idx] = sexp_dup(result);
					LOG(fprintf(stderr, "Destroyed call: ");)
					LOG(print_trace(job);)
			calltree_destroy(*job, &I->arenapool);
			// continue execution in the parent that needed the value
			job = job->parent;
			if (!job->parent) continue;
				LOG(fprintf(stderr, "Unwound to: ");)
				LOG(print_trace(job);)
			LOG(if (call_parent->state.num_call_results > 0)
				LOG(fprintf(stderr, "With value "LPS_Fmt"\n", LPS_Arg(sexp_format(result)));))
			interpreter_push_call(I, job, dispatcher_id);
		}
	}
	return NULL;
}

ChannelPair *interpreter_begin(Interpreter *I, Channel *out_channel, MasterSignal *ms) {
	ChannelPair *channel = ms
		                   ? channelpair_new_one_to_many(out_channel, ms)
	                       : channelpair_new_one_to_one();
	I->parent_channel = channel;
	I->dispatcher_threads = calloc(I->num_dispatchers, sizeof(*I->dispatcher_threads));
	for (size_t i = 0; i < I->num_dispatchers; ++i) {
		I->dispatcher_queues[i] = channelpair_new_one_to_one();
	}
	for (size_t i = 0; i < I->num_dispatchers; ++i) {
		struct dispatcher_config *cfg = malloc(sizeof(*cfg));
		cfg->dispatcher_id = i;
		cfg->origin = I;
		pthread_create(&I->dispatcher_threads[i], NULL, dispatcher_loop, cfg);
	}
	return channel;
}

