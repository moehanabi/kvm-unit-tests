/*
 * Header for libc string functions
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Library General Public License version 2.
 */
#ifndef _STRING_H_
#define _STRING_H_

#include <stddef.h>  /* For size_t */

extern size_t strlen(const char *buf);
extern size_t strnlen(const char *buf, size_t maxlen);
extern char *strcat(char *dest, const char *src);
extern char *strcpy(char *dest, const char *src);
extern int strcmp(const char *a, const char *b);
extern int strncmp(const char *a, const char *b, size_t n);
extern char *strchr(const char *s, int c);
extern char *strrchr(const char *s, int c);
extern char *strchrnul(const char *s, int c);
extern char *strstr(const char *haystack, const char *needle);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);
extern void *memmove(void *dest, const void *src, size_t n);
extern void *memchr(const void *s, int c, size_t n);

#endif /* _STRING_H_ */
