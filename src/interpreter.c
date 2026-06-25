
#define ARENA_IMPLEMENTATION
#include "include/arena.h"

#include "interpreter.h"
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
#include <threads.h>
#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

StackState stackstate_new(Sexp *to_eval) {
	StackState ss = (StackState) {
		.memory = {0},
		.sexp_called = to_eval,
		.symboltable = symboltable_new(),
	};
	size_t len = to_eval->is_list ? sexp_num_children(*to_eval) : 1;
	ss.call_results = arena_alloc(&ss.memory, len * sizeof(Sexp));
	memset(ss.call_results, 0, len * sizeof(Sexp));
	return ss;
}
void stackstate_destroy(StackState ss) {
	arena_free(&ss.memory);
}

CallTree calltree_new(CallTree *parent, Sexp *to_eval, uint call_idx) {
	CallTree ct = (CallTree) {
		.parent = parent,
		.state = stackstate_new(to_eval),
		.origin_idx = call_idx,
	};
	return ct;
}

// produces a call to an implicit progn at a call inst, the sexp may or may not
// actually be at the call location, but its result will be stored in its entry on unwind
// used for interpreted derivative functions
CallTree *calltree_child_at(CallTree *parent, Sexp *to_eval, size_t branch_idx) {
	CallTree *new = calltree_alloc(parent, sizeof(CallTree));
	*new = calltree_new(parent, to_eval, branch_idx);
	return new;
}

// produces a child call into the inside of the callinst
// CallInst calltree_branch(CallInst call) {
// }

void calltree_destroy(CallTree ct) {
	stackstate_destroy(ct.state);
}

void interpreter_set_stream(Interpreter *I, FILE *in_stream) {
	I->in_stream = in_stream;
}
// void interpreter_set_root_symboltable(Interpreter *I, Symboltable *st);

int calltree_set_symbol_val(CallTree *at, const lps sym, Sexp val) {
	return symboltable_set(at->state.symboltable, sym, val);
}
Sexp calltree_remove_symbol_val(CallTree *at, const lps sym) {
	CallTree *cursor = at;
	while (cursor && !symboltable_get_ref(at->state.symboltable, sym)) {
		cursor = cursor->parent;
	}
	return symboltable_remove(at->state.symboltable, sym);
}
Sexp *calltree_get_symbol_val_ref(CallTree *at, const lps sym) {
	CallTree *cursor = at;
	while (cursor && !symboltable_get_ref(cursor->state.symboltable, sym)) {
		cursor = cursor->parent;
		if (!cursor)
			return NULL;
	}
	return symboltable_get_ref(cursor->state.symboltable, sym);
}
void *calltree_alloc(CallTree *at, size_t size) {
	return arena_alloc(&at->state.memory, size);
}

// returns ptr to sexp at the path (NULL if DNE)
Sexp *callinst_sexp_at(CallInst at) {
	return &sexp_children(*at.node->state.sexp_called)[at.call_idx];
}
u16 callinst_row_at_call(CallInst at) {
	return callinst_sexp_at(at)->row;
}
u16 callinst_col_at_call(CallInst at) {
	return callinst_sexp_at(at)->col;
}

#define ANY_DISPATCHER -1
void interpreter_push_callinst(Interpreter *I, CallInst ci, int dispatcher);

Interpreter *interpreter_new(const Primitives *prims, uint num_threads) {
	Interpreter *new = malloc(sizeof(*new));
	*new = (Interpreter) {
		.prims = prims,
		.error_flag = ERR_NONE,
		.in_stream = NULL,
		.rt = readtable_new(),
		.sexp_root = sexp_new_source_list(0, 0),
		.tail_row = 1,
		.tail_col = 0, // increment happens before read
		.num_dispatchers = num_threads,
	};
	primitives_export_readers(prims, new->rt);
	new->dispatcher_queues = calloc(num_threads, sizeof(ChannelPair*));
	for (size_t i = 0; i < num_threads; ++i)
		new->dispatcher_queues[i] = channelpair_new_one_to_one();
	new->call_root = malloc(sizeof(CallTree));
	new->call_root->parent = NULL;
	new->call_root->state = stackstate_new(&new->sexp_root);
	// initialise eval to the primitive's default
	Sexp default_eval = sexp_new_source_list(0, 0);
	sexp_list_append(&default_eval, sexp_new_source_list(0, 0));
	sexp_list_append(&default_eval, sexp_new_source_atom(prims->default_eval_binding, 0, 0));
	calltree_set_symbol_val(new->call_root, lps_from_cstr("eval"), default_eval);
	return new;
}

