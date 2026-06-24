#include "readtable.h"
#include "sexp.h"
#include <stdio.h>
#include <stdlib.h>


size_t hash_char (char c)
	{ return c; }
int eq_char (char c, char o)
	{ return c == o; }
char* serialise_char (char c)
	{
		char *s = malloc(2);
		*s = c;
		*s = '\0';
		return s;
	}
char* serialise_reader_macro (reader_macro m)
	{ return "unimplemented"; }
#ifndef GUARD_HM_char_reader_macro_C
#define GUARD_HM_char_reader_macro_C

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
// #define NUM_PROBES 10 // is bounded probing good, or bad?
#ifndef INITIAL_SIZE
#define INITIAL_SIZE 16
#endif
typedef struct HM_Node_char_reader_macro {
	char key;
	reader_macro value;
	// TODO: null(K) trait?
	bool not_null; // zero-initialised
} HM_Node_char_reader_macro;
typedef struct HM_char_reader_macro {
	size_t capacity;
	size_t num_elements;
	struct HM_Node_char_reader_macro *table;
} HM_char_reader_macro;
static HM_char_reader_macro* HM_char_reader_macro_new(void) {
	HM_char_reader_macro *new = malloc(sizeof(*new));
	new->capacity = INITIAL_SIZE;
	new->num_elements = 0;
	new->table = calloc(INITIAL_SIZE, sizeof(HM_Node_char_reader_macro));
	return new;
}
static void HM_char_reader_macro_free(HM_char_reader_macro *HM) {
	free(HM->table);
	free(HM);
}
// helper fn
static char *format_char_reader_macro(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);
	if (len < 0)
		return NULL;
	char *buffer = malloc(len + 1);
	if (!buffer)
		return NULL;
	va_start(args, fmt);
	vsnprintf(buffer, len + 1, fmt, args);
	va_end(args);
	return buffer;
}
static char* HM_char_reader_macro_serialise(const HM_char_reader_macro *HM, const char *hm_name) {
	size_t len = 1;
	char *serialised = malloc(len+2); // closing '}'
	serialised[0] = '{';
	serialised[len] = '\0';
	for (size_t i = 0; i < HM->capacity; ++i) {
		if (HM->table[i].not_null) {
			char *k_d = serialise_char(HM->table[i].key);
			char *v_d = serialise_reader_macro(HM->table[i].value);
			char *nulled = format_char_reader_macro("{ %s, %s, true }", k_d, v_d);
			size_t extra_len = strlen(nulled);
			serialised = realloc(serialised, len+extra_len+2);
			memcpy(serialised+len, nulled, extra_len);
			len += extra_len;
			serialised[len] = '\0';
		} else {
			// not {0} because nullable keys don't need the bool
			const char *nulled = "{ {0}, {0}, false }";
			size_t extra_len = strlen(nulled);
			serialised = realloc(serialised, len+extra_len+2);
			memcpy(serialised+len, nulled, extra_len);
			len += extra_len;
			serialised[len] = '\0';
		}
		if (i+1 < HM->capacity) {
			serialised = realloc(serialised, ++len+2);
			serialised[len-1] = ',';
			serialised[len] = '\0';
		}
	}
	serialised[len] = '}';
	serialised[len+1] = '\0';

	const char *definition_quote = "\n\tconst HM_Node_char_reader_macro %s_table[] = %s;\n\tconst HM_char_reader_macro %s = ( HM_char_reader_macro ) {\n\t\t.num_elements = %zu,\n\t\t.capacity = %zu,\n\t\t.table = %s_table\n\t};\n\t" ;
	return format_char_reader_macro(definition_quote, hm_name, serialised, hm_name, HM->num_elements, HM->capacity, hm_name);
}
// quadratic probing
static size_t HM_char_reader_macro_index(const HM_char_reader_macro *HM, const char K, int attempt) {
	size_t i = attempt;
	return ( hash_char(K) + (i + i^2)/2 ) % HM->capacity; // c1 = 1, c2 = 1
}
// for different const contexts
#define HM_GET_NODE_char_reader_macro_BODY \
    for (size_t i = 0; ; ++i) { \
        size_t index = HM_char_reader_macro_index(HM, K, i); \
        if (HM->table[index].not_null) { \
            if (eq_char(HM->table[index].key, K)) { \
                return &HM->table[index]; \
            } \
        } else { \
            return NULL; \
        } \
    }
