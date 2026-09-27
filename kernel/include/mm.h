#pragma once
#include "types.h"

/* ── Physical frame allocator ── */
void frame_alloc_init(uint64_t mem_start, uint64_t mem_end);
uintptr_t frame_alloc(void);
void frame_free(uintptr_t frame);
uint64_t frame_alloc_free_count(void);

/* ── Virtual memory / page tables ── */

/* Page entry flags */
#define PTE_PRESENT   (1ULL << 0)
#define PTE_WRITABLE  (1ULL << 1)
#define PTE_USER      (1ULL << 2)
#define PTE_HUGE      (1ULL << 7)
#define PTE_NX        (1ULL << 63)

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

void vmm_init(void);
void vmm_map(uintptr_t virt, uintptr_t phys, uint64_t flags);
void vmm_map_in(uintptr_t cr3, uintptr_t virt, uintptr_t phys, uint64_t flags);
void vmm_unmap(uintptr_t virt);
uintptr_t vmm_virt_to_phys(uintptr_t virt);
uintptr_t vmm_new_space(void);
void vmm_switch(uintptr_t cr3);

/* Kernel virtual base — kernel identity-mapped in lower 4MB for now */
#define KERNEL_VIRT_BASE 0x0000000000000000ULL
#define KERNEL_LOAD_ADDR 0x0000000000010000ULL

/* ── Kernel heap ── */
void heap_init(uintptr_t start, size_t size);
void* kmalloc(size_t size);
void* kzalloc(size_t size);
void  kfree(void* ptr);
void* krealloc(void* ptr, size_t new_size);

/* Simple memory utilities (no libc) */
void* kmemset(void* dst, int val, size_t len);
void* kmemcpy(void* dst, const void* src, size_t len);
int   kmemcmp(const void* a, const void* b, size_t len);
