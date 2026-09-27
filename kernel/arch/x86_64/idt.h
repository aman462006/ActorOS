#pragma once
#include "../../include/types.h"

#define IDT_ENTRIES 256

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;           /* interrupt stack table index (0 = none) */
    uint8_t  type_attr;     /* gate type + DPL + present bit */
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} PACKED IdtEntry;

typedef struct {
    uint16_t limit;
    uint64_t base;
} PACKED IdtPointer;

/* Interrupt frame pushed by CPU (and our stubs) */
typedef struct {
    /* Pushed by our stub */
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9,  r8;
    uint64_t rbp, rdi, rsi, rdx;
    uint64_t rcx, rbx, rax;
    /* Pushed by stub (error code slot) or CPU */
    uint64_t int_no;
    uint64_t error_code;
    /* Pushed by CPU automatically */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} PACKED InterruptFrame;

void idt_init(void);
void idt_set_handler(uint8_t vec, void* handler, uint8_t ist, uint8_t dpl);

/* The main C dispatcher — called from every ASM stub */
void interrupt_dispatch(InterruptFrame* frame);
