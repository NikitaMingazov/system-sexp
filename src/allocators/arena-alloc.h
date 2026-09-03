// wraps a general-purpose allocator with a bump allocation strategy

#ifndef ARENA_ALLOCATOR_H_
#define ARENA_ALLOCATOR_H_

#include "allocator.h"

// create an arena allocator (the arena itself is allocated)
Allocator arena_allocator_new(Allocator a);

// calling below functions on an Allocator not from arena_allocator_new is UB

// reset the allocator's arena
void arena_allocator_reset(Allocator *a);
// free the allocator's arena
void arena_allocator_free(Allocator *a);

#endif // #ifndef ARENA_ALLOCATOR_H_

