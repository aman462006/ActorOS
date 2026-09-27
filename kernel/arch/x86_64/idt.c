#include "idt.h"
#include "../../include/io.h"
#include "../../include/actor.h"
#include "../../include/scheduler.h"

/* Defined in idt_stubs.asm */
extern void* isr_table[256];

static IdtEntry idt[IDT_ENTRIES];
static IdtPointer idt_ptr;

static void idt_entry_set(uint8_t vec, void* handler, uint8_t ist, uint8_t dpl) {
    uint64_t addr = (uint64_t)handler;
    idt[vec].offset_low  = addr & 0xFFFF;
    idt[vec].selector    = 0x08;          /* GDT_KCODE */
    idt[vec].ist         = ist & 0x07;
    /* 0x8E = present(1) | DPL(00) | zero(0) | type(1110=interrupt gate) */
    idt[vec].type_attr   = 0x80 | ((dpl & 3) << 5) | 0x0E;
    idt[vec].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vec].reserved    = 0;
}

/* ── PIC 8259 remapping ── */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static void pic_remap(uint8_t offset1, uint8_t offset2) {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11);  io_wait();  /* ICW1: init + ICW4 needed */
    outb(PIC2_CMD, 0x11);  io_wait();
    outb(PIC1_DATA, offset1); io_wait(); /* ICW2: vector offset */
    outb(PIC2_DATA, offset2); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();  /* ICW3: slave on IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();  /* ICW4: 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, mask1);            /* restore saved masks */
    outb(PIC2_DATA, mask2);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_set_mask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, inb(port) | (1 << irq));
}

void pic_clear_mask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, inb(port) & ~(1 << irq));
}

void idt_set_handler(uint8_t vec, void* handler, uint8_t ist, uint8_t dpl) {
    idt_entry_set(vec, handler, ist, dpl);
}

void idt_init(void) {
    /* Remap PIC: IRQ0-7 → INT 32-39, IRQ8-15 → INT 40-47 */
    pic_remap(0x20, 0x28);

    /* Mask all IRQs initially (drivers unmask their own) */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    /* Install all 256 stubs */
    for (int i = 0; i < 256; i++) {
        idt_entry_set(i, isr_table[i], 0, 0);
    }

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint64_t)&idt;

    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}

/* ── Fault messages ── */
static const char* exception_names[] = {
    "Divide by zero",      "Debug",               "NMI",
    "Breakpoint",          "Overflow",             "Bound range",
    "Invalid opcode",      "Device not available", "Double fault",
    "Coprocessor overrun", "Invalid TSS",          "Segment not present",
    "Stack fault",         "General protection",   "Page fault",
    "Reserved",            "x87 FPU error",        "Alignment check",
    "Machine check",       "SIMD FP",              "Virtualization",
    "Control protection",
};

/* Called from every asm stub after registers are pushed */
void interrupt_dispatch(InterruptFrame* frame) {
    uint64_t vec = frame->int_no;

    if (vec < 32) {
        /* CPU exception */
        /* For now: print info over serial and halt.
           Future: send message to supervisor actor. */
        extern void serial_puts(const char* s);
        extern void serial_puthex(uint64_t v);
        serial_puts("\n[EXCEPTION] ");
        if (vec < 22) serial_puts(exception_names[vec]);
        else serial_puts("Reserved");
        serial_puts(" (vec=");
        serial_puthex(vec);
        serial_puts(" err=");
        serial_puthex(frame->error_code);
        serial_puts(" rip=");
        serial_puthex(frame->rip);
        serial_puts(")\n");
        __asm__ volatile("cli; hlt");
        __builtin_unreachable();
    }

    if (vec >= 32 && vec < 48) {
        /* Hardware IRQ from PIC */
        uint8_t irq = (uint8_t)(vec - 32);
        irq_dispatch(irq);
        pic_send_eoi(irq);
    }

    /* Vectors 48-255: software-defined, ignored for now */
}
