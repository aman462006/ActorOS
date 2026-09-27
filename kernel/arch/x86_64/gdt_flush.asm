; gdt_flush.asm  —  load GDT and reload all segment registers
;
; gdt_load(GdtPointer* ptr [rcx])  —  lgdt, then reload CS via far return
; Windows x64 ABI: first arg in rcx, second in rdx

BITS 64
section .text
global gdt_load
global gdt_flush

; gdt_load(GdtPointer* ptr)
;   Loads GDTR from the 10-byte pointer structure at [rcx],
;   then does a far return to reload CS with selector 0x08,
;   then reloads all other segment registers with 0x10,
;   then loads TSS with selector 0x28.
gdt_load:
    lgdt [rcx]

    ; Far return to flush CS pipeline
    lea  rax, [rel .reload_cs]
    push qword 0x08         ; new CS selector
    push rax                ; new RIP
    retfq

.reload_cs:
    mov ax, 0x10            ; kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov ax, 0x28            ; TSS selector
    ltr ax
    ret

; gdt_flush(code_sel [rcx], data_sel [rdx])  —  legacy alias, not used
gdt_flush:
    mov ax, dx
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
