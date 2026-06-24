// dynamic array for chars

#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>

typedef struct buffer {
	char *data;
	size_t len;
	size_t cap;
} Buffer;

Buffer *buffer_new();
void buffer_free(Buffer *buf);

void buffer_clear(Buffer *buf);
// grows the buffer's allocation to the given length
int buffer_reserve(Buffer *buf, size_t new_capacity);
char buffer_pop_end(Buffer *buf);

// add null terminator if not already present
void buffer_null_terminate(Buffer *buf);
char *buffer_clone_as_str(const Buffer *buf);
// shrinks allocation to the actual length of content
void buffer_compact(Buffer *buf);
// frees the buffer and releases its content as a null-terminated string
char *buffer_to_cstr_move(Buffer *buf);

int buffer_append_char(Buffer *buf, const char c);
int buffer_prepend_char(Buffer *buf, const char c);

int buffer_append_cstr(Buffer *buf, const char *s);
int buffer_prepend_cstr(Buffer *buf, const char* s);

int buffer_append_chars(Buffer *buf, const char *s, const size_t len);
int buffer_prepend_chars(Buffer *buf, const char* s, const size_t len);

int buffer_append_buffer(Buffer *buf, const Buffer *other);

#endif

