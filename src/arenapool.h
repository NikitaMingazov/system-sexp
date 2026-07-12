#ifndef ARENA_POOL_H
#define ARENA_POOL_H

#include "include/arena.h"

typedef unsigned int uint;

typedef struct {
	Arena *items;
	uint count;
	uint capacity;
} Arenas;

typedef struct {
	// space of created arenas
	Arenas total;
	// stack of unused arenas
	Arenas available;
} ArenaPool;

ArenaPool arena_pool_new();
void arena_pool_destroy(ArenaPool *pool);

// get an arena from the pool (creates one if none are available)
Arena arena_pool_get(ArenaPool *pool);
// return an arena to the pool
void arena_pool_return(ArenaPool *pool, Arena a);

#endif

