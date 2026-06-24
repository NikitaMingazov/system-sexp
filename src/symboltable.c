#include "symboltable.h"
#include "sexp.h"
#include "lps.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static size_t find(Symboltable *st, lps key) {
	size_t idx = lps_hash(key) % st->capacity;
	for (size_t i = 0; st->hashtable[idx].key != NULL; ++i) {
		if (lps_cmp(st->hashtable[idx].key, key) == 0)
			return idx;
		idx = (idx + (i + i*i)/2) % st->capacity;
	}
	return idx;
}

static void resize(Symboltable *st) {
	size_t old_cap = st->capacity;
	Entry *old = st->hashtable;
	size_t new_cap = old_cap * 2;
	Entry *new_table = calloc(new_cap, sizeof(Entry));
	if (!new_table) return;
	st->hashtable = new_table;
	st->capacity = new_cap;
	st->size = 0;
	// move old table into the new
	for (size_t i = 0; i < old_cap; i++) {
		if (old[i].key == NULL)
			continue;
		size_t idx = lps_hash(old[i].key) % new_cap;
		while (st->hashtable[idx].key != NULL)
			idx = (idx + 1) % new_cap;
		st->hashtable[idx] = old[i];
		st->size++;
	}
	free(old);
}

Symboltable *symboltable_new(void) {
	Symboltable *new = malloc(sizeof(*new));
	*new = (Symboltable){
		.hashtable = calloc(16, sizeof(Entry)),
		.capacity = 16,
		.size = 0
	};
	return new;
}

void symboltable_free(Symboltable *st) {
	for (size_t i = 0; i < st->capacity; i++)
		if (st->hashtable[i].key) {
			sexp_free(st->hashtable[i].val);
		}
	free(st->hashtable);
}

int symboltable_set(Symboltable *st, const lps key, Sexp val) {
	if ((double)st->size / st->capacity > 0.75) resize(st);
	size_t idx = find(st, key);
	Entry *e = &st->hashtable[idx];
	if (e->key == NULL) {
		e->key = key;
		e->val = val;
		st->size++;
	} else {
		abort();
	}
	e->val = val;
	return 0;
}

Sexp symboltable_remove(Symboltable *st, const lps key) {
	size_t idx = find(st, key);
	Entry *e = &st->hashtable[idx];
	if (e->key == NULL)
		return sexp_null();
	e->key = NULL; // mark entry as empty
	return e->val;
}

Sexp symboltable_peek(Symboltable *st, const lps key) {
	size_t idx = find(st, key);
	Entry *e = &st->hashtable[idx];
	if (e->key == NULL)
		return sexp_null();
	return e->val;
}

Sexp *symboltable_get_ref(Symboltable *st, const lps key) {
	size_t idx = find(st, key);
	Entry *e = &st->hashtable[idx];
	if (e->key == NULL)
		return NULL;
	return &e->val;
}

