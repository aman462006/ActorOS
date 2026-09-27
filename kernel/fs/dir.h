#pragma once
#include "../include/actor.h"
#include "fs.h"

typedef struct {
    char       name[FS_NAME_MAX];
    Capability cap;
    uint8_t    type;
    bool       used;
} DirEntry;

typedef struct {
    DirEntry entries[FS_MAX_DIR_ENTRIES];
    uint32_t count;
} DirState;

/* Allocate an empty DirState */
DirState* dir_alloc(void);

/* Insert a pre-existing actor into a directory */
void dir_insert(DirState* ds, const char* name, Capability cap, uint8_t type);

/* Run the directory message loop forever (does not return) */
void dir_main_loop(Actor* self, DirState* ds);

/* Generic directory actor — empty, accepts dynamic create/lookup/list */
void dir_actor_entry(Actor* self);
