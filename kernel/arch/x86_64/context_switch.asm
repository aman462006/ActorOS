; context_switch.asm  —  actor-to-actor context switch
; Windows x64 ABI:
;   actor_context_switch(CpuContext* from [rcx], CpuContext* to [rdx])
;
; CpuContext layout (must match actor.h exactly):
;   0:  rax    8:  rbx   16: rcx   24: rdx
;  32:  rsi   40:  rdi   48: rbp   56: rsp
;  64:  r8    72:  r9    80: r10   88: r11
;  96:  r12  104:  r13  112: r14  120: r15
; 128:  rip  136:  rflags
; 144:  cr3

BITS 64
section .text
global actor_context_switch

actor_context_switch:
    ; rcx = from CpuContext*
    ; rdx = to   CpuContext*

    ; Save caller-saved and callee-saved registers into 'from'
    mov [rcx +   0], rax
    mov [rcx +   8], rbx
    ; rcx itself: save the original rcx value (it's caller's arg, now overwritten)
    ; We save the *current* rcx which is the 'from' pointer — we'll fix rip on restore
    mov [rcx +  16], rcx
    mov [rcx +  24], rdx
    mov [rcx +  32], rsi
    mov [rcx +  40], rdi
    mov [rcx +  48], rbp
    ; RSP: save current RSP (after the call pushed return address)
    lea rax, [rsp + 8]          ; RSP before call (return addr is at [rsp])
    mov [rcx +  56], rax
    mov [rcx +  64], r8
    mov [rcx +  72], r9
    mov [rcx +  80], r10
    mov [rcx +  88], r11
    mov [rcx +  96], r12
    mov [rcx + 104], r13
    mov [rcx + 112], r14
    mov [rcx + 120], r15

    ; Save RIP = return address (the instruction after call actor_context_switch)
    mov rax, [rsp]
    mov [rcx + 128], rax

    ; Save RFLAGS
    pushfq
    pop rax
    mov [rcx + 136], rax

    ; Save CR3 (page table root)
    mov rax, cr3
    mov [rcx + 144], rax

    ; ── Switch to 'to' context (rdx) ──

    ; Restore CR3 first (switches address space)
    mov rax, [rdx + 144]
    mov rcx, cr3
    cmp rax, rcx
    je .same_cr3            ; skip TLB flush if same address space
    mov cr3, rax
.same_cr3:

    ; Restore RFLAGS
    mov rax, [rdx + 136]
    push rax
    popfq

    ; Restore general-purpose registers
    mov rax, [rdx +   0]
    mov rbx, [rdx +   8]
    ; rcx: restore last (we still need rdx as our 'to' pointer)
    mov rsi, [rdx +  32]
    mov rdi, [rdx +  40]
    mov rbp, [rdx +  48]
    mov r8,  [rdx +  64]
    mov r9,  [rdx +  72]
    mov r10, [rdx +  80]
    mov r11, [rdx +  88]
    mov r12, [rdx +  96]
    mov r13, [rdx + 104]
    mov r14, [rdx + 112]
    mov r15, [rdx + 120]

    ; Switch stack: load RSP from 'to' context
    mov rsp, [rdx +  56]

    ; Push new RIP onto new stack so 'ret' jumps there
    push qword [rdx + 128]

    ; Restore rcx and rdx last
    mov rcx, [rdx +  16]
    mov rdx, [rdx +  24]   ; rdx may now be garbage for caller, that's fine

    ; Jump to saved RIP of 'to' actor
    ret