void interpreter_push_callinst(Interpreter *I, CallInst ci, int dispatcher) {
	CallInst *ci_heap = malloc(sizeof(CallInst));
	memcpy(ci_heap, &ci, sizeof(CallInst));
	Sexp *msg = malloc(sizeof(Sexp));
	*msg = sexp_new_source_atom(NULL, 0, 0);
	msg->word.atom_type = A_PTR;
	msg->qword.atom.as_ptr = ci_heap;
	master_send_msg(I->dispatcher_queues[dispatcher], msg);
}

// evaluate a sexp and store the result in its parent's slot
Sexp eval(Sexp sexp, Interpreter *I, CallInst at) {
	if (at.node->parent && !sexp_is_null(at.node->state.call_results[at.call_idx]))
		return at.node->state.call_results[at.call_idx];
	Sexp *scoped = calltree_alloc(at.node, sizeof(Sexp));
	*scoped = sexp;
	CallTree *call = calltree_child_at(at.node, scoped, at.call_idx);
	Sexp with_eval = sexp_new_list(call);
	sexp_list_append(&with_eval, sexp_new_atom_str(lps_from_cstr("eval"), call));
	sexp_list_append(&with_eval, sexp);
	// p_exec can longjmp
	Sexp result = p_exec(1, &with_eval, I, call);
	// if not longjmped
	calltree_destroy(*call);
	if (at.node->parent)
		at.node->state.call_results[at.call_idx] = result;
	return result;
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
		CallInst job;
		// the zeroth dispatcher reads in sexps while not busy
		if (dispatcher_id == 0) {
			Sexp *job_msg = slave_receive_msg_if_exists(job_queue);
			if (!job_msg) {
				Sexp next = read(I);
				// terminate on EOF/lexical error  TODO: lexical error handling
				if (sexp_is_null(next)) {
					Sexp *nil = malloc(sizeof(Sexp));
					*nil = sexp_new_source_list(0, 0);
					slave_send_msg(I->parent_channel, nil);
					// todo: queue a EOF () into each queue instead?
					return NULL;
				}
				sexp_list_append(&I->sexp_root, next);
				job = (CallInst) {
					.node = I->call_root,
					.call_idx = sexp_num_children(I->sexp_root)-1,
				};
			} else {
				job = *(CallInst*)sexp_read_ptr(*job_msg);
			}
		} else {
			Sexp *job_msg = slave_await_msg(job_queue);
			// if (sexp_is_nil(*job_msg)) {
			// 	return NULL;
			// }
			job = *(CallInst*)sexp_read_ptr(*job_msg);
		}
		// job is done, unwind call stack
		if (job.call_idx >= sexp_num_children(*job.node->state.sexp_called)) {
			// check if parent is root, which doesn't save call results
			if (!job.node->parent)
				continue;
			// send return value upward using the previous job
			Sexp rval = job.node->state.call_results[job.call_idx-1];
			CallTree *call_parent = job.node->parent;
			call_parent->state.call_results[job.node->origin_idx] = rval;
			// continue execution in the parent that needed the value
			CallInst nextjob = (CallInst) {
				.node = call_parent,
				.call_idx = job.node->origin_idx,
			};
			calltree_destroy(*job.node);
			interpreter_push_callinst(I, nextjob, dispatcher_id);
			continue;
		}
		// if eval longjmped out, that means it has queued a job
		if (setjmp(unwind_point)) {
			continue;
		} else {
			job.node->state.call_results[job.call_idx] = eval(*callinst_sexp_at(job), I, job);
			// if it didn't, it's up to the dispatcher to do that
			// only interpreted operations have multiple steps
			// for primitives this triggers a return above
			CallInst nextjob = (CallInst) {
				.node = job.node,
				.call_idx = job.call_idx+1,
			};
			interpreter_push_callinst(I, nextjob, dispatcher_id);
		}
		/*
		job.node->state.call_results[job.call_idx] = result;
		// pass result up
		if (job.call_idx == 0 && job.node->parent) {
			// unwind stack
			Sexp *passed_up = calltree_alloc(job.node->parent, sizeof(Sexp));
			memcpy(passed_up, &result, sizeof(Sexp));

		}
		// in parallel evaluation of
		// (a 1 2 3)
		// 1,2,3 are pushed, then the last of the lot pushes a
		// TODO: mutex
		for (size_t i = 1; i < job.node->num_children; ++i) {
			if (sexp_is_null(job.node->state.call_results[job.call_idx]))
				goto next_job;
		}
		CallInst leftcall = job;
		leftcall.call_idx = 0;
		interpreter_push_callinst(I, leftcall, 0);
		*/
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

