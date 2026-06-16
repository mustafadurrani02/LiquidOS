#include <liquidos/fs.h>
#include <liquidos/loader.h>
#include <liquidos/lib.h>
#include <liquidos/pmm.h>
#include <liquidos/process.h>
#include <liquidos/serial.h>
#include <liquidos/syscall.h>
#include <liquidos/vmm.h>

#define USER_BASE  0x0000000000400000ULL
#define USER_STACK 0x0000000000600000ULL

void loader_init(void) {
    serial_write_line("App loader initialized");
}

static LoadResult make_result(bool ok, u32 pid, const char *message) {
    LoadResult out;
    out.ok = ok;
    out.pid = pid;
    strncpy(out.message, message, sizeof(out.message) - 1);
    out.message[sizeof(out.message) - 1] = 0;
    return out;
}

LoadResult loader_load_app(const char *path) {
    const FsFile *file = fs_find(path);
    if (!file) {
        return make_result(false, 0, "app file not found");
    }
    if (strncmp(file->contents, "LAPP", 4) != 0) {
        return make_result(false, 0, "unsupported app format");
    }

    AddressSpace space = vmm_create_user_space();
    if (!space.pml4_phys) {
        return make_result(false, 0, "address space allocation failed");
    }

    void *code = pmm_alloc_page();
    void *stack = pmm_alloc_page();
    if (!code || !stack) {
        return make_result(false, 0, "user page allocation failed");
    }

    const char *message = "hello.app: loaded into a user address space; ring-3 dispatch is next.\n";
    strncpy((char *)code, message, VMM_PAGE_SIZE - 1);

    if (!vmm_map_page(&space, USER_BASE, (u64)(uintptr_t)code, VMM_WRITE | VMM_USER) ||
        !vmm_map_page(&space, USER_STACK - VMM_PAGE_SIZE, (u64)(uintptr_t)stack, VMM_WRITE | VMM_USER) ||
        !vmm_map_guard_page(&space, USER_STACK - (VMM_PAGE_SIZE * 2))) {
        return make_result(false, 0, "user mapping failed");
    }

    u32 pid = process_spawn_user_stub("hello.app", space, USER_BASE, USER_STACK - 16, USER_BASE, VMM_PAGE_SIZE,
                                      (1ULL << SYS_WRITE) | (1ULL << SYS_EXIT) | (1ULL << SYS_YIELD) |
                                      (1ULL << SYS_GETPID) | (1ULL << SYS_TICKS));
    if (!pid) {
        return make_result(false, 0, "process table full");
    }

    serial_write_line(message);
    return make_result(true, pid, "hello.app loaded into user address space");
}
