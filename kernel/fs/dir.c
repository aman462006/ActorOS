/* dir.c — Directory actor message loop.
 * A directory is an actor whose state is a DirState: a table of
 * (name, Capability, type) entries.  The loop handles:
 *   LOOKUP_REQ  — find entry by name, reply with its capability
 *   LIST_REQ    — enumerate entries by index
 *   CREATE_REQ  — spawn a new file/dir/device actor and add it
 */

#include "../include/actor.h"
#include "../include/mm.h"
#include "fs.h"
#include "dir.h"

static bool kstreq(const char* a, const char* b) {
    while (*a && *b) { if (*a++ != *b++) return false; }
    return *a == *b;
}
static void kstrcpy_n(char* dst, const char* src, size_t n) {
    size_t i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

DirState* dir_alloc(void) {
    return (DirState*)kzalloc(sizeof(DirState));
}

void dir_insert(DirState* ds, const char* name, Capability cap, uint8_t type) {
    if (ds->count >= FS_MAX_DIR_ENTRIES) return;
    for (uint32_t i = 0; i < FS_MAX_DIR_ENTRIES; i++) {
        if (!ds->entries[i].used) {
            ds->entries[i].used = true;
            ds->entries[i].type = type;
            ds->entries[i].cap  = cap;
            kstrcpy_n(ds->entries[i].name, name, FS_NAME_MAX);
            ds->count++;
            return;
        }
    }
}

static void handle_lookup(Message* msg, DirState* ds) {
    char name[FS_NAME_MAX];
    uint32_t nlen = MSG_MAX_DATA - 12;
    if (nlen >= FS_NAME_MAX) nlen = FS_NAME_MAX - 1;
    __builtin_memcpy(name, msg->data + 12, nlen);
    name[nlen] = '\0';

    for (uint32_t i = 0; i < FS_MAX_DIR_ENTRIES; i++) {
        DirEntry* e = &ds->entries[i];
        if (!e->used || !kstreq(e->name, name)) continue;
        uint8_t buf[21];
        __builtin_memcpy(buf,      &e->cap.actor_id,    8);
        __builtin_memcpy(buf + 8,  &e->cap.generation,  4);
        __builtin_memcpy(buf + 12, &e->cap.permissions, 4);
        buf[20] = e->type;
        fs_reply(msg, MSG_FS_LOOKUP_RESP, 0, buf, 21);
        return;
    }
    fs_reply(msg, MSG_FS_LOOKUP_RESP, 1, NULL, 0);
}

static void handle_list(Message* msg, DirState* ds) {
    uint32_t req_idx  = msg_get_u32(msg, 12);
    uint32_t real_idx = 0;
    for (uint32_t i = 0; i < FS_MAX_DIR_ENTRIES; i++) {
        if (!ds->entries[i].used) continue;
        if (real_idx == req_idx) {
            uint8_t buf[36];
            __builtin_memcpy(buf,     &ds->count, 4);
            kstrcpy_n((char*)(buf + 4), ds->entries[i].name, 32);
            fs_reply(msg, MSG_FS_LIST_RESP, 0, buf, 36);
            return;
        }
        real_idx++;
    }
    uint8_t buf[4];
    __builtin_memcpy(buf, &ds->count, 4);
    fs_reply(msg, MSG_FS_LIST_RESP, 1, buf, 4);
}

static void handle_create(Message* msg, DirState* ds, Actor* self) {
    if (ds->count >= FS_MAX_DIR_ENTRIES) {
        fs_reply(msg, MSG_FS_CREATE_RESP, 1, NULL, 0);
        return;
    }
    uint8_t type = msg->data[12];
    char name[FS_NAME_MAX];
    uint32_t nlen = MSG_MAX_DATA - 13;
    if (nlen >= FS_NAME_MAX) nlen = FS_NAME_MAX - 1;
    __builtin_memcpy(name, msg->data + 13, nlen);
    name[nlen] = '\0';

    for (uint32_t i = 0; i < FS_MAX_DIR_ENTRIES; i++) {
        if (ds->entries[i].used && kstreq(ds->entries[i].name, name)) {
            fs_reply(msg, MSG_FS_CREATE_RESP, 2, NULL, 0);  /* exists */
            return;
        }
    }

    extern void file_actor_entry(Actor*);
    extern void device_actor_entry(Actor*);

    void (*entry_fn)(Actor*) = NULL;
    if      (type == FS_TYPE_FILE)   entry_fn = file_actor_entry;
    else if (type == FS_TYPE_DEVICE) entry_fn = device_actor_entry;
    else if (type == FS_TYPE_DIR)    entry_fn = dir_actor_entry;
    else { fs_reply(msg, MSG_FS_CREATE_RESP, 3, NULL, 0); return; }

    int cap_idx = actor_spawn(self, name, entry_fn, ACTOR_PRIO_NORMAL);
    if (cap_idx < 0) { fs_reply(msg, MSG_FS_CREATE_RESP, 4, NULL, 0); return; }

    Capability cap;
    cap_get(self, (uint32_t)cap_idx, &cap);
    dir_insert(ds, name, cap, type);

    uint8_t buf[17];
    __builtin_memcpy(buf,      &cap.actor_id,    8);
    __builtin_memcpy(buf + 8,  &cap.generation,  4);
    __builtin_memcpy(buf + 12, &cap.permissions, 4);
    buf[16] = type;
    fs_reply(msg, MSG_FS_CREATE_RESP, 0, buf, 17);
}

/* Shared message loop used by all directory actor variants */
void dir_main_loop(Actor* self, DirState* ds) {
    Message msg;
    while (true) {
        actor_recv(self, &msg);
        switch (msg.type) {
            case MSG_FS_LOOKUP_REQ: handle_lookup(&msg, ds);       break;
            case MSG_FS_LIST_REQ:   handle_list(&msg, ds);         break;
            case MSG_FS_CREATE_REQ: handle_create(&msg, ds, self); break;
            case MSG_SHUTDOWN:      kfree(ds); return;
            default: break;
        }
    }
}

/* Generic directory actor — empty DirState, accepts dynamic create */
void dir_actor_entry(Actor* self) {
    DirState* ds = dir_alloc();
    if (!ds) return;
    dir_main_loop(self, ds);
}
