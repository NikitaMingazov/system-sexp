// length-prefixed string (akin to 'ASN.1')
// encoding:
// u8 [0,8]     u8 [1,1]        u8 [1,2^64)
// <str length> <prefix length> <char*>
// if prefix length byte <= 7F then it is used as the str length
// if not, it is 0x80+N, where N is the number of bytes before the char*
// <str length> is big-endian, left padded with implicit 0s

#include "lps.h"
#include "allocators/allocator.h"
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

static size_t prefix_len_from_strlen(const size_t len) {
	size_t prefix_len = 1; // including the prefix length byte itself
	if (len > 0x7F) { // more prefix bytes are required
		size_t len_copy = len;
		while (len_copy) {
			++prefix_len;
			len_copy = len_copy >> 8;
		}
	}
	return prefix_len;
}

void lps_free(lps s, Allocator a) {
	size_t prefix_len = prefix_len_from_strlen(lps_len(s));
	a.free(a.ctx, s-prefix_len, alignof(typeof(s)));
}

lps lps_with_reserved_len(const size_t len, Allocator a) {
	size_t prefix_len = prefix_len_from_strlen(len);
	u8 *new = a.alloc(a.ctx, prefix_len + len, alignof(typeof(*new)));
	if (prefix_len == 1) {
		new[prefix_len-1] = len;
	} else {
		new[prefix_len-1] = 0x80 + prefix_len;
		for (size_t i = 0; i < prefix_len - 1; i++) {
			// get the most significant uncopied byte
			size_t shift = 8 * ((prefix_len - 2) - i);
			new[i] = (len >> shift) & 0xFF;
		}
		// I'd like this but endianness TODO
		// memcpy(new, (void*)&len+(prefix_len-sizeof(size_t)), prefix_len-1);
		// memcpy(new, (void*)&len, prefix_len-1);
	}
	return new + prefix_len;
}

lps lps_cat(lps s1, const lps s2, Allocator a) {
	const size_t len2 = lps_len(s2);
	if (len2 == 0)
		return s1;
	const size_t len1 = lps_len(s1);
	lps out = lps_with_reserved_len(len1 + len2, a);
	memcpy(out, s1, len1);
	memcpy(out+len1, s2, len2);
	lps_free(s1, a);
	return out;
}

// make lps from char* and len pair
lps lps_from_chars_with_len(const char *s, const size_t len, Allocator a) {
	lps new = lps_with_reserved_len(len, a);
	memcpy(new, s, len);
	return new;
}

lps lps_from_cstr(const char *s, Allocator a) {
	const size_t len = strlen(s);
	return lps_from_chars_with_len(s, len, a);
}

size_t lps_len(const lps s) {
	if (s[-1] < 0x80)
		return s[-1];
	const u8 prefix_len = s[-1] - 0x80;
	const u8* prefix_start = &s[-prefix_len];
	size_t len = 0;
	for (size_t i = 0; i < prefix_len-1; ++i) {
		len = (len << 8) | prefix_start[i];
	}
	// I want to do this but endianness is a pain
	// const size_t wl = sizeof(size_t);
	// memcpy(&len, prefix_start, prefix_len);
	// len = len << (wl*(wl - prefix_len));
	return len;
}

int lps_cmp(const lps s1, const lps s2) {
	const size_t len = lps_len(s1);
	// TODO: proper handling
	if (len != lps_len(s2))
		return -1;
	return memcmp(s1, s2, len);
}

int lps_cmp_cstr(const lps s, const char *cstr) {
	const size_t len = lps_len(s);
	// TODO: proper handling
	if (len != strlen(cstr))
		return -1;
	return memcmp(s, cstr, len);
}

char* lps_to_cstr(const lps s, Allocator a) {
	const size_t len = lps_len(s);
	char *new = a.alloc(a.ctx, len+1, alignof(char));
	memcpy(new, s, len);
	new[len] = '\0';
	return new;
}

size_t lps_hash(const lps key) {
	size_t h = 5381;
	// const size_t len = lps_len(key);
	for (size_t i = 0; i < lps_len(key); i++)
		h = ((h << 5) + h) + (unsigned char)key[i];
	return h;
}

#ifdef TEST

#include "allocators/std-alloc.c"
#include <assert.h>
#include <stdio.h>

#define CONCAT_INNER(a, b) a##b
#define CONCAT(a, b) CONCAT_INNER(a, b)
// capture __COUNTER__ once and reuse it for each symbol
#define LPS_TEST(str) LPS_TEST_INNER(str, __COUNTER__)
// check if lps_len matches strlen
#define LPS_TEST_INNER(str, id) \
	const char *CONCAT(_cstr_, id) = str; \
	lps CONCAT(lps_str_, id) = lps_from_cstr(CONCAT(_cstr_, id), std_allocator()); \
	fprintf(stderr, "lps: "LPS_Fmt"\n", LPS_Arg(CONCAT(lps_str_, id))); \
	assert(memcmp(CONCAT(lps_str_, id), str, strlen(str)) == 0); \
	fprintf(stderr, "len: %zu\n", lps_len(CONCAT(lps_str_, id))); \
	assert(strlen(str) == lps_len(CONCAT(lps_str_, id))); \
	lps_free(CONCAT(lps_str_, id), std_allocator());

int main(int argc, const char *argv[]) {
	LPS_TEST("test")
	LPS_TEST("the quick brown fox")
	LPS_TEST("")
	LPS_TEST("two hundred As AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
	LPS_TEST("five hundred As AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
	return 0;
}

#endif // ifdef TEST

