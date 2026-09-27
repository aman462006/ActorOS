/* ramdisk.c — Root filesystem initialization.
 * Spawns the initial directory tree:
 *   /           root_dir_entry
 *   /dev        dev_dir_entry
 *   /dev/serial device_actor_entry
 *   /bin        dir_actor_entry (empty, filled later)
 *
 * Each directory type has its own entry function that does its specific
 * setup before calling dir_main_loop().  No global setup-function pointers
 * are needed, so there is no scheduler-ordering race.
 */

#include "../include/actor.h"
#include "../include/mm.h"
#include "fs.h"
#include "dir.h"

extern void serial_puts(const char* s);
extern void serial_putdec(uint64_t v);
extern void file_actor_entry(Actor*);
extern void device_actor_entry(Actor*);

/* Well-known filesystem actor IDs */
uint64_t fs_root_id = 0;
uint64_t fs_dev_id  = 0;
uint64_t fs_bin_id  = 0;

/* ── /dev directory ── */
static void dev_dir_entry(Actor* self) {
    fs_dev_id = self->id;
    DirState* ds = dir_alloc();
    if (!ds) return;

    /* Spawn /dev/serial */
    int cap_idx = actor_spawn(self, "serial", device_actor_entry, ACTOR_PRIO_NORMAL);
    if (cap_idx >= 0) {
        Capability cap;
        cap_get(self, (uint32_t)cap_idx, &cap);
        dir_insert(ds, "serial", cap, FS_TYPE_DEVICE);
    }
    dir_main_loop(self, ds);
}

/* ── /bin directory ── */
static void bin_dir_entry(Actor* self) {
    fs_bin_id = self->id;
    DirState* ds = dir_alloc();
    if (!ds) return;
    /* Empty for now — shell will populate via fs_create once ELF loading exists */
    dir_main_loop(self, ds);
}

/* ── / root directory ── */
static void root_dir_entry(Actor* self) {
    fs_root_id = self->id;
    DirState* ds = dir_alloc();
    if (!ds) return;

    /* Spawn /dev */
    int dev_cap_idx = actor_spawn(self, "dev", dev_dir_entry, ACTOR_PRIO_NORMAL);
    if (dev_cap_idx >= 0) {
        Capability cap;
        cap_get(self, (uint32_t)dev_cap_idx, &cap);
        dir_insert(ds, "dev", cap, FS_TYPE_DIR);
    }

    /* Spawn /bin */
    int bin_cap_idx = actor_spawn(self, "bin", bin_dir_entry, ACTOR_PRIO_NORMAL);
    if (bin_cap_idx >= 0) {
        Capability cap;
        cap_get(self, (uint32_t)bin_cap_idx, &cap);
        dir_insert(ds, "bin", cap, FS_TYPE_DIR);
    }

    serial_puts("[fs] root dir ready, id=");
    serial_putdec(self->id);
    serial_puts("\n");

    dir_main_loop(self, ds);
}

/* ── FS helper functions (build and send FS request messages) ── */

static void fs_send(uint64_t target_id, Message* msg) {
    actor_send_direct(target_id, msg);
}

void fs_lookup(uint64_t dir_id, uint64_t reply_to, uint32_t seq, const char* name) {
    Message msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.type = MSG_FS_LOOKUP_REQ;
    msg_set_u64(&msg, 0, reply_to);
    msg_set_u32(&msg, 8, seq);
    size_t i = 0;
    while (name[i] && i < FS_NAME_MAX - 1) { msg.data[12 + i] = (uint8_t)name[i]; i++; }
    msg.len = (uint32_t)(12 + i + 1);
    fs_send(dir_id, &msg);
}

void fs_read(uint64_t file_id, uint64_t reply_to, uint32_t seq,
             uint32_t offset, uint32_t len) {
    Message msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.type = MSG_FS_READ_REQ;
    msg_set_u64(&msg, 0, reply_to);
    msg_set_u32(&msg, 8, seq);
    msg_set_u32(&msg, 12, offset);
    msg_set_u32(&msg, 16, len);
    msg.len = 20;
    fs_send(file_id, &msg);
}

void fs_write(uint64_t file_id, uint64_t reply_to, uint32_t seq,
              uint32_t offset, const void* data, uint32_t len) {
    Message msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.type = MSG_FS_WRITE_REQ;
    msg_set_u64(&msg, 0, reply_to);
    msg_set_u32(&msg, 8, seq);
    msg_set_u32(&msg, 12, offset);
    if (len > MSG_MAX_DATA - 20) len = MSG_MAX_DATA - 20;
    msg_set_u32(&msg, 16, len);
    kmemcpy(msg.data + 20, data, len);
    msg.len = 20 + len;
    fs_send(file_id, &msg);
}

void fs_stat(uint64_t node_id, uint64_t reply_to, uint32_t seq) {
    Message msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.type = MSG_FS_STAT_REQ;
    msg_set_u64(&msg, 0, reply_to);
    msg_set_u32(&msg, 8, seq);
    msg.len = 12;
    fs_send(node_id, &msg);
}

void fs_create(uint64_t dir_id, uint64_t reply_to, uint32_t seq,
               uint8_t type, const char* name) {
    Message msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.type = MSG_FS_CREATE_REQ;
    msg_set_u64(&msg, 0, reply_to);
    msg_set_u32(&msg, 8, seq);
    msg.data[12] = type;
    size_t i = 0;
    while (name[i] && i < FS_NAME_MAX - 1) { msg.data[13 + i] = (uint8_t)name[i]; i++; }
    msg.len = (uint32_t)(13 + i + 1);
    fs_send(dir_id, &msg);
}

void fs_list(uint64_t dir_id, uint64_t reply_to, uint32_t seq, uint32_t index) {
    Message msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.type = MSG_FS_LIST_REQ;
    msg_set_u64(&msg, 0, reply_to);
    msg_set_u32(&msg, 8, seq);
    msg_set_u32(&msg, 12, index);
    msg.len = 16;
    fs_send(dir_id, &msg);
}

/* ── Ramdisk initialization — spawns the root actor ── */
void ramdisk_init(Actor* parent) {
    serial_puts("[fs] spawning ramdisk...\n");
    int cap_idx = actor_spawn(parent, "root", root_dir_entry, ACTOR_PRIO_NORMAL);
    if (cap_idx < 0) {
        serial_puts("[fs] ERROR: failed to spawn root\n");
        return;
    }
    Capability cap;
    cap_get(parent, (uint32_t)cap_idx, &cap);
    fs_root_id = cap.actor_id;
    serial_puts("[fs] ramdisk actor spawned\n");
}
