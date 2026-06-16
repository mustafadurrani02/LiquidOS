#include <liquidos/fs.h>
#include <liquidos/loader.h>
#include <liquidos/lib.h>
#include <liquidos/pmm.h>
#include <liquidos/process.h>
#include <liquidos/serial.h>
#include <liquidos/syscall.h>
#include <liquidos/usermode.h>
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

static void write_u32(u8 *out, u32 value) {
    out[0] = (u8)(value & 0xFF);
    out[1] = (u8)((value >> 8) & 0xFF);
    out[2] = (u8)((value >> 16) & 0xFF);
    out[3] = (u8)((value >> 24) & 0xFF);
}

static void build_hello_app(u8 *code) {
    static const char message[] = "hello.app: running in ring 3 through int 0x80\n";
    const u32 msg_offset = 34;
    u32 i = 0;

    code[i++] = 0x48; code[i++] = 0xC7; code[i++] = 0xC0; write_u32(&code[i], SYS_WRITE); i += 4; /* mov rax, SYS_WRITE */
    code[i++] = 0x48; code[i++] = 0x8D; code[i++] = 0x1D; write_u32(&code[i], msg_offset - (i + 4)); i += 4; /* lea rbx, [rip+msg] */
    code[i++] = 0xCD; code[i++] = 0x80;                                                     /* int 0x80 */
    code[i++] = 0x48; code[i++] = 0xC7; code[i++] = 0xC0; write_u32(&code[i], SYS_EXIT); i += 4;  /* mov rax, SYS_EXIT */
    code[i++] = 0x48; code[i++] = 0x31; code[i++] = 0xDB;                                     /* xor rbx, rbx */
    code[i++] = 0xCD; code[i++] = 0x80;                                                       /* int 0x80 */
    code[i++] = 0xEB; code[i++] = 0xFE;                                                       /* jmp $ */

    memcpy(&code[msg_offset], message, sizeof(message));
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

    build_hello_app((u8 *)code);

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

    serial_write_line("hello.app: entering ring 3");
    process_set_current(pid);
    user_enter(space.pml4_phys, USER_BASE, USER_STACK - 16);
    serial_write_line("hello.app: exited to kernel");
    return make_result(true, pid, "hello.app ran in ring 3");
}
