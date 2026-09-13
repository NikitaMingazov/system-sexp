#ifndef ARENA_POOL_H
#define ARENA_POOL_H

#include "allocators/allocator.h"
// maybe split allocator vtable and ctx for uses like this
// #include "include/arena.h"

typedef unsigned int uint;

typedef struct {
	Allocator *items;
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
Allocator arena_pool_get(ArenaPool *pool);
// return an arena to the pool
void arena_pool_return(ArenaPool *pool, Allocator a);

#endif

