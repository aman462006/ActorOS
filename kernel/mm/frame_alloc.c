#include "../include/mm.h"
#include "../include/types.h"

/* Bitmap physical frame allocator.
   One bit per 4KB frame — bit 1 = free, 0 = used. */

#define MAX_FRAMES (64 * 1024)              /* supports up to 256MB */
#define BITMAP_WORDS (MAX_FRAMES / 64)

static uint64_t bitmap[BITMAP_WORDS];
static uint64_t total_frames;
static uint64_t free_frames;
static uint64_t mem_base;

static void bitmap_set_free(uint64_t frame) {
    bitmap[frame / 64] |= (1ULL << (frame % 64));
}

static void bitmap_set_used(uint64_t frame) {
    bitmap[frame / 64] &= ~(1ULL << (frame % 64));
}

static bool bitmap_is_free(uint64_t frame) {
    return (bitmap[frame / 64] >> (frame % 64)) & 1;
}

void frame_alloc_init(uint64_t mem_start, uint64_t mem_end) {
    mem_base = ALIGN_UP(mem_start, PAGE_SIZE);
    uint64_t mem_top = ALIGN_DOWN(mem_end, PAGE_SIZE);

    total_frames = (mem_top - mem_base) / PAGE_SIZE;
    if (total_frames > MAX_FRAMES) total_frames = MAX_FRAMES;

    /* Mark all as free initially */
    for (uint64_t i = 0; i < BITMAP_WORDS; i++) bitmap[i] = 0;
    for (uint64_t i = 0; i < total_frames; i++) bitmap_set_free(i);
    free_frames = total_frames;

    extern void serial_puts(const char*);
    extern void serial_putdec(uint64_t);
    serial_puts("[mm] frame allocator: ");
    serial_putdec(total_frames);
    serial_puts(" frames (");
    serial_putdec(total_frames * 4);
    serial_puts(" KB)\n");
}

uintptr_t frame_alloc(void) {
    if (free_frames == 0) return 0;

    /* Linear scan — find first free bit */
    for (uint64_t w = 0; w < BITMAP_WORDS; w++) {
        if (bitmap[w] == 0) continue;
        /* Found a word with at least one free bit */
        int bit = __builtin_ctzll(bitmap[w]);
        uint64_t frame = w * 64 + bit;
        if (frame >= total_frames) return 0;
        bitmap_set_used(frame);
        free_frames--;
        return mem_base + frame * PAGE_SIZE;
    }
    return 0;  /* out of memory */
}

void frame_free(uintptr_t phys) {
    if (phys < mem_base) return;
    uint64_t frame = (phys - mem_base) / PAGE_SIZE;
    if (frame >= total_frames) return;
    if (bitmap_is_free(frame)) return;  /* double-free guard */
    bitmap_set_free(frame);
    free_frames++;
}

uint64_t frame_alloc_free_count(void) {
    return free_frames;
}
