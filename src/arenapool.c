// a pool that manages arena creation and reuse, to reduce memory usage

#define NOB_DA_INIT_CAP 4
#define NOB_IMPLEMENTATION
#include "include/nob.h"

#include "allocators/allocator.h"
#include "allocators/arena-alloc.h"
#include "allocators/std-alloc.h"

#include "arenapool.h"

ArenaPool arena_pool_new() {
	ArenaPool new = {0};
	return new;
}

void arena_pool_destroy(ArenaPool *pool) {
	for (unsigned i = 0; i < pool->total.count; ++i) {
		arena_allocator_free(&pool->total.items[i]);
	}
	nob_da_free(pool->total);
	nob_da_free(pool->available);
}

Allocator arena_pool_get(ArenaPool *pool) {
	// TODO: atomic and spinlock mutex
	if (pool->available.count > 0) {
		return nob_da_pop(&pool->available);
	}
	nob_da_append(&pool->total, arena_allocator_new(std_allocator()));
	Allocator a = nob_da_last(&pool->total);
	return a;
}

void arena_pool_return(ArenaPool *pool, Allocator a) {
	// fprintf(stderr, "Released Arena { begin=%p, end=%p }\n", a.begin, a.end);
	arena_allocator_reset(&a);
	nob_da_append(&pool->available, a);
}