static struct HM_Node_char_reader_macro *HM_char_reader_macro_get_node_mut(HM_char_reader_macro *HM, const char K) {
	HM_GET_NODE_char_reader_macro_BODY
}
static const struct HM_Node_char_reader_macro *HM_char_reader_macro_get_node(const HM_char_reader_macro *HM, const char K) {
	HM_GET_NODE_char_reader_macro_BODY
}
static reader_macro *HM_char_reader_macro_get(HM_char_reader_macro *HM, const char K) {
	struct HM_Node_char_reader_macro *adr = HM_char_reader_macro_get_node_mut(HM, K);
	if (!adr) return NULL;
	return &adr->value;
}
static bool HM_char_reader_macro_contains(const HM_char_reader_macro *HM, const char K) {
	const struct HM_Node_char_reader_macro *adr = HM_char_reader_macro_get_node(HM, K);
	if (adr)
		return true;
	else
		return false;
}
static void HM_char_reader_macro_remove(HM_char_reader_macro *HM, const char K) {
	struct HM_Node_char_reader_macro *adr = HM_char_reader_macro_get_node_mut(HM, K);
	if (adr) {
		adr->not_null = false;
		--HM->num_elements;
	}
}
static void HM_char_reader_macro_insert(HM_char_reader_macro *HM, const char K, const reader_macro V);
static void HM_char_reader_macro_upscale(HM_char_reader_macro *HM) {
	size_t old_capacity = HM->capacity;
	HM->capacity *= 2;
	HM->table = realloc(HM->table, HM->capacity*sizeof(HM_Node_char_reader_macro));
	memset(&HM->table[old_capacity], 0, sizeof(HM_Node_char_reader_macro)*(HM->capacity-old_capacity));
	for (size_t i = 0; i < old_capacity; ++i) {
		// move existing table entries
		HM_Node_char_reader_macro *cur = &HM->table[i];
		if (cur->not_null) {
			cur->not_null = false;
			HM_char_reader_macro_insert(HM, cur->key, cur->value);
		}
	}
}
static void HM_char_reader_macro_insert(HM_char_reader_macro *HM, const char K, const reader_macro V) {
	// resize if 0.8 of capacity is used
	if (HM->num_elements * 5 >= HM->capacity * 4 )
		HM_char_reader_macro_upscale(HM);
	for (size_t i = 0; ; ++i) {
		size_t index = HM_char_reader_macro_index(HM, K, i);
		if (!HM->table[index].not_null) {
			++HM->num_elements;
			HM->table[index].key = K;
			HM->table[index].value = V;
			HM->table[index].not_null = true;
			return;
		}
	}
}

#endif


typedef struct readtable {
	HM_char_reader_macro *table;
} Readtable;

Readtable *readtable_new() {
	Readtable *new = malloc(sizeof(*new));
	new->table = HM_char_reader_macro_new();
	return new;
}
void readtable_free(Readtable *rt) {
	return HM_char_reader_macro_free (rt->table);
}

// struct reader_macro {
// 	Sexp *fn;
// 	char c;
// };
// the sexp is interpreted

reader_macro *readtable_get_macro(const Readtable *rt, char c) {
	return HM_char_reader_macro_get (rt->table, c);
}
void readtable_add_macro(Readtable *rt, char c, reader_macro macro) {
	return HM_char_reader_macro_insert (rt->table, c, macro);
}
// Sexp (*readtable_get_macro(const Readtable *rt, char c))(FILE*, char);
// void readtable_add_macro(Readtable *rt, Sexp (*macro)(FILE*, char), char c);

