#ifndef __KLIB_H__
#define __KLIB_H__

#include <stdint.h>
#include <stddef.h>
#include <task.h>

#define KPRN_MAX_TYPE 3

#define KPRN_INFO   0
#define KPRN_WARN   1
#define KPRN_ERR    2
#define KPRN_DBG    3

#define EMPTY ((void *)(size_t)(-1))

pid_t kexec(const char *, const char **, const char **);

int ktolower(int);
char *kstrchrnul(const char *, int);
char *kstrcpy(char *, const char *);
size_t kstrlen(const char *);
int kstrcmp(const char *, const char *);
int kstrncmp(const char *, const char *, size_t);
void kprint(int type, const char *fmt, ...);
void *kalloc(size_t);
void kfree(void *);
void *krealloc(void *, size_t);

void *kmemset(void *, int, size_t);
void *kmemcpy(void *, const void*, size_t);
int kmemcmp(const void *, const void *, size_t);
void *memmove(void *, const void *, size_t);

#define MIN(a,b) (((a)<(b))?(a):(b))

#endif
