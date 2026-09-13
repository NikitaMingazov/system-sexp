#include "symboltable.h"
#include "allocators/allocator.h"
#include "include/ht.h"
#include "sexp.h"
#include "lps.h"
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t lps_hasheq(Ht_Op op, void const* a_, void const* b_, size_t n)
{
	const lps *a = a_;
	const lps *b = b_;
	HT_ASSERT(n == sizeof(*a));
	switch (op) {
	case HT_HASH: return lps_hash(*a);
	case HT_EQ:   return lps_cmp(*a, *b) == 0;
	}
	return 0;
}

Symboltable *symboltable_new(Allocator a) {
	Symboltable *new = a.alloc(a.ctx, sizeof(*new), alignof(typeof(*new)));
	*new = (Symboltable){
		(inner){ .hasheq = lps_hasheq }
	};
	return new;
}

void symboltable_free(Symboltable *st) {
	ht_free(&st->table);
}

int symboltable_set(Symboltable *st, const lps key, Sexp val, Allocator a) {
	(void)a;
	*ht_put(&st->table, key) = val;
	return 0;
}

Sexp symboltable_remove(Symboltable *st, const lps key) {
	Sexp *s = ht_find(&st->table, key);
	if (!s) return sexp_null();
	Sexp result = *s;
	ht_delete(&st->table, key);
	return result;
}

Sexp symboltable_peek(Symboltable *st, const lps key) {
	Sexp *s = ht_find(&st->table, key);
	if (!s)
		return sexp_null();
	return *s;
}

Sexp *symboltable_get_ref(Symboltable *st, const lps key) {
	Sexp *s = ht_find(&st->table, key);
	if (!s)
		return NULL;
	return s;
}

