#include "gdt.h"
#include "../../include/mm.h"

static GdtEntry gdt[8];
static Tss      tss;
static GdtPointer gdt_ptr;

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t gran) {
    gdt[idx].base_low   = (base & 0xFFFF);
    gdt[idx].base_mid   = (base >> 16) & 0xFF;
    gdt[idx].base_high  = (base >> 24) & 0xFF;
    gdt[idx].limit_low  = (limit & 0xFFFF);
    gdt[idx].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[idx].access     = access;
}

static void gdt_set_tss(int idx, uintptr_t base, uint32_t limit) {
    /* TSS descriptor is 16 bytes (two consecutive GDT slots) */
    uint64_t* raw = (uint64_t*)&gdt[idx];
    raw[0] = 0;
    raw[1] = 0;

    /* Lower 8 bytes */
    raw[0]  = (uint64_t)(limit & 0xFFFF);
    raw[0] |= ((base & 0xFFFFFF) << 16);
    raw[0] |= ((uint64_t)0x89 << 40);          /* present, ring-0, 64-bit TSS available */
    raw[0] |= (((uint64_t)(limit >> 16) & 0xF) << 48);
    raw[0] |= (((base >> 24) & 0xFF) << 56);

    /* Upper 8 bytes — high bits of 64-bit base */
    raw[1] = (base >> 32) & 0xFFFFFFFF;
}

void gdt_init(void) {
    kmemset(&tss, 0, sizeof(Tss));
    tss.iopb_offset = sizeof(Tss);

    /* Entry 0: null */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Entry 1: kernel code — access 0x9A = present|ring0|code|readable */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xAF);  /* 0xAF = 4KB gran + 64-bit */

    /* Entry 2: kernel data — access 0x92 = present|ring0|data|writable */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* Entry 3: user code (ring 3) */
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xAF);

    /* Entry 4: user data (ring 3) */
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    /* Entries 5-6: TSS (16-byte descriptor) */
    gdt_set_tss(5, (uintptr_t)&tss, sizeof(Tss) - 1);

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint64_t)&gdt;

    /* Load GDTR and flush segment registers (far return to reload CS) */
    gdt_load(&gdt_ptr);
}

void gdt_set_kernel_stack(uintptr_t stack_top) {
    tss.rsp0 = stack_top;
}
