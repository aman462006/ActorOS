#pragma once
#include "types.h"

/* ── Message types ── */
#define MSG_INTERRUPT   1
#define MSG_USER        2
#define MSG_CAPABILITY  3
#define MSG_SPAWN_REQ   4
#define MSG_SPAWN_OK    5
#define MSG_ACTOR_DIED  6
#define MSG_TIMER_TICK  7
#define MSG_SHUTDOWN    8

/* Filesystem messages (data[0..7] = reply_to actor_id, data[8..11] = seq) */
#define MSG_FS_LOOKUP_REQ   20  /* data[12..47] = name (36 bytes) */
#define MSG_FS_LOOKUP_RESP  21  /* data[8..11]=error, data[12..31]=cap, data[32]=type */
#define MSG_FS_READ_REQ     22  /* data[12..19]=offset, data[20..23]=len */
#define MSG_FS_READ_RESP    23  /* data[8..11]=error, data[12..15]=bytes_read, data[16..47]=payload */
#define MSG_FS_WRITE_REQ    24  /* data[12..19]=offset, data[20..23]=len, data[24..47]=payload */
#define MSG_FS_WRITE_RESP   25  /* data[8..11]=error, data[12..15]=bytes_written */
#define MSG_FS_STAT_REQ     26  /* data[12..]=name or empty (stat self) */
#define MSG_FS_STAT_RESP    27  /* data[8..11]=error, data[12..19]=size, data[20..23]=type */
#define MSG_FS_CREATE_REQ   28  /* data[12]=type, data[13..47]=name */
#define MSG_FS_CREATE_RESP  29  /* data[8..11]=error, data[12..31]=cap */
#define MSG_FS_LIST_REQ     30  /* data[12..15]=index (for pagination) */
#define MSG_FS_LIST_RESP    31  /* data[8..11]=error, data[12..15]=total,
                                   data[16..47]=name (32 bytes) of entry at index */
#define MSG_FS_ERROR        32

/* Terminal/shell messages */
#define MSG_TERM_KEY        40  /* data[0]=char */
#define MSG_TERM_LINE       41  /* data[0..47]=line (null-terminated) */
#define MSG_TERM_WRITE      42  /* data[0..47]=text (null-terminated) */
#define MSG_TERM_INIT       43  /* shell→terminal: init with shell actor_id */

/* FS entry types */
#define FS_TYPE_FILE        1
#define FS_TYPE_DIR         2
#define FS_TYPE_DEVICE      3

#define MSG_MAX_DATA 48     /* bytes of inline payload per message */

typedef struct {
    uint64_t sender_id;     /* which actor sent this */
    uint32_t type;          /* MSG_* constant */
    uint32_t len;           /* bytes used in data[] */
    uint8_t  data[MSG_MAX_DATA];
} Message;

/* ── Capabilities ── */
#define CAP_PERM_SEND       (1 << 0)
#define CAP_PERM_KILL       (1 << 1)
#define CAP_PERM_SPAWN_CHILD (1 << 2)
#define CAP_PERM_ALL        0x07

typedef struct {
    uint64_t actor_id;
    uint32_t generation;    /* revocation counter */
    uint32_t permissions;
} Capability;

#define CAP_TABLE_SIZE 256

typedef struct {
    Capability entries[CAP_TABLE_SIZE];
    bool valid[CAP_TABLE_SIZE];
} CapTable;

/* ── CPU context (saved during actor switch) ── */
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8,  r9,  r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t cr3;           /* page table root */
} CpuContext;

/* ── Mailbox (fixed-size ring, MPSC) ── */
#define MAILBOX_SIZE 64

typedef struct {
    Message  ring[MAILBOX_SIZE];
    uint64_t head;          /* next slot to write (producer) */
    uint64_t tail;          /* next slot to read  (consumer) */
    bool     blocked;       /* actor is sleeping waiting for message */
} Mailbox;

