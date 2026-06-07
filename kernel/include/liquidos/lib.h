#ifndef LIQUIDOS_LIB_H
#define LIQUIDOS_LIB_H

#include <liquidos/types.h>

void *memcpy(void *dest, const void *src, size_t count);
void *memmove(void *dest, const void *src, size_t count);
void *memset(void *dest, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);

size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t count);

void u64_to_dec(u64 value, char *out, size_t out_size);
void u64_to_hex(u64 value, char *out, size_t out_size);

#endif
