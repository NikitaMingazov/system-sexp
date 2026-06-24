// length-prefixed string
// NOT null-terminated!

#ifndef LPS_H_
#define LPS_H_

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef u8* lps;

// interfacing with null-terminated arrays
lps lps_from_cstr(const char *s);
char* lps_to_cstr(const lps s);
int lps_cmp_cstr(const lps s, const char *cstr);

void lps_free(lps s);

lps lps_from_chars_with_len(const char *s, const size_t len);
// prepares a new lps to be filled with chars
// unless you memcpy this will leak memory
lps lps_with_reserved_len(const size_t len);

// equivalent to strlen
size_t lps_len(const lps s);
// returns the new lps
// ! s1 is freed by this function !
lps lps_cat(lps s1, const lps s2);
lps lps_dup(const lps s);
int lps_cmp(const lps s1, const lps s2);

size_t lps_hash(const lps key);

// formatting macros
#define LPS_Fmt "%.*s"
#define LPS_Arg(s) (int)lps_len(s), s

#endif // ifndef LPS_H_

