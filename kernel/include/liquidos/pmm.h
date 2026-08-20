#ifndef LIQUIDOS_PMM_H
#define LIQUIDOS_PMM_H

#include <liquidos/boot.h>
#include <liquidos/types.h>

void pmm_init(const BootInfo *boot);
void *kmalloc(size_t size, size_t alignment);
void *kcalloc(size_t count, size_t size, size_t alignment);
char *kstrdup(const char *text);
void *pmm_alloc_page(void);
void pmm_free_page(void *page);
u64 pmm_total_bytes(void);
u64 pmm_usable_bytes(void);
u64 pmm_heap_used_bytes(void);
u64 pmm_heap_limit_bytes(void);
u64 pmm_heap_free_bytes(void);
u64 pmm_free_page_count(void);

#endif
