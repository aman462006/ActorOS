; entry.asm — Kernel entry point (64-bit)
; This is the FIRST code executed when stage2 jumps to 0x10000.
; Responsibilities: zero BSS, call kernel_main.
; Windows x64 ABI: BootInfo* in rcx (passed from stage2)

BITS 64

; Must be placed at start of kernel binary — linker puts .text.entry first
section .text.entry
global kernel_entry

extern kernel_main
extern __bss_start
extern __bss_end

kernel_entry:
    ; Save rcx (boot_info pointer) — we need it after BSS clear
    push rcx

    ; Zero BSS section
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    test rcx, rcx
    jle .bss_done
    xor al, al
    rep stosb

.bss_done:
    ; Restore boot_info and call kernel_main(boot_info)
    pop rcx
    call kernel_main

    ; kernel_main should never return — halt if it does
.halt:
    hlt
    jmp .halt
