
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
	ss.call_results = arena_alloc(&ss.memory, to_eval->val.list.num_children * sizeof(Sexp));
	return ss;
}
void stackstate_destroy(StackState ss) {
	arena_free(&ss.memory);
}

CallTree calltree_new(CallTree *parent, uint call_idx) {
	CallTree ct = (CallTree) {
		.parent = parent,
		.state = stackstate_new(&parent->state.sexp_called->val.list.children[call_idx]),
	};
	return ct;
}
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
	while (cursor && !symboltable_get_ref(at->state.symboltable, sym)) {
		cursor = cursor->parent;
	}
	return symboltable_get_ref(at->state.symboltable, sym);
}
void *calltree_alloc(CallTree *at, size_t size) {
	return arena_alloc(&at->state.memory, size);
}

// returns ptr to sexp at the path (NULL if DNE)
Sexp *callinst_sexp_at(CallInst at) {
	return &at.node->state.sexp_called->val.list.children[at.call_idx];
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
	msg->val.atom.type = A_PTR;
	msg->val.atom.val.as_ptr = ci_heap;
	master_send_msg(I->dispatcher_queues[dispatcher], msg);
}

Sexp eval(Sexp sexp, Interpreter *I, CallInst at) {
	Sexp with_eval = sexp_new_list(at);
	sexp_list_append(&with_eval, sexp_new_atom_str(lps_from_cstr("eval"), at));
	sexp_list_append(&with_eval, sexp);
	Sexp result = p_exec(1, &with_eval, I, at);
	return result;
}

struct dispatcher_config {
	size_t dispatcher_id;
	Interpreter *origin;
};

void *dispatcher_loop(void *cfg_ptr) {
	struct dispatcher_config *cfg = cfg_ptr;
	Interpreter *I = cfg->origin;
	size_t dispatcher_id = cfg->dispatcher_id;
	ChannelPair *job_queue = I->dispatcher_queues[cfg->dispatcher_id];
	free(cfg_ptr);
next_job:
	while (1) {
		Sexp *job_msg;
		if (dispatcher_id == 0) {
			job_msg = slave_receive_msg_if_exists(job_queue);
			if (!job_msg) {
				Sexp next = read(I);
				if (sexp_is_null(next)) {
					Sexp *nil = malloc(sizeof(Sexp));
					*nil = sexp_new_source_list(0, 0);
					slave_send_msg(I->parent_channel, nil);
					// todo: queue a EOF () into each queue
					return NULL;
				}
				sexp_list_append(&I->sexp_root, next);
				CallInst newcall = (CallInst) {
					.node = I->call_root,
					.call_idx = I->sexp_root.val.list.num_children-1,
				};
				interpreter_push_callinst(I, newcall, 0);
			}
		}
		job_msg = slave_await_msg(job_queue);
		if (sexp_is_nil(*job_msg)) {
			return NULL;
		}
		CallInst job = *(CallInst*)job_msg->val.atom.val.as_ptr;
		Sexp result = eval(*callinst_sexp_at(job), I, job);
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

