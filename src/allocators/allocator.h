// allocator API

#ifndef ALLOCATOR_H_
#define ALLOCATOR_H_

#include <stddef.h> // size_t

typedef struct allocator {
	void* (*alloc)(void* ctx, size_t size, size_t alignment);
	void* (*realloc)(void* ctx, void* ptr, size_t old_size, size_t new_size, size_t alignment);
	void (*free)(void* ctx, void* ptr, size_t size);
	void* ctx;
} Allocator;

#endif // #ifndef ALLOCATOR_H_

