/* shell.c — Shell actor.
 * Receives MSG_TERM_LINE (a command line), parses it into command + args,
 * looks up /bin/<command> in the filesystem, spawns an actor for it,
 * and connects the terminal capability for stdout.
 *
 * For now: built-in commands (echo, ls, help) are handled inline.
 * External commands would be loaded from /bin/ once we have an ELF loader.
 */

#include "../include/actor.h"
#include "../include/mm.h"
#include "shell.h"
#include "../fs/fs.h"

extern void serial_puts(const char* s);
extern void serial_putdec(uint64_t v);

/* Terminal actor ID — to send MSG_TERM_WRITE output back */
static uint64_t terminal_id = 0;

/* ── Shell output helper ── */
static void shell_print(const char* s) {
    if (!terminal_id) { serial_puts(s); return; }
    Message msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.type = MSG_TERM_WRITE;
    uint32_t i = 0;
    while (s[i] && i < MSG_MAX_DATA - 1) { msg.data[i] = (uint8_t)s[i]; i++; }
    msg.data[i] = '\0';
    msg.len = i + 1;
    actor_send_direct(terminal_id, &msg);
}

static void shell_print_num(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[20] = '\0';
    if (v == 0) { buf[--i] = '0'; }
    else { while (v) { buf[--i] = '0' + (v % 10); v /= 10; } }
    shell_print(buf + i);
}

/* ── Simple command parser ── */
#define MAX_ARGS 8
#define ARG_LEN  24

typedef struct {
    char cmd[ARG_LEN];
    char args[MAX_ARGS][ARG_LEN];
    int  argc;
} ParsedCmd;

static void parse_cmd(const char* line, ParsedCmd* out) {
    kmemset(out, 0, sizeof(*out));

    /* Skip leading spaces */
    while (*line == ' ') line++;

    /* Extract tokens */
    int tok = -1;  /* -1 = command, 0+ = args */
    int pos = 0;
    while (*line && tok < MAX_ARGS) {
        if (*line == ' ') {
            if (tok == -1) { out->cmd[pos] = '\0'; tok = 0; pos = 0; }
            else if (pos > 0) {
                out->args[tok][pos] = '\0';
                tok++;
                pos = 0;
            }
            line++;
            continue;
        }
        char* dst = (tok == -1) ? out->cmd : out->args[tok];
        int  max  = ARG_LEN - 1;
        if (pos < max) dst[pos++] = *line;
        line++;
    }
    if (tok == -1 && pos > 0) out->cmd[pos] = '\0';
    else if (tok >= 0 && pos > 0) { out->args[tok][pos] = '\0'; tok++; }
    out->argc = tok < 0 ? 0 : tok;
}

/* ── Built-in commands ── */

static void cmd_echo(ParsedCmd* p) {
    for (int i = 0; i < p->argc; i++) {
        shell_print(p->args[i]);
        if (i < p->argc - 1) shell_print(" ");
    }
    shell_print("\r\n");
}

static void cmd_help(void) {
    shell_print("ActorOS shell — built-in commands:\r\n");
    shell_print("  echo <args>    print arguments\r\n");
    shell_print("  ls             list root directory\r\n");
    shell_print("  actors         show running actors\r\n");
    shell_print("  help           this message\r\n");
}

static void cmd_actors(void) {
    shell_print("Running actors:\r\n");
    for (int i = 0; i < 64; i++) {
        Actor* a = actor_get_slot(i);
        if (!a || a->state == ACTOR_DEAD) continue;
        shell_print("  [");
        shell_print_num(a->id);
        shell_print("] ");
        shell_print(a->name);
        shell_print("\r\n");
    }
}

static void cmd_ls(Actor* self) {
    /* Send MSG_FS_LIST_REQ to root dir and print entries.
       We use a blocking request-reply pattern: send, then recv response. */
    if (!fs_root_id) { shell_print("(no filesystem)\r\n"); return; }

    shell_print("/:\r\n");
    for (uint32_t idx = 0; idx < FS_MAX_DIR_ENTRIES; idx++) {
        fs_list(fs_root_id, self->id, idx, idx);

        Message resp;
        /* Wait for the response (may get other messages first — drain loop) */
        uint32_t tries = 0;
        bool got = false;
        while (tries < 128) {
            actor_recv(self, &resp);
            if (resp.type == MSG_FS_LIST_RESP) { got = true; break; }
            tries++;
        }
        if (!got) break;

        uint32_t error = msg_get_u32(&resp, 4);
        if (error) break;

        uint32_t total = msg_get_u32(&resp, 8);
        const char* name = (const char*)resp.data + 12;
        shell_print("  ");
        shell_print(name);
        shell_print("\r\n");
        if (idx + 1 >= total) break;
    }
}

/* ── Main shell loop ── */

void shell_actor_entry(Actor* self) {
    serial_puts("[shell] actor started\n");

    /* We need the terminal id — it's passed via MSG_TERM_INIT before the loop
       or via the spawn sequence. We'll wait for it. */
    Message msg;
    while (!terminal_id) {
        actor_recv(self, &msg);
        if (msg.type == MSG_TERM_INIT) {
            terminal_id = msg_get_u64(&msg, 0);
            /* Tell the terminal our id so it can route lines to us */
            Message reply;
            kmemset(&reply, 0, sizeof(reply));
            reply.type = MSG_TERM_INIT;
            msg_set_u64(&reply, 0, self->id);
            actor_send_direct(terminal_id, &reply);
        }
    }

    shell_print("ActorOS shell ready. Type 'help' for commands.\r\n$ ");

    while (true) {
        actor_recv(self, &msg);
        switch (msg.type) {
            case MSG_TERM_LINE: {
                const char* line = (const char*)msg.data;
                ParsedCmd p;
                parse_cmd(line, &p);

                if (p.cmd[0] == '\0') {
                    /* empty line */
                } else if (p.cmd[0] == 'e' && p.cmd[1] == 'c' && p.cmd[2] == 'h' && p.cmd[3] == 'o') {
                    cmd_echo(&p);
                } else if (p.cmd[0] == 'l' && p.cmd[1] == 's') {
                    cmd_ls(self);
                } else if (p.cmd[0] == 'h' && p.cmd[1] == 'e') {
                    cmd_help();
                } else if (p.cmd[0] == 'a' && p.cmd[1] == 'c') {
                    cmd_actors();
                } else {
                    shell_print("Unknown command: ");
                    shell_print(p.cmd);
                    shell_print("\r\n");
                }
                shell_print("$ ");
                break;
            }
            case MSG_SHUTDOWN:
                return;
            default:
                break;
        }
    }
}
