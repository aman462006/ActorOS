#include "../include/mm.h"
#include "../include/io.h"
#include "../include/types.h"

/* 4-level page table virtual memory manager.
   x86-64: PML4→PDPT→PD→PT→4KB page
   All physical addresses equal virtual addresses (identity map from bootloader). */

#define PML4_IDX(v)  (((v) >> 39) & 0x1FF)
#define PDPT_IDX(v)  (((v) >> 30) & 0x1FF)
#define PD_IDX(v)    (((v) >> 21) & 0x1FF)
#define PT_IDX(v)    (((v) >> 12) & 0x1FF)

/* Identity mapped: phys == virt for kernel-managed memory */
static inline uint64_t* pt_phys_to_virt(uintptr_t phys) {
    return (uint64_t*)phys;
}

static uint64_t* get_or_create_table(uint64_t* parent, int idx, uint64_t flags) {
    if (parent[idx] & PTE_PRESENT)
        return pt_phys_to_virt(parent[idx] & PTE_ADDR_MASK);
    uintptr_t frame = frame_alloc();
    if (!frame) return NULL;
    kmemset(pt_phys_to_virt(frame), 0, PAGE_SIZE);
    parent[idx] = frame | flags | PTE_PRESENT;
    return pt_phys_to_virt(frame);
}

/* Map virt→phys in CURRENT address space */
void vmm_map(uintptr_t virt, uintptr_t phys, uint64_t flags) {
    uint64_t* pml4 = pt_phys_to_virt(read_cr3() & PTE_ADDR_MASK);
    uint64_t* pdpt = get_or_create_table(pml4, PML4_IDX(virt), PTE_PRESENT|PTE_WRITABLE);
    if (!pdpt) return;
    uint64_t* pd   = get_or_create_table(pdpt, PDPT_IDX(virt), PTE_PRESENT|PTE_WRITABLE);
    if (!pd) return;
    uint64_t* pt   = get_or_create_table(pd,   PD_IDX(virt),   PTE_PRESENT|PTE_WRITABLE);
    if (!pt) return;
    pt[PT_IDX(virt)] = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    invlpg(virt);
}

/* Map virt→phys in a SPECIFIC address space (given cr3 physical address) */
void vmm_map_in(uintptr_t cr3, uintptr_t virt, uintptr_t phys, uint64_t flags) {
    uint64_t* pml4 = pt_phys_to_virt(cr3 & PTE_ADDR_MASK);
    uint64_t* pdpt = get_or_create_table(pml4, PML4_IDX(virt), PTE_PRESENT|PTE_WRITABLE);
    if (!pdpt) return;
    uint64_t* pd   = get_or_create_table(pdpt, PDPT_IDX(virt), PTE_PRESENT|PTE_WRITABLE);
    if (!pd) return;
    uint64_t* pt   = get_or_create_table(pd,   PD_IDX(virt),   PTE_PRESENT|PTE_WRITABLE);
    if (!pt) return;
    pt[PT_IDX(virt)] = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    /* No invlpg needed — this isn't the current address space */
}

void vmm_unmap(uintptr_t virt) {
    uint64_t* pml4 = pt_phys_to_virt(read_cr3() & PTE_ADDR_MASK);
    if (!(pml4[PML4_IDX(virt)] & PTE_PRESENT)) return;
    uint64_t* pdpt = pt_phys_to_virt(pml4[PML4_IDX(virt)] & PTE_ADDR_MASK);
    if (!(pdpt[PDPT_IDX(virt)] & PTE_PRESENT)) return;
    uint64_t* pd = pt_phys_to_virt(pdpt[PDPT_IDX(virt)] & PTE_ADDR_MASK);
    if (!(pd[PD_IDX(virt)] & PTE_PRESENT)) return;
    uint64_t* pt = pt_phys_to_virt(pd[PD_IDX(virt)] & PTE_ADDR_MASK);
    pt[PT_IDX(virt)] = 0;
    invlpg(virt);
}

/* Translate a virtual address to physical (works only in current address space) */
uintptr_t vmm_virt_to_phys(uintptr_t virt) {
    uint64_t* pml4 = pt_phys_to_virt(read_cr3() & PTE_ADDR_MASK);
    if (!(pml4[PML4_IDX(virt)] & PTE_PRESENT)) return 0;
    uint64_t* pdpt = pt_phys_to_virt(pml4[PML4_IDX(virt)] & PTE_ADDR_MASK);
    if (!(pdpt[PDPT_IDX(virt)] & PTE_PRESENT)) return 0;
    uint64_t pdpte = pdpt[PDPT_IDX(virt)];
    /* 1GB huge page */
    if (pdpte & PTE_HUGE) return (pdpte & PTE_ADDR_MASK) | (virt & 0x3FFFFFFF);
    uint64_t* pd = pt_phys_to_virt(pdpte & PTE_ADDR_MASK);
    if (!(pd[PD_IDX(virt)] & PTE_PRESENT)) return 0;
    uint64_t pde = pd[PD_IDX(virt)];
    /* 2MB huge page */
    if (pde & PTE_HUGE) return (pde & PTE_ADDR_MASK) | (virt & 0x1FFFFF);
    uint64_t* pt = pt_phys_to_virt(pde & PTE_ADDR_MASK);
    if (!(pt[PT_IDX(virt)] & PTE_PRESENT)) return 0;
    return (pt[PT_IDX(virt)] & PTE_ADDR_MASK) | (virt & 0xFFF);
}

/* Create a new isolated address space with kernel upper-half copied */
uintptr_t vmm_new_space(void) {
    uintptr_t pml4_phys = frame_alloc();
    if (!pml4_phys) return 0;
    uint64_t* pml4 = pt_phys_to_virt(pml4_phys);
    kmemset(pml4, 0, PAGE_SIZE);

    /* Share kernel mappings (PML4 entries 256-511 = upper half) */
    uint64_t* cur_pml4 = pt_phys_to_virt(read_cr3() & PTE_ADDR_MASK);
    for (int i = 256; i < 512; i++)
        pml4[i] = cur_pml4[i];

    return pml4_phys;
}

void vmm_switch(uintptr_t cr3) { write_cr3(cr3); }

void vmm_init(void) {
    extern void serial_puts(const char*);
    serial_puts("[mm] VMM ready (identity map, per-actor isolation available)\n");
}
