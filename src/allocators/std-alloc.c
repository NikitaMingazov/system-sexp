
#include "allocator.h"

#include <stddef.h> // size_t
#include <stdlib.h> // malloc, realloc, free_sized

static void* std_alloc(void* ctx, size_t size, size_t alignment) {
    (void)ctx; (void)alignment;
    return malloc(size);
}

static void* std_realloc(void* ctx, void* ptr, size_t old_size, size_t new_size, size_t alignment) {
    (void)ctx; (void)old_size; (void)alignment;
    return realloc(ptr, new_size);
}

static void std_free(void* ctx, void* ptr, size_t size) {
    (void)ctx;
    // free_sized(ptr, size);
    free(ptr);
}

static const Allocator CLIB_ALLOCATOR = {
    .alloc = std_alloc,
    .realloc = std_realloc,
    .free = std_free,
    .ctx = NULL
};

Allocator std_allocator() {
	return CLIB_ALLOCATOR;
}

