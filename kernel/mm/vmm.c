#include <liquidos/lib.h>
#include <liquidos/pmm.h>
#include <liquidos/serial.h>
#include <liquidos/vmm.h>

static AddressSpace kernel_space;

static u64 read_cr3(void) {
    u64 value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static u64 *phys_to_table(u64 phys) {
    return (u64 *)(uintptr_t)(phys & ~0xFFFULL);
}

static u16 pml4_index(u64 va) {
    return (u16)((va >> 39) & 0x1FF);
}

static u16 pdpt_index(u64 va) {
    return (u16)((va >> 30) & 0x1FF);
}

static u16 pd_index(u64 va) {
    return (u16)((va >> 21) & 0x1FF);
}

static u16 pt_index(u64 va) {
    return (u16)((va >> 12) & 0x1FF);
}

static u64 *ensure_next_table(u64 *table, u16 index, u64 flags) {
    if ((table[index] & VMM_PRESENT) == 0) {
        void *page = pmm_alloc_page();
        if (!page) {
            return NULL;
        }
        table[index] = ((u64)(uintptr_t)page) | VMM_PRESENT | VMM_WRITE | (flags & VMM_USER);
    }
    return phys_to_table(table[index]);
}

static u64 *ensure_page_table(u64 *pd, u16 index, u64 virtual_address, u64 flags) {
    if ((pd[index] & VMM_PRESENT) && (pd[index] & (1ULL << 7))) {
        void *page = pmm_alloc_page();
        if (!page) {
            return NULL;
        }
        u64 *pt = (u64 *)page;
        u64 base = virtual_address & ~0x1FFFFFULL;
        u64 inherited = pd[index] & (VMM_WRITE | VMM_USER);
        for (u32 i = 0; i < 512; i++) {
            pt[i] = base + (i * VMM_PAGE_SIZE) + inherited + VMM_PRESENT;
        }
        pd[index] = ((u64)(uintptr_t)pt) | VMM_PRESENT | VMM_WRITE | (flags & VMM_USER);
        return pt;
    }

    return ensure_next_table(pd, index, flags);
}

void vmm_init(void) {
    kernel_space.pml4_phys = read_cr3();
    serial_write_line("VMM initialized");
}

AddressSpace vmm_kernel_space(void) {
    return kernel_space;
}

AddressSpace vmm_create_user_space(void) {
    AddressSpace space;
    void *pml4_page = pmm_alloc_page();
    if (!pml4_page) {
        space.pml4_phys = 0;
        return space;
    }

    /* User mappings are staged in their own tables so the loader cannot mutate
       the live boot identity map before CR3 switching exists. */
    space.pml4_phys = (u64)(uintptr_t)pml4_page;
    return space;
}

bool vmm_map_page(AddressSpace *space, u64 virtual_address, u64 physical_address, u64 flags) {
    if (!space || !space->pml4_phys) {
        return false;
    }

    u64 *pml4 = phys_to_table(space->pml4_phys);
    u64 *pdpt = ensure_next_table(pml4, pml4_index(virtual_address), flags);
    if (!pdpt) {
        return false;
    }
    u64 *pd = ensure_next_table(pdpt, pdpt_index(virtual_address), flags);
    if (!pd) {
        return false;
    }
    u64 *pt = ensure_page_table(pd, pd_index(virtual_address), virtual_address, flags);
    if (!pt) {
        return false;
    }

    pt[pt_index(virtual_address)] = (physical_address & ~0xFFFULL) | flags | VMM_PRESENT;
    return true;
}

bool vmm_unmap_page(AddressSpace *space, u64 virtual_address) {
    if (!space || !space->pml4_phys) {
        return false;
    }

    u64 *pml4 = phys_to_table(space->pml4_phys);
    u64 pml4e = pml4[pml4_index(virtual_address)];
    if ((pml4e & VMM_PRESENT) == 0) {
        return false;
    }
    u64 *pdpt = phys_to_table(pml4e);
    u64 pdpte = pdpt[pdpt_index(virtual_address)];
    if ((pdpte & VMM_PRESENT) == 0) {
        return false;
    }
    u64 *pd = phys_to_table(pdpte);
    u64 pde = pd[pd_index(virtual_address)];
    if ((pde & VMM_PRESENT) == 0 || (pde & (1ULL << 7))) {
        return false;
    }
    u64 *pt = phys_to_table(pde);
    pt[pt_index(virtual_address)] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return true;
}

bool vmm_map_guard_page(AddressSpace *space, u64 virtual_address) {
    return vmm_unmap_page(space, virtual_address);
}

void vmm_report_page_fault(u64 fault_address, u64 error_code, u64 rip) {
    char text[32];
    serial_write("Page fault va=");
    serial_write_hex(fault_address);
    serial_write(" error=");
    u64_to_hex(error_code, text, sizeof(text));
    serial_write(text);
    serial_write(" rip=");
    serial_write_hex(rip);
    serial_write_line("");
}
