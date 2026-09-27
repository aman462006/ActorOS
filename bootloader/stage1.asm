; stage1.asm — Master Boot Record (exactly 512 bytes)
; BIOS loads this at 0x7C00 in real mode (16-bit)
; We relocate to 0x0600, then load Stage2 and jump to it.

BITS 16
ORG 0x7C00

STAGE1_RELOC    equ 0x0600   ; where we relocate ourselves
STAGE2_LOAD     equ 0x7E00   ; where stage2 is loaded
STAGE2_SECTORS  equ 8        ; how many 512-byte sectors = 4KB

start:
    ; Disable interrupts during setup
    cli

    ; Normalise CS:IP — BIOS may jump with different segments
    jmp 0x0000:.normalise
.normalise:

    ; Set up segment registers to zero
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00      ; stack grows down from where we sit

    sti

    ; Save boot drive number (BIOS puts it in DL)
    mov [boot_drive], dl

    ; Relocate stage1 from 0x7C00 to 0x0600 so we don't get overwritten
    mov si, 0x7C00
    mov di, STAGE1_RELOC
    mov cx, 256         ; 256 words = 512 bytes
    rep movsw

    ; Jump to relocated code
    jmp 0x0000:(STAGE1_RELOC + .after_reloc - start)

.after_reloc:
    ; Update segment pointers after relocation
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; Print status
    mov si, msg_loading
    call print_str

    ; Load Stage2 from disk
    ; LBA sector 1 (right after MBR), 8 sectors, load at 0x7E00
    mov ah, 0x02        ; INT 13h: read sectors
    mov al, STAGE2_SECTORS
    mov ch, 0           ; cylinder 0
    mov cl, 2           ; sector 2 (1-based: MBR=sector 1, Stage2 starts sector 2)
    mov dh, 0           ; head 0
    mov dl, [boot_drive + STAGE1_RELOC - start]
    mov bx, STAGE2_LOAD
    int 0x13
    jc .disk_error

    ; Verify Stage2 magic
    cmp word [STAGE2_LOAD], 0xB007  ; our stage2 magic
    jne .bad_magic

    ; Jump to Stage2
    jmp STAGE2_LOAD

.disk_error:
    mov si, msg_disk_err
    call print_str
    jmp .halt

.bad_magic:
    mov si, msg_bad_magic
    call print_str

.halt:
    hlt
    jmp .halt

; ── Subroutines ──

print_str:   ; SI = null-terminated string (BIOS teletype output)
    push ax
    push bx
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .loop
.done:
    pop bx
    pop ax
    ret

; ── Data ──
boot_drive:   db 0
msg_loading:  db "Loading ActorOS...", 0x0D, 0x0A, 0
msg_disk_err: db "Disk error!", 0x0D, 0x0A, 0
msg_bad_magic:db "Bad stage2!", 0x0D, 0x0A, 0

; ── Pad to 510 bytes and add boot signature ──
TIMES 510 - ($ - $$) db 0
dw 0xAA55