/* ── Actor states ── */
typedef enum {
    ACTOR_READY   = 0,
    ACTOR_RUNNING = 1,
    ACTOR_BLOCKED = 2,
    ACTOR_DEAD    = 3,
} ActorState;

/* ── Actor priority ── */
#define ACTOR_PRIO_LEVELS 8
#define ACTOR_PRIO_HIGH   0
#define ACTOR_PRIO_NORMAL 4
#define ACTOR_PRIO_LOW    7

/* ── Actor flags ── */
#define ACTOR_FLAG_ISOLATED (1 << 0)  /* own page table for memory isolation */

/* ── Actor ── */
#define ACTOR_NAME_LEN 32
#define MAX_ACTORS 1024

typedef struct Actor Actor;
struct Actor {
    uint64_t    id;
    uint32_t    generation;
    ActorState  state;
    uint8_t     priority;
    uint32_t    flags;          /* ACTOR_FLAG_* */
    char        name[ACTOR_NAME_LEN];

    Mailbox     mailbox;
    CapTable    caps;
    CpuContext  context;

    uintptr_t   stack_top;
    size_t      stack_size;
    uintptr_t   stack_phys_base; /* physical base of stack frames */

    Actor*      parent;
    uint64_t    children[16];
    uint32_t    child_count;

    void (*entry)(Actor* self);
};

/* ── Actor table (flat array, kernel-global) ── */
extern Actor* actor_table[MAX_ACTORS];
extern uint64_t next_actor_id;

/* ── Actor API ── */
Actor*   actor_create(const char* name, void (*entry)(Actor*), uint8_t priority);
void     actor_destroy(Actor* a);
Actor*   actor_get_slot(int slot);     /* iterate: slot 0..MAX_ACTORS-1, returns NULL for empty */
void     actor_run(Actor* a);               /* mark as READY, enqueue in scheduler */

/* Send/Receive — the ONLY communication primitives */
bool     actor_send(Actor* sender, uint32_t cap_idx, Message* msg);
bool     actor_recv(Actor* self, Message* out);  /* blocks if mailbox empty */
bool     actor_try_recv(Actor* self, Message* out);  /* non-blocking */

/* Spawn a child actor. Returns index into sender's cap table, or -1 on failure */
int      actor_spawn(Actor* parent, const char* name,
                     void (*entry)(Actor*), uint8_t priority);
int      actor_spawn_isolated(Actor* parent, const char* name,
                     void (*entry)(Actor*), uint8_t priority);

/* Kernel direct send — bypasses capability checks (kernel service actors only) */
void     actor_send_direct(uint64_t target_id, Message* msg);

/* Convenience: build a FS response from a request */
void     fs_reply(Message* req, uint32_t resp_type, uint32_t error,
                  const void* payload, uint32_t payload_len);

/* Helper: read uint64_t / uint32_t from msg.data[] */
static inline uint64_t msg_get_u64(const Message* m, int offset) {
    uint64_t v; __builtin_memcpy(&v, m->data + offset, 8); return v;
}
static inline uint32_t msg_get_u32(const Message* m, int offset) {
    uint32_t v; __builtin_memcpy(&v, m->data + offset, 4); return v;
}
static inline void msg_set_u64(Message* m, int offset, uint64_t v) {
    __builtin_memcpy(m->data + offset, &v, 8);
}
static inline void msg_set_u32(Message* m, int offset, uint32_t v) {
    __builtin_memcpy(m->data + offset, &v, 4);
}

/* Capability management */
int      cap_insert(Actor* a, Capability cap);  /* returns cap index */
bool     cap_get(Actor* a, uint32_t idx, Capability* out);
void     cap_revoke(Actor* a, uint32_t idx);
/* Kernel validates cap before allowing send */
bool     cap_validate(Actor* a, uint32_t cap_idx, uint64_t target_id);

/* Interrupt → message routing */
void     irq_route(uint8_t irq, uint64_t actor_id);
void     irq_dispatch(uint8_t irq);  /* called from interrupt handler */
