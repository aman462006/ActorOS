#include "../include/io.h"
#include "../include/types.h"
#include "../include/scheduler.h"
#include "../arch/x86_64/idt.h"

extern void pic_clear_mask(uint8_t irq);

/* PIT 8253/8254 — runs at 1.193182 MHz */
#define PIT_CHANNEL0 0x40
#define PIT_CMD      0x43
#define PIT_HZ       100    /* 100 ticks/sec = 10ms per tick */
#define PIT_DIVISOR  (1193182 / PIT_HZ)

void timer_init(void) {
    /* Channel 0, lobyte/hibyte, rate generator (mode 2) */
    outb(PIT_CMD, 0x36);
    outb(PIT_CHANNEL0, PIT_DIVISOR & 0xFF);
    outb(PIT_CHANNEL0, (PIT_DIVISOR >> 8) & 0xFF);

    pic_clear_mask(0);  /* unmask IRQ0 (timer) */
}

/* Called from interrupt_dispatch for IRQ0 */
void timer_handler(void) {
    tick_count++;
    scheduler_tick();
}
