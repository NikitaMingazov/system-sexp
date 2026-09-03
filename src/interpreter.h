#ifndef INTERPRETER_H_
#define INTERPRETER_H_

#include "include/arena.h"
#include "arenapool.h"
#include "primitives_t.h"
#include "master_slave_channel.h"
#include "symboltable.h"
#include "readtable_t.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef unsigned int uint;

typedef struct stack_state {
	// u32 arena_idx;
	Arena memory;
	Symboltable *symboltable;
	// cached reads of upwards symbols
	// hashtable<lps, symboltable*> cached_origins
	// reference to the list being evaluated (not owned)
	Sexp *sexp_called;
	// temporarily stored, last result gets sent up on unwind
	// lifetime is the arena
	Sexp *call_results;
	size_t num_call_results;
} StackState;

StackState stackstate_new(Sexp *to_eval, size_t num_eval_tmps, ArenaPool *pool);
void stackstate_destroy(StackState ss, ArenaPool *pool);

// a calltree is a job given to the scheduler
// inverted tree, you go up from the leafs to the root
typedef struct call_tree {
	StackState state;
	struct call_tree *parent;
	// the entry in call_results the result will be sent to when popped
	uint origin_idx;
	// TODO: multithreading
	// uint owner_thread;
} CallTree;

CallTree calltree_new(CallTree *parent, Sexp *to_eval, size_t num_eval_tmps, uint call_idx, ArenaPool *pool);
void calltree_destroy(CallTree ct, ArenaPool *pool);

// create a child tree
CallTree *calltree_child_at(CallTree *parent, Sexp *to_eval, size_t num_eval_tmps, size_t branch_idx, ArenaPool *pool);
// returns the sexp at a node
Sexp *calltree_sexp_at(CallTree *at);
u16 calltree_row_at(CallTree *at);
u16 calltree_col_at(CallTree *at);
// debugging
void print_trace(CallTree *call);

int calltree_set_symbol_val(CallTree *at, const lps sym, Sexp val);
Sexp calltree_remove_symbol_val(CallTree *at, const lps sym);
Sexp *calltree_get_symbol_val_ref_walk_up(CallTree *at, const lps sym);
Sexp *calltree_get_symbol_val_ref_no_walk(CallTree *at, const lps sym);
// allocates into the arena
void *calltree_alloc(CallTree *at, size_t size);

// for internal errors
enum interpreter_error {
	ERR_NONE          = 0,
	ERR_UNCLOSED_LPAR = 1 << 0,
	ERR_UNMATCED_RPAR = 1 << 1,
	ERR_OTHER         = 1 << 2,
};

typedef struct interpreter {
	const Primitives *prims;
	FILE *in_stream;
	// for the reader
	u16 tail_row;
	u16 tail_col;
	u8 error_flags;
	ChannelPair *parent_channel;
	Readtable *rt; // not thread safe, maybe add a mutex?
	// root is a conceptually an implicit progn of all the source sexps
	CallTree *call_root;
	// the program
	Sexp sexp_root;
	ArenaPool arenapool;
	// one-to-one write-only CallInst* queues
	// TODO: more specific data structure
	ChannelPair **dispatcher_queues;
	pthread_t *dispatcher_threads;
	uint num_dispatchers; // number of dispatcher threads
} Interpreter;

Interpreter *interpreter_new(const Primitives *prims, uint num_threads);
void interpreter_free(Interpreter *I);
void interpreter_set_stream(Interpreter *I, FILE *in_stream);
// void interpreter_set_root_symboltable(Interpreter *I, Symboltable *st);

#define ANY_DISPATCHER -1
void interpreter_push_call(Interpreter *I, CallTree *job, int dispatcher);

// spawns a new thread that runs the interpreter
// returns a channel for message passing, protocol design is left open
//   but main.c exists as a reference, naturally
// if only one interpreter is to be spawned, channel and signal can be left as NULL
//   and the function will produce its own.
ChannelPair *interpreter_begin(Interpreter *I, Channel *out_channel, MasterSignal *ms);

// convenience function that wraps with "eval" and calls p-exec
Sexp eval(Sexp sexp, Interpreter *I, CallTree *at);

#endif // ifndef INTERPRETER_H_

