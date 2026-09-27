#include "../include/actor.h"
#include "../include/mm.h"

int cap_insert(Actor* a, Capability cap) {
    for (int i = 0; i < CAP_TABLE_SIZE; i++) {
        if (!a->caps.valid[i]) {
            a->caps.entries[i] = cap;
            a->caps.valid[i]   = true;
            return i;
        }
    }
    return -1;  /* cap table full */
}

bool cap_get(Actor* a, uint32_t idx, Capability* out) {
    if (idx >= CAP_TABLE_SIZE) return false;
    if (!a->caps.valid[idx]) return false;
    *out = a->caps.entries[idx];
    return true;
}

void cap_revoke(Actor* a, uint32_t idx) {
    if (idx >= CAP_TABLE_SIZE) return;
    a->caps.valid[idx] = false;
}

bool cap_validate(Actor* a, uint32_t cap_idx, uint64_t target_id) {
    if (cap_idx >= CAP_TABLE_SIZE) return false;
    if (!a->caps.valid[cap_idx]) return false;

    Capability* cap = &a->caps.entries[cap_idx];
    if (cap->actor_id != target_id) return false;

    /* Find target and check generation */
    for (int i = 0; i < MAX_ACTORS; i++) {
        if (actor_table[i] && actor_table[i]->id == target_id) {
            return cap->generation == actor_table[i]->generation;
        }
    }
    return false;  /* target not found */
}
