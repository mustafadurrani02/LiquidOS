#include <liquidos/boot.h>
#include <liquidos/lib.h>
#include <liquidos/pmm.h>
#include <liquidos/serial.h>

extern u8 __kernel_end;

#define PMM_MAX_PAGES 4096
#define PAGE_SIZE 4096ULL

static u64 total_bytes = 0;
static u64 usable_bytes = 0;
static u64 heap_start = 0;
static u64 heap_next = 0;
static u64 heap_limit = 0;
static u64 free_pages[PMM_MAX_PAGES];
static u64 free_page_count = 0;

static u64 align_up_u64(u64 value, u64 alignment) {
    if (alignment == 0) {
        return value;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

void pmm_init(const BootInfo *boot) {
    total_bytes = 0;
    usable_bytes = 0;
    free_page_count = 0;

    for (u32 i = 0; i < boot->memory_map_count && i < 32; i++) {
        const BootMemoryMapEntry *entry = &boot->memory_map[i];
        u64 end = entry->base + entry->length;
        if (end > total_bytes) {
            total_bytes = end;
        }
        if (entry->type == BOOT_MEMORY_USABLE) {
            usable_bytes += entry->length;
        }
    }

    heap_start = align_up_u64((u64)(uintptr_t)&__kernel_end, 4096);
    heap_next = heap_start;
    heap_limit = heap_start;

    for (u32 i = 0; i < boot->memory_map_count && i < 32; i++) {
        const BootMemoryMapEntry *entry = &boot->memory_map[i];
        u64 start = entry->base;
        u64 end = entry->base + entry->length;

        if (entry->type == BOOT_MEMORY_USABLE && heap_start >= start && heap_start < end) {
            heap_limit = end;
            if (heap_limit > heap_start + (8ULL * 1024ULL * 1024ULL)) {
                heap_limit = heap_start + (8ULL * 1024ULL * 1024ULL);
            }
            break;
        }
    }

    for (u32 i = 0; i < boot->memory_map_count && i < 32; i++) {
        const BootMemoryMapEntry *entry = &boot->memory_map[i];
        if (entry->type != BOOT_MEMORY_USABLE) {
            continue;
        }

        u64 start = align_up_u64(entry->base, PAGE_SIZE);
        u64 end = entry->base + entry->length;
        for (u64 page = start; page + PAGE_SIZE <= end && free_page_count < PMM_MAX_PAGES; page += PAGE_SIZE) {
            if (page < heap_limit) {
                continue;
            }
            free_pages[free_page_count++] = page;
        }
    }

    serial_write_line("PMM initialized");
}

void *kmalloc(size_t size, size_t alignment) {
    if (size == 0) {
        return NULL;
    }

    u64 aligned = align_up_u64(heap_next, alignment ? alignment : 8);
    u64 next = aligned + size;

    if (next > heap_limit) {
        return NULL;
    }

    heap_next = next;
    return (void *)(uintptr_t)aligned;
}

void *kcalloc(size_t count, size_t size, size_t alignment) {
    if (count == 0 || size == 0) {
        return NULL;
    }
    if (count > ((size_t)-1) / size) {
        return NULL;
    }

    size_t bytes = count * size;
    void *ptr = kmalloc(bytes, alignment);
    if (!ptr) {
        return NULL;
    }
    memset(ptr, 0, bytes);
    return ptr;
}

char *kstrdup(const char *text) {
    if (!text) {
        return NULL;
    }

    size_t bytes = strlen(text) + 1;
    char *copy = (char *)kmalloc(bytes, 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, bytes);
    return copy;
}

void *pmm_alloc_page(void) {
    if (free_page_count == 0) {
        return NULL;
    }

    u64 page = free_pages[--free_page_count];
    memset((void *)(uintptr_t)page, 0, PAGE_SIZE);
    return (void *)(uintptr_t)page;
}

void pmm_free_page(void *page) {
    if (!page || free_page_count >= PMM_MAX_PAGES) {
        return;
    }

    u64 address = (u64)(uintptr_t)page;
    if ((address & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    free_pages[free_page_count++] = address;
}

u64 pmm_total_bytes(void) {
    return total_bytes;
}

u64 pmm_usable_bytes(void) {
    return usable_bytes;
}

u64 pmm_heap_used_bytes(void) {
    if (heap_next < heap_start) {
        return 0;
    }
    return heap_next - heap_start;
}

u64 pmm_heap_limit_bytes(void) {
    if (heap_limit < heap_start) {
        return 0;
    }
    return heap_limit - heap_start;
}

u64 pmm_heap_free_bytes(void) {
    if (heap_limit < heap_next) {
        return 0;
    }
    return heap_limit - heap_next;
}

u64 pmm_free_page_count(void) {
    return free_page_count;
}
