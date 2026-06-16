#ifndef LIQUIDOS_VMM_H
#define LIQUIDOS_VMM_H

#include <liquidos/types.h>

#define VMM_PAGE_SIZE 4096ULL
#define VMM_PRESENT   0x001ULL
#define VMM_WRITE     0x002ULL
#define VMM_USER      0x004ULL
#define VMM_NOEXEC    (1ULL << 63)

typedef struct AddressSpace {
    u64 pml4_phys;
} AddressSpace;

void vmm_init(void);
AddressSpace vmm_kernel_space(void);
AddressSpace vmm_create_user_space(void);
bool vmm_map_page(AddressSpace *space, u64 virtual_address, u64 physical_address, u64 flags);
bool vmm_unmap_page(AddressSpace *space, u64 virtual_address);
bool vmm_map_guard_page(AddressSpace *space, u64 virtual_address);
void vmm_report_page_fault(u64 fault_address, u64 error_code, u64 rip);

#endif
