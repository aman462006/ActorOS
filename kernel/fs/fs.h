#pragma once
#include "../include/actor.h"

/* Maximum file size for a single in-memory file (ramdisk) */
#define FS_MAX_FILE_SIZE  4096
/* Maximum files in a single directory */
#define FS_MAX_DIR_ENTRIES 32
/* Maximum name length */
#define FS_NAME_MAX 32

/* Global actor IDs for well-known filesystem actors */
extern uint64_t fs_root_id;  /* root directory '/' */

/* Spawn the root ramdisk filesystem */
void ramdisk_init(Actor* parent);

/* Called by actors to send filesystem requests.
   The reply arrives as MSG_FS_*_RESP in the caller's mailbox.
   reply_cap_id = actor_id that should receive the response (usually self->id). */
void fs_lookup(uint64_t dir_id, uint64_t reply_to, uint32_t seq,
               const char* name);
void fs_read(uint64_t file_id, uint64_t reply_to, uint32_t seq,
             uint32_t offset, uint32_t len);
void fs_write(uint64_t file_id, uint64_t reply_to, uint32_t seq,
              uint32_t offset, const void* data, uint32_t len);
void fs_stat(uint64_t node_id, uint64_t reply_to, uint32_t seq);
void fs_create(uint64_t dir_id, uint64_t reply_to, uint32_t seq,
               uint8_t type, const char* name);
void fs_list(uint64_t dir_id, uint64_t reply_to, uint32_t seq, uint32_t index);
