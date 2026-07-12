// a pool that manages arena creation and reuse, to reduce memory usage

#define NOB_DA_INIT_CAP 4
#define NOB_IMPLEMENTATION
#include "include/nob.h"

#define ARENA_REGION_DEFAULT_CAPACITY (256)
#define ARENA_IMPLEMENTATION
#include "include/arena.h"

#include "arenapool.h"

ArenaPool arena_pool_new() {
	ArenaPool new = {0};
	return new;
}

void arena_pool_destroy(ArenaPool *pool) {
	nob_da_free(pool->total);
	nob_da_free(pool->available);
}

Arena arena_pool_get(ArenaPool *pool) {
	// TODO: atomic and spinlock mutex
	if (pool->available.count > 0) {
		return nob_da_pop(&pool->available);
	}
	nob_da_append(&pool->total, (Arena) {0});
	Arena a = nob_da_last(&pool->total);
	return a;
}

void arena_pool_return(ArenaPool *pool, Arena a) {
	// fprintf(stderr, "Released Arena { begin=%p, end=%p }\n", a.begin, a.end);
	arena_reset(&a);
	nob_da_append(&pool->available, a);
}

