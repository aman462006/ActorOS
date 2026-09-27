/* file.c — File actor.
 * This IS the file: an actor that holds its own data and responds to messages.
 * Protocol (FS requests have reply_to at data[0..7], seq at data[8..11]):
 *   READ_REQ:  data[12..15]=offset, data[16..19]=len
 *   WRITE_REQ: data[12..15]=offset, data[16..19]=len, data[20..47]=payload
 *   STAT_REQ:  (no extra fields)
 */

#include "../include/actor.h"
#include "../include/mm.h"
#include "fs.h"

extern void serial_write(const uint8_t* buf, uint32_t len);

typedef struct {
    uint8_t  data[FS_MAX_FILE_SIZE];
    uint32_t size;
    uint8_t  type;   /* FS_TYPE_FILE or FS_TYPE_DEVICE */
} FileState;

static void handle_read(Message* msg, FileState* fs) {
    uint32_t offset = msg_get_u32(msg, 12);
    uint32_t len    = msg_get_u32(msg, 16);

    if (fs->type == FS_TYPE_DEVICE) {
        uint8_t payload[4] = {0};
        fs_reply(msg, MSG_FS_READ_RESP, 0, payload, 4);
        return;
    }
    if (offset >= fs->size) {
        fs_reply(msg, MSG_FS_READ_RESP, 1, NULL, 0);
        return;
    }

    uint32_t avail = fs->size - offset;
    if (len > avail) len = avail;

    /* fs_reply puts payload at data[8+], so max payload = MSG_MAX_DATA-8 = 40.
       We prefix with bytes_read (4 bytes), so data can be at most 36 bytes. */
    uint32_t max_data = MSG_MAX_DATA - 8 - 4;  /* 36 */
    if (len > max_data) len = max_data;

    uint8_t payload[40];
    __builtin_memcpy(payload,     &len,                4);
    __builtin_memcpy(payload + 4, fs->data + offset,   len);
    fs_reply(msg, MSG_FS_READ_RESP, 0, payload, 4 + len);
}

static void handle_write(Message* msg, FileState* fs) {
    uint32_t offset = msg_get_u32(msg, 12);
    uint32_t len    = msg_get_u32(msg, 16);

    if (fs->type == FS_TYPE_DEVICE) {
        uint32_t wlen = len < (MSG_MAX_DATA - 20) ? len : (MSG_MAX_DATA - 20);
        serial_write(msg->data + 20, wlen);
        fs_reply(msg, MSG_FS_WRITE_RESP, 0, &wlen, 4);
        return;
    }

    uint32_t max_payload = MSG_MAX_DATA - 20;  /* 28 bytes max inline data */
    if (len > max_payload) len = max_payload;

    uint32_t end = offset + len;
    if (end > FS_MAX_FILE_SIZE) {
        fs_reply(msg, MSG_FS_WRITE_RESP, 2, NULL, 0);  /* out of space */
        return;
    }
    __builtin_memcpy(fs->data + offset, msg->data + 20, len);
    if (end > fs->size) fs->size = end;
    fs_reply(msg, MSG_FS_WRITE_RESP, 0, &len, 4);
}

static void handle_stat(Message* msg, FileState* fs) {
    uint8_t buf[12];
    uint64_t sz = fs->size;
    uint32_t ty = fs->type;
    __builtin_memcpy(buf,     &sz, 8);
    __builtin_memcpy(buf + 8, &ty, 4);
    fs_reply(msg, MSG_FS_STAT_RESP, 0, buf, 12);
}

void file_actor_entry(Actor* self) {
    FileState* fs = (FileState*)kzalloc(sizeof(FileState));
    if (!fs) return;
    fs->type = FS_TYPE_FILE;

    Message msg;
    while (true) {
        actor_recv(self, &msg);
        switch (msg.type) {
            case MSG_FS_READ_REQ:  handle_read(&msg, fs);  break;
            case MSG_FS_WRITE_REQ: handle_write(&msg, fs); break;
            case MSG_FS_STAT_REQ:  handle_stat(&msg, fs);  break;
            case MSG_SHUTDOWN:     kfree(fs); return;
            default: break;
        }
    }
}

void device_actor_entry(Actor* self) {
    /* Device actors don't use the data buffer — allocate only the header. */
    FileState* fs = (FileState*)kzalloc(sizeof(FileState));
    if (!fs) return;
    fs->type = FS_TYPE_DEVICE;

    Message msg;
    while (true) {
        actor_recv(self, &msg);
        switch (msg.type) {
            case MSG_FS_READ_REQ:  handle_read(&msg, fs);  break;
            case MSG_FS_WRITE_REQ: handle_write(&msg, fs); break;
            case MSG_FS_STAT_REQ:  handle_stat(&msg, fs);  break;
            case MSG_SHUTDOWN:     kfree(fs); return;
            default: break;
        }
    }
}
