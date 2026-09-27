#pragma once
#include "../../include/types.h"

/* Segment selectors (byte offset into GDT) */
#define GDT_NULL    0x00
#define GDT_KCODE   0x08    /* kernel code, ring 0 */
#define GDT_KDATA   0x10    /* kernel data, ring 0 */
#define GDT_UCODE   0x18    /* user code,   ring 3 */
#define GDT_UDATA   0x20    /* user data,   ring 3 */
#define GDT_TSS_LO  0x28    /* TSS lower descriptor (16-byte entry) */
/* TSS upper (next 8 bytes) is at 0x30 — GDT entry index 7 */

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} PACKED GdtEntry;

/* TSS needed for ring-3 → ring-0 stack switch */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;          /* kernel stack for ring-3 → ring-0 transitions */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];        /* interrupt stack tables */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} PACKED Tss;

typedef struct {
    uint16_t limit;
    uint64_t base;
} PACKED GdtPointer;

void gdt_init(void);
void gdt_set_kernel_stack(uintptr_t stack_top);
extern void gdt_load(GdtPointer* ptr); /* in gdt_flush.asm: lgdt + reload segments */
