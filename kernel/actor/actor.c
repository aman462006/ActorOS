#include "../include/actor.h"
#include "../include/mm.h"
#include "../include/io.h"
#include "../include/scheduler.h"

Actor* actor_table[MAX_ACTORS];
uint64_t next_actor_id = 1;

/* IRQ → actor routing table */
static uint64_t irq_routing[16];

extern void serial_puts(const char* s);
extern void serial_putdec(uint64_t v);

/* Forward declarations */
static void actor_destroy_self(void);

static Actor* actor_create_internal(const char* name, void (*entry)(Actor*),
                                     uint8_t priority, uint32_t flags) {
    int slot = -1;
    for (int i = 0; i < MAX_ACTORS; i++) {
        if (!actor_table[i]) { slot = i; break; }
    }
    if (slot < 0) return NULL;

    Actor* a = (Actor*)kzalloc(sizeof(Actor));
    if (!a) return NULL;

    a->id       = next_actor_id++;
    a->state    = ACTOR_READY;
    a->priority = priority < ACTOR_PRIO_LEVELS ? priority : ACTOR_PRIO_NORMAL;
    a->flags    = flags;
    a->entry    = entry;

    int i = 0;
    while (name[i] && i < ACTOR_NAME_LEN - 1) { a->name[i] = name[i]; i++; }
    a->name[i] = '\0';

    /* Allocate 32KB stack */
    a->stack_size = 32 * 1024;
    uint8_t* stack = (uint8_t*)kmalloc(a->stack_size);
    if (!stack) { kfree(a); return NULL; }
    a->stack_phys_base = (uintptr_t)stack;  /* identity mapped: phys == virt */

    /* Default: kernel address space, stack accessed at its kernel virtual address */
    uintptr_t stack_virt_top = (uintptr_t)stack + a->stack_size;
    a->context.cr3 = read_cr3();

    if (flags & ACTOR_FLAG_ISOLATED) {
        /* Create a new address space with kernel upper-half shared */
        uintptr_t new_cr3 = vmm_new_space();
        if (!new_cr3) { kfree(stack); kfree(a); return NULL; }

        /* Map actor stack at a fixed user-space virtual address.
           Actor stack virtual base: 0x0000_0000_0008_0000 (512KB mark).
           Grows upward in physical memory but is only accessible in this actor's space. */
        uintptr_t stack_virt_base = 0x80000ULL;
        for (size_t off = 0; off < a->stack_size; off += PAGE_SIZE) {
            vmm_map_in(new_cr3,
                       stack_virt_base + off,
                       (uintptr_t)stack + off,
                       PTE_PRESENT | PTE_WRITABLE);
        }
        stack_virt_top = stack_virt_base + a->stack_size;
        a->stack_top   = stack_virt_top;
        a->context.cr3 = new_cr3;
    } else {
        a->stack_top = stack_virt_top;
    }

    kmemset(&a->context, 0, sizeof(CpuContext));
    a->context.rip    = (uint64_t)entry;
    a->context.rsp    = a->stack_top - 8;
    a->context.rflags = 0x202;   /* IF=1, reserved bit 1 */
    a->context.rcx    = (uint64_t)a;  /* Windows x64 ABI: first arg */

    /* Return address: actor_destroy_self (so returning from entry() is safe) */
    uint64_t* rsp_ptr;
    if (flags & ACTOR_FLAG_ISOLATED) {
        /* Stack is mapped at stack_virt_base but phys is stack array.
           Write return address through physical pointer. */
        rsp_ptr = (uint64_t*)(a->stack_phys_base + a->stack_size - 8);
    } else {
        rsp_ptr = (uint64_t*)a->context.rsp;
    }
    *rsp_ptr = (uint64_t)actor_destroy_self;

    actor_table[slot] = a;

    serial_puts("[actor] '");
    serial_puts(a->name);
    serial_puts("' id=");
    serial_putdec(a->id);
    if (flags & ACTOR_FLAG_ISOLATED) serial_puts(" [isolated]");
    serial_puts("\n");

    return a;
}

Actor* actor_create(const char* name, void (*entry)(Actor*), uint8_t priority) {
    return actor_create_internal(name, entry, priority, 0);
}

/* Returns actor at slot (for iteration — avoids external symbol refs to actor_table) */
Actor* actor_get_slot(int slot) {
    if (slot < 0 || slot >= MAX_ACTORS) return NULL;
    return actor_table[slot];
}

