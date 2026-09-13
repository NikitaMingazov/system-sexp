
#define ARENA_REGION_DEFAULT_CAPACITY (256)
#define ARENA_IMPLEMENTATION
#include "arena.h"

#include "arena-alloc.h"
#include "allocator.h"
#include <stdalign.h>
#include <stddef.h>

// TODO: implement my own arena.h using the allocator
typedef struct arena_alloc_ctx {
	Allocator host;
	Arena a;
} Arena_Alloc_Ctx;

static void* inner_alloc(void* ctx, size_t size, size_t alignment) {
	(void)alignment;
	return arena_alloc(&((Arena_Alloc_Ctx*)ctx)->a, size);
}

static void* inner_realloc(void* ctx, void* ptr, size_t oldsz, size_t newsz, size_t alignment) {
    (void)ctx; (void)alignment;
	return arena_realloc(&((Arena_Alloc_Ctx*) ctx)->a, ptr, oldsz, newsz);
}

static void inner_free(void* ctx, void* ptr, size_t size) {
    (void)ctx; (void)size; (void)ptr;
}

Allocator arena_allocator_new(Allocator host) {
	Arena_Alloc_Ctx *ctx = host.alloc(host.ctx, sizeof(*ctx), alignof(typeof(*ctx)));
	*ctx = (Arena_Alloc_Ctx) {
		.host = host,
		.a = (Arena) {0},
	};
	return (Allocator) {
		.ctx = ctx,
		.alloc = inner_alloc,
		.realloc = inner_realloc,
		.free = inner_free,
	};
}

void arena_allocator_reset(Allocator *a) {
	arena_reset(&((Arena_Alloc_Ctx*)a->ctx)->a);
}

void arena_allocator_free(Allocator *a) {
	Arena_Alloc_Ctx *ctx = (Arena_Alloc_Ctx*) a->ctx;
	arena_free(&ctx->a);
	ctx->host.free(ctx->host.ctx, ctx, alignof(typeof(*ctx)));
}

