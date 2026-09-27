#include "../include/mm.h"
#include "../include/types.h"

/* Simple free-list heap allocator.
   Each allocation has a small header immediately before the user data. */

#define HEAP_MAGIC 0xDEADBEEFCAFEBABEULL

typedef struct Block {
    uint64_t    magic;
    size_t      size;       /* usable bytes (not including header) */
    bool        free;
    struct Block* next;
    struct Block* prev;
} Block;

#define BLOCK_HEADER_SIZE (sizeof(Block))
#define MIN_BLOCK_SIZE    32

static Block* heap_head = NULL;
static uintptr_t heap_end_addr = 0;
static uintptr_t heap_limit = 0;

void heap_init(uintptr_t start, size_t size) {
    heap_head = (Block*)start;
    heap_end_addr = start + BLOCK_HEADER_SIZE;
    heap_limit = start + size;

    heap_head->magic = HEAP_MAGIC;
    heap_head->size  = size - BLOCK_HEADER_SIZE;
    heap_head->free  = true;
    heap_head->next  = NULL;
    heap_head->prev  = NULL;
}

static void split_block(Block* b, size_t needed) {
    if (b->size < needed + BLOCK_HEADER_SIZE + MIN_BLOCK_SIZE) return;

    Block* new_block = (Block*)((uint8_t*)b + BLOCK_HEADER_SIZE + needed);
    new_block->magic = HEAP_MAGIC;
    new_block->size  = b->size - needed - BLOCK_HEADER_SIZE;
    new_block->free  = true;
    new_block->next  = b->next;
    new_block->prev  = b;

    if (b->next) b->next->prev = new_block;
    b->next = new_block;
    b->size = needed;
}

static void merge_block(Block* b) {
    /* Merge with next if both free */
    while (b->next && b->next->free) {
        b->size += BLOCK_HEADER_SIZE + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN_UP(size, 8);

    Block* b = heap_head;
    while (b) {
        if (b->magic != HEAP_MAGIC) return NULL;  /* heap corruption */
        if (b->free && b->size >= size) {
            split_block(b, size);
            b->free = false;
            return (void*)((uint8_t*)b + BLOCK_HEADER_SIZE);
        }
        b = b->next;
    }
    return NULL;  /* out of heap */
}

void* kzalloc(size_t size) {
    void* p = kmalloc(size);
    if (p) kmemset(p, 0, size);
    return p;
}

void kfree(void* ptr) {
    if (!ptr) return;
    Block* b = (Block*)((uint8_t*)ptr - BLOCK_HEADER_SIZE);
    if (b->magic != HEAP_MAGIC || b->free) return;
    b->free = true;
    merge_block(b);
}

void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return NULL; }

    Block* b = (Block*)((uint8_t*)ptr - BLOCK_HEADER_SIZE);
    if (b->magic != HEAP_MAGIC) return NULL;

    if (b->size >= new_size) return ptr;

    void* new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;
    kmemcpy(new_ptr, ptr, b->size < new_size ? b->size : new_size);
    kfree(ptr);
    return new_ptr;
}

/* ── Memory utilities ── */
void* kmemset(void* dst, int val, size_t len) {
    uint8_t* d = (uint8_t*)dst;
    while (len--) *d++ = (uint8_t)val;
    return dst;
}

void* kmemcpy(void* dst, const void* src, size_t len) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (len--) *d++ = *s++;
    return dst;
}

int kmemcmp(const void* a, const void* b, size_t len) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    while (len--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}
