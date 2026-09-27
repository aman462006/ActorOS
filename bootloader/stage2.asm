; stage2.asm — Extended bootloader
; Loaded at 0x7E00 by Stage1.
; Responsibilities:
;   1. Enable A20 line
;   2. Get memory map from BIOS (E820)
;   3. Load kernel from disk to 0x10000
;   4. Set up GDT + page tables
;   5. Enter 64-bit long mode
;   6. Jump to kernel_main

BITS 16
ORG 0x7E00

STAGE2_MAGIC    equ 0xB007      ; magic word at start (checked by stage1)
KERNEL_LOAD_SEG equ 0x1000      ; segment for kernel: 0x1000 * 16 = 0x10000
KERNEL_SECTORS  equ 128         ; 64KB of kernel maximum initially
KERNEL_LBA_START equ 10         ; LBA sector where kernel starts (after stage1+stage2)

; Page table addresses (well below 0x10000 so they don't clash with kernel)
PML4_ADDR   equ 0x1000
PDPT_ADDR   equ 0x2000
PD_ADDR     equ 0x3000

; Boot info struct passed to kernel_main (at 0x500, safely below stack)
BOOT_INFO_ADDR equ 0x0500

    dw STAGE2_MAGIC     ; magic checked by stage1

main:
    ; Fix up segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Save boot drive
    mov [boot_drive], dl

    ; ── Step 1: Enable A20 ──
    ; Try BIOS method first (INT 15h AX=2401h)
    mov ax, 0x2401
    int 0x15
    jnc .a20_ok

    ; Fallback: Fast A20 via port 0x92
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al

.a20_ok:
    mov si, msg_a20
    call print_str

    ; ── Step 2: Get memory size (simple method) ──
    ; INT 15h AX=E801h — get extended memory size
    xor ax, ax
    mov [BOOT_INFO_ADDR + 0],  dword 0x4B524E4C  ; magic "KRNL"
    mov word [BOOT_INFO_ADDR + 12], 0             ; reserved

    mov ax, 0xE801
    int 0x15
    jc .mem_fallback
    cmp ah, 0x86
    je .mem_fallback

    ; AX = extended memory 1MB-16MB in 1KB blocks
    ; BX = extended memory 16MB-4GB in 64KB blocks
    mov [BOOT_INFO_ADDR + 4], ax   ; mem_lower_kb (conventional)
    movzx eax, bx
    shl eax, 6                     ; convert 64KB blocks to KB
    add eax, [BOOT_INFO_ADDR + 4]
    mov [BOOT_INFO_ADDR + 8], eax  ; mem_upper_kb
    jmp .mem_done

.mem_fallback:
    ; Assume 16MB
    mov dword [BOOT_INFO_ADDR + 4], 640
    mov dword [BOOT_INFO_ADDR + 8], 15360

.mem_done:
    mov si, msg_mem
    call print_str

    ; ── Step 3: Load kernel ──
    mov si, msg_kernel
    call print_str

    ; Use INT 13h extended read (LBA) via DAP
    mov ah, 0x41        ; check extensions
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .no_ext          ; no extensions — use CHS (simplified)

    ; Extended LBA read
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .disk_error
    jmp .kernel_loaded

.no_ext:
    ; Fallback CHS read (assumes small disk, track 0, head 0, sectors 11-74)
    mov ah, 0x02
    mov al, KERNEL_SECTORS
    mov ch, 0           ; cylinder 0
    mov cl, KERNEL_LBA_START + 1  ; 1-based
    mov dh, 0
    mov dl, [boot_drive]
    mov bx, 0
    mov ax, KERNEL_LOAD_SEG
    mov es, ax
    int 0x13
    xor ax, ax
    mov es, ax
    jc .disk_error

.kernel_loaded:
    mov si, msg_ok
    call print_str

    ; ── Step 4: Enter protected mode ──
    cli

    ; Load GDT
    lgdt [gdt32_ptr]

    ; Set PE bit in CR0
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to 32-bit protected mode code
    jmp 0x08:protected_mode_entry

.disk_error:
    mov si, msg_disk_err
    call print_str
.halt16:
    hlt
    jmp .halt16

; INT 13h extended read Disk Address Packet
ALIGN 4
dap:
    db 0x10             ; size of DAP
    db 0                ; reserved
    dw KERNEL_SECTORS   ; number of sectors to read
    dw 0x0000           ; buffer offset
    dw KERNEL_LOAD_SEG  ; buffer segment → physical 0x10000
    dq KERNEL_LBA_START ; LBA start sector

; ── 32-bit GDT (temporary, for transition) ──
ALIGN 8
gdt32:
    dq 0                ; null
    ; code: present|ring0|S|exec|readable, 32-bit, 4KB gran, base=0, limit=4GB
    dq 0x00CF9A000000FFFF
    ; data: present|ring0|S|writable, 32-bit, 4KB gran
    dq 0x00CF92000000FFFF
gdt32_ptr:
    dw $ - gdt32 - 1
    dd gdt32

; ── Data ──
boot_drive:    db 0
msg_a20:       db "A20 OK", 0x0D, 0x0A, 0
msg_mem:       db "Memory OK", 0x0D, 0x0A, 0
msg_kernel:    db "Loading kernel...", 0x0D, 0x0A, 0
msg_ok:        db "Kernel OK", 0x0D, 0x0A, 0
msg_disk_err:  db "Disk error in stage2!", 0x0D, 0x0A, 0

; Real-mode print
print_str:
    push ax
    push bx
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    jmp .loop
.done:
    pop bx
    pop ax
    ret

; ═══════════════════════════════════════════════════════
; 32-BIT PROTECTED MODE CODE
; ═══════════════════════════════════════════════════════
BITS 32
protected_mode_entry:
    ; Reload data segments with 32-bit descriptor
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7C00

    ; ── Clear page tables ──
    mov edi, PML4_ADDR
    xor eax, eax
    mov ecx, 3 * 1024   ; 3 pages × 1024 dwords
    rep stosd

    ; ── Set up 4-level page tables for identity map ──
    ; PML4[0] → PDPT (identity map lower half)
    mov eax, PDPT_ADDR
    or eax, 0x03        ; present + writable
    mov [PML4_ADDR], eax

    ; PDPT[0] → PD
    mov eax, PD_ADDR
    or eax, 0x03
    mov [PDPT_ADDR], eax

    ; PD[0] → 2MB huge page at 0x000000 (kernel at 0x10000 is covered)
    mov dword [PD_ADDR +  0], 0x000083  ; 0-2MB, present+writable+huge
    ; PD[1] → 2MB huge page at 0x200000
    mov dword [PD_ADDR +  8], 0x200083  ; 2MB-4MB

    ; ── Enable PAE ──
    mov eax, cr4
    or eax, (1 << 5)    ; PAE bit
    mov cr4, eax

    ; ── Load PML4 into CR3 ──
    mov eax, PML4_ADDR
    mov cr3, eax

    ; ── Enable long mode in EFER MSR ──
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)    ; LME — long mode enable
    wrmsr

    ; ── Enable paging (also enables long mode since LME is set) ──
    mov eax, cr0
    or eax, (1 << 31)   ; PG
    mov cr0, eax

    ; ── Load 64-bit GDT ──
    lgdt [gdt64_ptr - stage2_start + 0x7E00]

    ; Far jump to 64-bit code using 64-bit code segment (selector 0x08)
    jmp 0x08:long_mode_entry - stage2_start + 0x7E00

; ── 64-bit GDT ──
ALIGN 8
gdt64:
    dq 0                ; null
    ; 64-bit code: present|ring0|S|exec|readable|L(long mode)
    dq 0x00AF9A000000FFFF
    ; 64-bit data: present|ring0|S|writable
    dq 0x00AF92000000FFFF
gdt64_ptr:
    dw $ - gdt64 - 1
    dq gdt64

; ═══════════════════════════════════════════════════════
; 64-BIT LONG MODE CODE
; ═══════════════════════════════════════════════════════
BITS 64
long_mode_entry:
    ; Load data segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up kernel stack (64KB stack, ends just below 0x10000)
    mov rsp, 0xFFF0

    ; Call kernel_main(BootInfo* boot_info)
    ; Windows x64 ABI: first arg in rcx
    mov rcx, BOOT_INFO_ADDR
    mov rax, 0x10000    ; kernel entry point
    call rax

    ; Should never return
.halt:
    hlt
    jmp .halt

stage2_start equ 0x7E00

; Pad stage2 to exactly STAGE2_SECTORS * 512 bytes
TIMES (8 * 512) - ($ - $$) db 0
