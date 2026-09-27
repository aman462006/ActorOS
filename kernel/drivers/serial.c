#include "../include/io.h"
#include "../include/types.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);  /* disable interrupts */
    outb(COM1 + 3, 0x80);  /* enable DLAB (baud rate divisor mode) */
    outb(COM1 + 0, 0x03);  /* divisor low byte: 38400 baud */
    outb(COM1 + 1, 0x00);  /* divisor high byte */
    outb(COM1 + 3, 0x03);  /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7);  /* enable + clear FIFO, 14-byte threshold */
    outb(COM1 + 4, 0x0B);  /* IRQs enabled, RTS/DSR set */
}

static bool serial_tx_ready(void) {
    return (inb(COM1 + 5) & 0x20) != 0;
}

void serial_putc(char c) {
    while (!serial_tx_ready()) cpu_pause();
    outb(COM1, (uint8_t)c);
    if (c == '\n') {
        while (!serial_tx_ready()) cpu_pause();
        outb(COM1, '\r');
    }
}

void serial_puts(const char* s) {
    while (*s) serial_putc(*s++);
}

void serial_puthex(uint64_t v) {
    serial_puts("0x");
    bool leading = true;
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (v >> i) & 0xF;
        if (nibble == 0 && leading && i > 0) continue;
        leading = false;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    if (leading) serial_putc('0');  /* handle v == 0 */
}

void serial_putdec(uint64_t v) {
    if (v == 0) { serial_putc('0'); return; }
    char buf[21];
    int i = 0;
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i-- > 0) serial_putc(buf[i]);
}

void serial_write(const uint8_t* buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) serial_putc((char)buf[i]);
}
