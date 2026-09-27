#include "../include/io.h"
#include "../include/types.h"
#include "../include/actor.h"
#include "../arch/x86_64/idt.h"

extern void pic_clear_mask(uint8_t irq);

#define KB_DATA 0x60
#define KB_STATUS 0x64

/* IRQ routing — which actor receives keyboard messages */
static uint64_t kb_listener_id = 0;

void keyboard_init(void) {
    pic_clear_mask(1);  /* unmask IRQ1 */
}

void keyboard_set_listener(uint64_t actor_id) {
    kb_listener_id = actor_id;
}

/* US keyboard scancode → ASCII (incomplete, enough for a shell) */
static const char scancode_ascii[] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' '
};

void keyboard_handler(void) {
    uint8_t scancode = inb(KB_DATA);
    if (scancode & 0x80) return;  /* key release, ignore */
    if (scancode >= sizeof(scancode_ascii)) return;

    char c = scancode_ascii[scancode];
    if (c == 0) return;

    /* Convert to a message and deliver to listener actor */
    irq_dispatch(1);  /* handled via irq routing table */
}