/* Called when an actor's entry function returns */
static void actor_destroy_self(void) {
    if (current_actor) actor_destroy(current_actor);
    /* Never returns — scheduler picks next actor */
    scheduler_enter();
}

void actor_destroy(Actor* a) {
    if (!a) return;

    a->state = ACTOR_DEAD;

    /* Notify parent */
    if (a->parent) {
        Message msg;
        kmemset(&msg, 0, sizeof(msg));
        msg.type = MSG_ACTOR_DIED;
        msg.sender_id = a->id;
        /* Enqueue directly — no cap check needed for supervisor messages */
        uint64_t head = a->parent->mailbox.head;
        uint64_t tail = a->parent->mailbox.tail;
        if (head - tail < MAILBOX_SIZE) {
            a->parent->mailbox.ring[head % MAILBOX_SIZE] = msg;
            __atomic_fetch_add(&a->parent->mailbox.head, 1, __ATOMIC_SEQ_CST);
            if (a->parent->mailbox.blocked) {
                a->parent->mailbox.blocked = false;
                scheduler_enqueue(a->parent);
            }
        }
    }

    /* Remove from table */
    for (int i = 0; i < MAX_ACTORS; i++) {
        if (actor_table[i] == a) { actor_table[i] = NULL; break; }
    }

    /* Increment generation so any outstanding capabilities become stale. */
    a->generation++;

    /* Free stack. stack_phys_base is the actual kmalloc'd pointer.
     * stack_top is virtual (0x80000+size for isolated actors), not the heap ptr. */
    kfree((void*)a->stack_phys_base);
    kfree(a);
}

void actor_run(Actor* a) {
    a->state = ACTOR_READY;
    scheduler_enqueue(a);
}

/* ── Send a message via capability ── */
bool actor_send(Actor* sender, uint32_t cap_idx, Message* msg) {
    Capability cap;
    if (!cap_get(sender, cap_idx, &cap)) return false;

    /* Find target actor */
    Actor* target = NULL;
    for (int i = 0; i < MAX_ACTORS; i++) {
        if (actor_table[i] && actor_table[i]->id == cap.actor_id) {
            target = actor_table[i]; break;
        }
    }
    if (!target || target->state == ACTOR_DEAD) return false;

    /* Validate capability */
    if (!cap_validate(sender, cap_idx, cap.actor_id)) return false;
    if (!(cap.permissions & CAP_PERM_SEND)) return false;

    /* Enqueue message into target mailbox */
    uint64_t head = __atomic_load_n(&target->mailbox.head, __ATOMIC_ACQUIRE);
    uint64_t tail = __atomic_load_n(&target->mailbox.tail, __ATOMIC_ACQUIRE);

    if (head - tail >= MAILBOX_SIZE) {
        /* Mailbox full — for now drop the message (TODO: block sender) */
        return false;
    }

    msg->sender_id = sender->id;
    target->mailbox.ring[head % MAILBOX_SIZE] = *msg;
    __atomic_fetch_add(&target->mailbox.head, 1, __ATOMIC_RELEASE);

    /* Wake target if it was blocked waiting */
    if (target->mailbox.blocked) {
        target->mailbox.blocked = false;
        target->state = ACTOR_READY;
        scheduler_enqueue(target);
    }

    return true;
}

/* ── Receive — blocks if mailbox empty ── */
bool actor_recv(Actor* self, Message* out) {
    while (true) {
        /* Disable interrupts before checking the mailbox to close the TOCTOU
         * window: without CLI, an interrupt could deliver a message after
         * try_recv returns false but before we set blocked=true, causing us
         * to sleep with a message sitting in the mailbox and no waker. */
        cli();
        if (actor_try_recv(self, out)) {
            sti();
            return true;
        }
        self->state = ACTOR_BLOCKED;
        self->mailbox.blocked = true;
        /* context_switch saves rflags (IF=0) and restores next's (IF=1),
         * so the new actor runs with interrupts enabled. When we are woken
         * and return here, rflags is restored with IF=0 — loop re-checks. */
        Actor* next = scheduler_next();
        if (next && next != self) {
            actor_context_switch(&self->context, &next->context);
        } else {
            sti();
            hlt();
            cli();
        }
    }
}

bool actor_try_recv(Actor* self, Message* out) {
    uint64_t tail = __atomic_load_n(&self->mailbox.tail, __ATOMIC_ACQUIRE);
    uint64_t head = __atomic_load_n(&self->mailbox.head, __ATOMIC_ACQUIRE);
    if (tail == head) return false;

    *out = self->mailbox.ring[tail % MAILBOX_SIZE];
    __atomic_fetch_add(&self->mailbox.tail, 1, __ATOMIC_RELEASE);
    return true;
}

