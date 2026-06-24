#ifndef INTERPRETER_H_
#define INTERPRETER_H_

#include "include/arena.h"
#include "primitives_t.h"
#include "master_slave_channel.h"
#include "symboltable.h"
#include "readtable_t.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef unsigned int uint;

typedef struct stack_state {
	Arena memory;
	Symboltable *symboltable;
	// cached reads of upwards symbols
	// hashtable<lps, symboltable*> cached_origins
	// reference to the list being evaluated (not owned)
	Sexp *sexp_called;
	// temporarily stored before being passed into the fn
	// lifetime is the arena
	Sexp *call_results;
} StackState;

StackState stackstate_new(Sexp *to_eval);
void stackstate_destroy(StackState ss);

// inverted tree, you go up from the leafs to the root
typedef struct call_tree {
	struct call_tree *parent;
	StackState state;
} CallTree;

CallTree calltree_new(CallTree *parent, uint call_idx);
void calltree_destroy(CallTree ct);

typedef struct call_instance {
	CallTree *node;
	uint call_idx;
} CallInst;

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
	u8 error_flag;
	ChannelPair *parent_channel;
	Readtable *rt; // not thread safe, maybe add a mutex?
	// root is a conceptually an implicit progn of all the source sexps
	CallTree *call_root;
	// the program
	Sexp sexp_root;
	// one-to-one write-only CallInst* queues
	// TODO: more accurate data structure
	ChannelPair **dispatcher_queues;
	pthread_t *dispatcher_threads;
	uint num_dispatchers; // number of dispatcher threads
} Interpreter;

Interpreter *interpreter_new(const Primitives *prims, uint num_threads);
void interpreter_set_stream(Interpreter *I, FILE *in_stream);
// void interpreter_set_root_symboltable(Interpreter *I, Symboltable *st);

int calltree_set_symbol_val(CallTree *at, const lps sym, Sexp val);
Sexp calltree_remove_symbol_val(CallTree *at, const lps sym);
Sexp *calltree_get_symbol_val_ref(CallTree *at, const lps sym);
// allocates into the arena
void *calltree_alloc(CallTree *at, size_t size);

// returns the parent of the current call (root => CallTree* == NULL)
CallInst callinst_parent(CallInst at);
// returns the next call, assuming sequential execution (halt => CallTree* == NULL)
// does not go into children, only looks at currently in-scope calls
CallInst callinst_next_call(CallInst at);
// returns ptr to sexp at the path (NULL if DNE)
Sexp *callinst_sexp_at(CallInst at);
u16 callinst_row_at_call(CallInst at);
u16 callinst_col_at_call(CallInst at);

#define ANY_DISPATCHER -1
void interpreter_push_callinst(Interpreter *I, CallInst ci, int dispatcher);

// spawns a new thread that runs the interpreter
// returns a channel for message passing, protocol design is left open
//   but main.c exists as a reference, naturally
// if only one interpreter is to be spawned, channel and signal can be left as NULL
//   and the function will produce its own.
ChannelPair *interpreter_begin(Interpreter *I, Channel *out_channel, MasterSignal *ms);

// convenience function that wraps with "eval" and calls p-exec
Sexp eval(Sexp sexp, Interpreter *I, CallInst at);

#endif // ifndef INTERPRETER_H_

