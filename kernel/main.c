#include "include/types.h"
#include "include/io.h"
#include "include/mm.h"
#include "include/actor.h"
#include "include/scheduler.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "fs/fs.h"
#include "shell/shell.h"
#include "net/net.h"

extern void serial_init(void);
extern void serial_puts(const char* s);
extern void serial_putdec(uint64_t v);
extern void serial_puthex(uint64_t v);
extern void timer_init(void);
extern void keyboard_init(void);

/* Static kernel stack — 32KB */
static uint8_t kernel_stack[32 * 1024] __attribute__((aligned(16)));

/* Heap lives right after BSS — 1MB */
static uint8_t kernel_heap[1024 * 1024] __attribute__((aligned(PAGE_SIZE)));

/* ── Boot info from stage2 bootloader ── */
typedef struct {
    uint32_t magic;             /* must be 0x4B524E4C */
    uint32_t mem_lower_kb;      /* conventional memory (below 1MB) in KB */
    uint32_t mem_upper_kb;      /* extended memory (above 1MB) in KB */
    uint32_t reserved;
} BootInfo;

#define BOOT_MAGIC 0x4B524E4C

/* ── Sample actors to demonstrate the system ── */

static void logger_actor(Actor* self) {
    serial_puts("[actor:logger] started\n");
    Message msg;
    while (true) {
        actor_recv(self, &msg);
        serial_puts("[actor:logger] msg type=");
        serial_putdec(msg.type);
        serial_puts(" from=");
        serial_putdec(msg.sender_id);
        serial_puts("\n");
    }
}

static void timer_actor(Actor* self) {
    serial_puts("[actor:timer_mon] started — listening for ticks\n");
    uint64_t tick = 0;
    Message msg;
    while (true) {
        actor_recv(self, &msg);
        if (msg.type == MSG_INTERRUPT) {
            tick++;
            if (tick % 100 == 0) {
                serial_puts("[actor:timer_mon] ");
                serial_putdec(tick);
                serial_puts(" ticks\n");
            }
        }
    }
}

static void init_actor(Actor* self) {
    serial_puts("[actor:init] system initialised\n");
    serial_puts("[actor:init] spawning subsystem actors\n");

    /* Spawn logger */
    actor_spawn(self, "logger", logger_actor, ACTOR_PRIO_NORMAL);

    /* Spawn timer monitor and route IRQ0 to it */
    int timer_cap = actor_spawn(self, "timer_mon", timer_actor, ACTOR_PRIO_HIGH);
    if (timer_cap >= 0) {
        Capability cap;
        if (cap_get(self, (uint32_t)timer_cap, &cap)) {
            irq_route(0, cap.actor_id);  /* timer IRQ → timer_mon actor */
        }
    }

    /* Spawn filesystem */
    ramdisk_init(self);

    /* Spawn terminal and shell actors */
    int term_cap = actor_spawn(self, "terminal", terminal_actor_entry, ACTOR_PRIO_HIGH);
    int shell_cap = actor_spawn(self, "shell", shell_actor_entry, ACTOR_PRIO_NORMAL);

    /* Route keyboard IRQ1 to terminal */
    if (term_cap >= 0) {
        Capability cap;
        if (cap_get(self, (uint32_t)term_cap, &cap)) {
            irq_route(1, cap.actor_id);
        }
    }

    /* Cross-connect terminal ↔ shell via MSG_TERM_INIT */
    if (term_cap >= 0 && shell_cap >= 0) {
        Capability term_cap_val, shell_cap_val;
        cap_get(self, (uint32_t)term_cap,  &term_cap_val);
        cap_get(self, (uint32_t)shell_cap, &shell_cap_val);

        /* Tell shell: "your terminal is X" */
        Message m;
        kmemset(&m, 0, sizeof(m));
        m.type = MSG_TERM_INIT;
        msg_set_u64(&m, 0, term_cap_val.actor_id);
        actor_send_direct(shell_cap_val.actor_id, &m);

        /* Tell terminal: "your shell is Y" */
        kmemset(&m, 0, sizeof(m));
        m.type = MSG_TERM_INIT;
        msg_set_u64(&m, 0, shell_cap_val.actor_id);
        actor_send_direct(term_cap_val.actor_id, &m);
    }

    /* Spawn network stack */
    net_init(self);

    serial_puts("[actor:init] done. Entering message loop.\n");

    /* init supervises its children */
    Message msg;
    while (true) {
        actor_recv(self, &msg);
        if (msg.type == MSG_ACTOR_DIED) {
            serial_puts("[actor:init] child ");
            serial_putdec(msg.sender_id);
            serial_puts(" died — could restart here\n");
        }
    }
}

/* ── Kernel main — called from boot entry ── */
/* Windows x64 ABI: boot_info in rcx */
void kernel_main(BootInfo* boot_info) {
    serial_init();
    serial_puts("\n");
    serial_puts("===========================================\n");
    serial_puts("  ActorOS — experimental actor-model OS   \n");
    serial_puts("===========================================\n");

    /* Validate boot info */
    uint32_t mem_upper_kb = 0;
    if (boot_info && boot_info->magic == BOOT_MAGIC) {
        mem_upper_kb = boot_info->mem_upper_kb;
        serial_puts("[boot] memory: ");
        serial_putdec(mem_upper_kb);
        serial_puts(" KB upper\n");
    } else {
        /* Fallback: assume 16MB */
        mem_upper_kb = 15 * 1024;
        serial_puts("[boot] no boot info — assuming 16MB\n");
    }

    /* GDT must be first — everything else may need proper segments */
    serial_puts("[init] GDT...\n");
    gdt_init();
    gdt_set_kernel_stack((uintptr_t)(kernel_stack + sizeof(kernel_stack)));

    /* IDT — after GDT so we have valid code segment */
    serial_puts("[init] IDT...\n");
    idt_init();

    /* Physical memory allocator.
       Free memory starts at 1MB + kernel size, ends at top of RAM.
       For now, give it 1MB-16MB as a conservative estimate. */
    uint64_t free_mem_start = 0x200000;   /* 2MB — above kernel */
    uint64_t free_mem_end   = (uint64_t)(1024 + mem_upper_kb) * 1024;
    serial_puts("[init] frame allocator...\n");
    frame_alloc_init(free_mem_start, free_mem_end);

    /* Kernel heap */
    serial_puts("[init] heap...\n");
    heap_init((uintptr_t)kernel_heap, sizeof(kernel_heap));

    /* Virtual memory (page tables already set up by bootloader) */
    serial_puts("[init] VMM...\n");
    vmm_init();

    /* Actor runtime */
    serial_puts("[init] actor runtime...\n");
    scheduler_init();

    /* Drivers */
    serial_puts("[init] timer (PIT 100Hz)...\n");
    timer_init();
    serial_puts("[init] keyboard...\n");
    keyboard_init();

    /* Enable interrupts */
    sti();

    /* Spawn root init actor */
    serial_puts("[init] spawning init actor...\n");
    Actor* init = actor_create("init", init_actor, ACTOR_PRIO_HIGH);
    if (!init) {
        serial_puts("[PANIC] failed to create init actor\n");
        cli(); hlt();
    }
    actor_run(init);

    serial_puts("[init] entering scheduler...\n");

    /* Enter the scheduler — never returns */
    scheduler_enter();
}
