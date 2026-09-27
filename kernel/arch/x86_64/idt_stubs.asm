; idt_stubs.asm  —  256 interrupt entry points
; Each stub saves all general-purpose registers, then calls interrupt_dispatch(frame*)
; Windows x64 ABI: first arg in rcx

BITS 64
section .text

extern interrupt_dispatch

; Common handler: all registers are already on stack, int_no and error_code pushed
isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Windows x64 ABI: first arg = rcx = pointer to InterruptFrame
    mov rcx, rsp
    ; Align stack to 16 bytes (Windows ABI requires this before call)
    sub rsp, 32         ; shadow space
    and rsp, ~15
    call interrupt_dispatch
    ; Restore RSP (we modified it for alignment + shadow)
    mov rsp, rcx        ; rcx still points to frame top

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Remove int_no and error_code from stack
    add rsp, 16
    iretq

; Macro for interrupts WITHOUT error code — push dummy 0
%macro ISR_NO_ERR 1
global isr%1
isr%1:
    push qword 0        ; dummy error code
    push qword %1       ; interrupt number
    jmp isr_common
%endmacro

; Macro for interrupts WITH error code pushed by CPU
%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1       ; interrupt number (error code already on stack from CPU)
    jmp isr_common
%endmacro

; CPU exceptions 0-31
ISR_NO_ERR 0    ; Divide by zero
ISR_NO_ERR 1    ; Debug
ISR_NO_ERR 2    ; NMI
ISR_NO_ERR 3    ; Breakpoint
ISR_NO_ERR 4    ; Overflow
ISR_NO_ERR 5    ; Bound range exceeded
ISR_NO_ERR 6    ; Invalid opcode
ISR_NO_ERR 7    ; Device not available
ISR_ERR    8    ; Double fault (error code = 0, but CPU pushes it)
ISR_NO_ERR 9    ; Coprocessor segment overrun
ISR_ERR    10   ; Invalid TSS
ISR_ERR    11   ; Segment not present
ISR_ERR    12   ; Stack-segment fault
ISR_ERR    13   ; General protection fault
ISR_ERR    14   ; Page fault
ISR_NO_ERR 15   ; Reserved
ISR_NO_ERR 16   ; x87 FPU error
ISR_ERR    17   ; Alignment check
ISR_NO_ERR 18   ; Machine check
ISR_NO_ERR 19   ; SIMD FP exception
ISR_NO_ERR 20   ; Virtualization exception
ISR_NO_ERR 21   ; Control protection exception
ISR_NO_ERR 22
ISR_NO_ERR 23
ISR_NO_ERR 24
ISR_NO_ERR 25
ISR_NO_ERR 26
ISR_NO_ERR 27
ISR_NO_ERR 28
ISR_NO_ERR 29
ISR_ERR    30   ; Security exception
ISR_NO_ERR 31

; Hardware IRQs (32-47 with PIC, 32+ with APIC)
%assign i 32
%rep 224
ISR_NO_ERR i
%assign i i+1
%endrep

; Export table so idt.c can register them
section .data
global isr_table
isr_table:
%assign i 0
%rep 256
    dq isr%+i
%assign i i+1
%endrep