/* ── Kernel direct send — no capability check (kernel service actors only) ── */
void actor_send_direct(uint64_t target_id, Message* msg) {
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor* target = actor_table[i];
        if (!target || target->id != target_id || target->state == ACTOR_DEAD) continue;

        uint64_t head = __atomic_load_n(&target->mailbox.head, __ATOMIC_ACQUIRE);
        uint64_t tail = __atomic_load_n(&target->mailbox.tail, __ATOMIC_ACQUIRE);
        if (head - tail >= MAILBOX_SIZE) return;  /* drop if full */

        target->mailbox.ring[head % MAILBOX_SIZE] = *msg;
        __atomic_fetch_add(&target->mailbox.head, 1, __ATOMIC_RELEASE);

        if (target->mailbox.blocked) {
            target->mailbox.blocked = false;
            target->state = ACTOR_READY;
            scheduler_enqueue(target);
        }
        return;
    }
}

/* ── FS reply helper ── */
void fs_reply(Message* req, uint32_t resp_type, uint32_t error,
              const void* payload, uint32_t payload_len) {
    uint64_t reply_to = msg_get_u64(req, 0);
    if (!reply_to) return;

    Message resp;
    kmemset(&resp, 0, sizeof(resp));
    resp.sender_id = 0;  /* kernel actor */
    resp.type = resp_type;

    /* Encode seq + error in first 8 bytes of data */
    uint32_t seq = msg_get_u32(req, 8);
    msg_set_u32(&resp, 0, seq);
    msg_set_u32(&resp, 4, error);

    if (payload && payload_len > 0) {
        uint32_t copy = payload_len < (MSG_MAX_DATA - 8) ? payload_len : (MSG_MAX_DATA - 8);
        kmemcpy(resp.data + 8, payload, copy);
        resp.len = copy + 8;
    } else {
        resp.len = 8;
    }

    actor_send_direct(reply_to, &resp);
}

/* ── Spawn child actor ── */
static int actor_spawn_with_flags(Actor* parent, const char* name,
                                   void (*entry)(Actor*), uint8_t priority, uint32_t flags) {
    Actor* child = actor_create_internal(name, entry, priority, flags);
    if (!child) return -1;

    child->parent = parent;
    if (parent && parent->child_count < 16)
        parent->children[parent->child_count++] = child->id;

    Capability cap = {
        .actor_id    = child->id,
        .generation  = child->generation,
        .permissions = CAP_PERM_ALL
    };
    int cap_idx = cap_insert(parent, cap);
    actor_run(child);
    return cap_idx;
}

int actor_spawn(Actor* parent, const char* name,
                void (*entry)(Actor*), uint8_t priority) {
    return actor_spawn_with_flags(parent, name, entry, priority, 0);
}

int actor_spawn_isolated(Actor* parent, const char* name,
                          void (*entry)(Actor*), uint8_t priority) {
    return actor_spawn_with_flags(parent, name, entry, priority, ACTOR_FLAG_ISOLATED);
}

/* ── IRQ routing ── */
void irq_route(uint8_t irq, uint64_t actor_id) {
    if (irq < 16) irq_routing[irq] = actor_id;
}

void irq_dispatch(uint8_t irq) {
    if (irq >= 16 || irq_routing[irq] == 0) return;

    uint64_t target_id = irq_routing[irq];
    Actor* target = NULL;
    for (int i = 0; i < MAX_ACTORS; i++) {
        if (actor_table[i] && actor_table[i]->id == target_id) {
            target = actor_table[i]; break;
        }
    }
    if (!target || target->state == ACTOR_DEAD) return;

    Message msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.type      = MSG_INTERRUPT;
    msg.sender_id = 0;  /* kernel */
    msg.data[0]   = irq;
    msg.len       = 1;

    uint64_t head = __atomic_load_n(&target->mailbox.head, __ATOMIC_ACQUIRE);
    uint64_t tail = __atomic_load_n(&target->mailbox.tail, __ATOMIC_ACQUIRE);
    if (head - tail < MAILBOX_SIZE) {
        target->mailbox.ring[head % MAILBOX_SIZE] = msg;
        __atomic_fetch_add(&target->mailbox.head, 1, __ATOMIC_RELEASE);
        if (target->mailbox.blocked) {
            target->mailbox.blocked = false;
            target->state = ACTOR_READY;
            scheduler_enqueue(target);
        }
    }
}
