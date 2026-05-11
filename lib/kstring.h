#ifndef KSTRING_H
#define KSTRING_H

#include <stddef.h>
#include <stdint.h>

void  *kmemset(void *dst, int c, size_t n);
void  *kmemcpy(void *dst, const void *src, size_t n);
size_t kstrlen(const char *s);
char  *kstrcpy(char *dst, const char *src);
int    kstrcmp(const char *a, const char *b);
int    kstrncmp(const char *a, const char *b, size_t n);

#endif
