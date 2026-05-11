#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

/*
 * Initialise the kernel heap over the physical region [start, start+size).
 * Must be called after paging is enabled (identity-mapped, so virt == phys).
 * Typical use: heap_init((uint32_t)&_kernel_end, HEAP_SIZE_BYTES).
 */
void heap_init(uint32_t start, size_t size);

void *kmalloc(size_t size);           /* allocate; returns NULL on failure */
void *kcalloc(size_t n, size_t size); /* allocate + zero                   */
void  kfree(void *ptr);               /* release; no-op on NULL            */

size_t heap_get_used(void);  /* bytes currently allocated */
size_t heap_get_free(void);  /* bytes available in free blocks */

#endif
