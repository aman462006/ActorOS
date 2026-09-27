#pragma once
#include "types.h"

static ALWAYS_INLINE void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static ALWAYS_INLINE void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static ALWAYS_INLINE void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static ALWAYS_INLINE uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

static ALWAYS_INLINE uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

static ALWAYS_INLINE uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

static ALWAYS_INLINE void io_wait(void) {
    outb(0x80, 0);
}

static ALWAYS_INLINE void cli(void) {
    __asm__ volatile("cli" ::: "memory");
}

static ALWAYS_INLINE void sti(void) {
    __asm__ volatile("sti" ::: "memory");
}

static ALWAYS_INLINE void hlt(void) {
    __asm__ volatile("hlt");
}

static ALWAYS_INLINE uint64_t read_cr3(void) {
    uint64_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static ALWAYS_INLINE void write_cr3(uint64_t val) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(val) : "memory");
}

static ALWAYS_INLINE void invlpg(uintptr_t addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static ALWAYS_INLINE uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static ALWAYS_INLINE void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

/* Pause instruction — reduces power in spin loops */
static ALWAYS_INLINE void cpu_pause(void) {
    __asm__ volatile("pause" ::: "memory");
}

static ALWAYS_INLINE void memory_barrier(void) {
    __asm__ volatile("mfence" ::: "memory");
}
