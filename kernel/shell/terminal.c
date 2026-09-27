/* terminal.c — Terminal actor.
 * Receives MSG_INTERRUPT (keyboard IRQ), accumulates characters into a line
 * buffer, and sends MSG_TERM_LINE to the shell actor when Enter is pressed.
 * Responds to MSG_TERM_WRITE by echoing text to the serial port (display).
 *
 * Keyboard IRQ → MSG_INTERRUPT → terminal actor → MSG_TERM_LINE → shell actor
 */

#include "../include/actor.h"
#include "../include/mm.h"

extern void serial_putc(char c);
extern void serial_puts(const char* s);

/* Shell actor ID — set by MSG_TERM_INIT */
static uint64_t shell_id = 0;

/* ── PS/2 keyboard scancode-to-ASCII (set 1, normal) ── */
static const char scancode_map[128] = {
    0,   27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']', '\n',
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,   '\\','z','x','c','v','b','n','m',',','.','/',
    0,   '*', 0,  ' ', 0,
    /* F1-F10: 59-68 */
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* 89-127 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static uint8_t ps2_read_scan(void) {
    /* Spin-wait for keyboard data ready (bit 0 of status port 0x64) */
    uint8_t scan = 0;
    /* Read scancode from data port 0x60 */
    __asm__ volatile ("inb $0x60, %0" : "=a"(scan));
    return scan;
}

/* Line editing buffer */
#define LINE_MAX 48

typedef struct {
    char     buf[LINE_MAX];
    uint32_t pos;
} LineState;

static void terminal_send_line(LineState* ls) {
    if (!shell_id) return;

    Message msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.type = MSG_TERM_LINE;
    uint32_t copy = ls->pos < MSG_MAX_DATA ? ls->pos : MSG_MAX_DATA - 1;
    __builtin_memcpy(msg.data, ls->buf, copy);
    msg.data[copy] = '\0';
    msg.len = copy + 1;
    actor_send_direct(shell_id, &msg);

    ls->pos = 0;
}

static void handle_key(LineState* ls, uint8_t scancode) {
    /* Ignore key-release events (bit 7 set) */
    if (scancode & 0x80) return;
    if (scancode >= 128) return;

    char c = scancode_map[scancode];
    if (!c) return;

    if (c == '\b') {
        if (ls->pos > 0) {
            ls->pos--;
            /* Echo backspace-space-backspace sequence */
            serial_putc('\b');
            serial_putc(' ');
            serial_putc('\b');
        }
        return;
    }

    if (c == '\n' || c == '\r') {
        ls->buf[ls->pos] = '\0';
        serial_putc('\n');
        terminal_send_line(ls);
        return;
    }

    if (ls->pos < LINE_MAX - 1) {
        ls->buf[ls->pos++] = c;
        serial_putc(c);   /* local echo */
    }
}

void terminal_actor_entry(Actor* self) {
    LineState ls;
    kmemset(&ls, 0, sizeof(ls));

    serial_puts("\r\n[terminal] ready\r\n$ ");

    Message msg;
    while (true) {
        actor_recv(self, &msg);
        switch (msg.type) {
            case MSG_INTERRUPT: {
                /* IRQ1 (keyboard) — read scancode from port 0x60 */
                uint8_t scan = ps2_read_scan();
                handle_key(&ls, scan);
                break;
            }
            case MSG_TERM_WRITE: {
                /* Shell (or any actor) wants to print text */
                const char* text = (const char*)msg.data;
                uint32_t limit = msg.len < MSG_MAX_DATA ? msg.len : MSG_MAX_DATA;
                for (uint32_t i = 0; i < limit && text[i]; i++)
                    serial_putc(text[i]);
                break;
            }
            case MSG_TERM_INIT: {
                /* Shell tells us its actor_id */
                shell_id = msg_get_u64(&msg, 0);
                break;
            }
            case MSG_SHUTDOWN:
                return;
            default:
                break;
        }
    }
}
