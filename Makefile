# ActorOS Makefile
# Toolchain: NASM + MinGW GCC + objcopy (COFF→ELF64) + lld (from Android NDK)

CC      = gcc
AS      = nasm
OBJCOPY = objcopy
PYTHON  = python

# lld from Android NDK — supports ELF64 output on Windows
NDK_BIN = C:/Users/AMAND/AppData/Local/Android/Sdk/ndk/30.0.14904198/toolchains/llvm/prebuilt/windows-x86_64/bin
LLD     = $(NDK_BIN)/ld.lld.exe

# Compiler flags — bare-metal 64-bit, no runtime, no SIMD
CFLAGS  = -ffreestanding \
           -fno-builtin \
           -fno-stack-protector \
           -fno-pic \
           -mno-red-zone \
           -mno-mmx -mno-sse -mno-sse2 \
           -nostdlib \
           -nostdinc \
           -m64 \
           -std=gnu11 \
           -Wall -Wextra \
           -Ikernel/include \
           -O2

# NASM: produce ELF64 objects (lld can link these natively)
ASFLAGS = -f elf64

# NASM for bootloader stages: flat binary
ASFLAGS_BIN = -f bin

# lld flags: ELF64 bare-metal, flat binary output
LDFLAGS = -m elf_x86_64 \
           --oformat binary \
           -T kernel/link.ld

DISK_IMG   = actoros.img
KERNEL_BIN = kernel.bin
STAGE1_BIN = bootloader/stage1.bin
STAGE2_BIN = bootloader/stage2.bin

# ── Kernel ASM sources (compiled with NASM to ELF64) ──
KERNEL_ASM_SRC = \
    kernel/arch/x86_64/entry.asm \
    kernel/arch/x86_64/gdt_flush.asm \
    kernel/arch/x86_64/idt_stubs.asm \
    kernel/arch/x86_64/context_switch.asm

# ── Kernel C sources (compiled with GCC, converted COFF→ELF64 via objcopy) ──
KERNEL_C_SRC = \
    kernel/main.c \
    kernel/arch/x86_64/gdt.c \
    kernel/arch/x86_64/idt.c \
    kernel/mm/frame_alloc.c \
    kernel/mm/vmm.c \
    kernel/mm/heap.c \
    kernel/actor/actor.c \
    kernel/actor/capability.c \
    kernel/actor/scheduler.c \
    kernel/drivers/serial.c \
    kernel/drivers/timer.c \
    kernel/drivers/keyboard.c \
    kernel/fs/file.c \
    kernel/fs/dir.c \
    kernel/fs/ramdisk.c \
    kernel/shell/terminal.c \
    kernel/shell/shell.c \
    kernel/net/e1000.c \
    kernel/net/net.c

# Object file paths
KERNEL_ASM_OBJ = $(KERNEL_ASM_SRC:.asm=.o)
KERNEL_C_OBJ   = $(KERNEL_C_SRC:.c=.o)
# ELF64 versions of C objects (converted from COFF)
KERNEL_C_ELF   = $(KERNEL_C_SRC:.c=_elf.o)
KERNEL_OBJS    = $(KERNEL_ASM_OBJ) $(KERNEL_C_ELF)

.PHONY: all run run-debug clean check-tools

all: check-tools $(DISK_IMG)
	@echo ""
	@echo "Build complete!"
	@echo "  Disk image: $(DISK_IMG)"
	@echo "  Install QEMU and run: make run"
	@echo "  QEMU install: winget install SoftwareFreedomConservancy.QEMU"

check-tools:
	@nasm --version > NUL 2>&1 || (echo ERROR: nasm missing && exit 1)
	@gcc  --version > NUL 2>&1 || (echo ERROR: gcc missing  && exit 1)

# ── Compile C → COFF object ──
%.o: %.c
	@echo "  CC  $<"
	$(CC) $(CFLAGS) -c $< -o $@

# ── Convert COFF → ELF64, then localize MinGW .refptr.* pseudo-reloc symbols.
#    MinGW generates a .refptr.SYMBOL in each TU that references an extern symbol.
#    PE linker deduplicates these (COMDAT), but after objcopy they become global ELF
#    symbols and LLD rejects duplicates.  Making them STB_LOCAL per-object fixes it.
%_elf.o: %.o
	@echo "  ELF $<"
	$(OBJCOPY) -I pe-x86-64 -O elf64-x86-64 $< $@
	$(OBJCOPY) --wildcard --localize-symbol=".refptr.*" $@ $@

# ── Assemble kernel ASM → ELF64 directly ──
kernel/arch/x86_64/%.o: kernel/arch/x86_64/%.asm
	@echo "  AS  $<"
	$(AS) $(ASFLAGS) $< -o $@

# ── Assemble bootloader stages → flat binary ──
$(STAGE1_BIN): bootloader/stage1.asm
	@echo "  AS  $<"
	$(AS) $(ASFLAGS_BIN) $< -o $@

$(STAGE2_BIN): bootloader/stage2.asm
	@echo "  AS  $<"
	$(AS) $(ASFLAGS_BIN) $< -o $@

# ── Link kernel — entry.o must appear first ──
$(KERNEL_BIN): $(KERNEL_OBJS) kernel/link.ld
	@echo "  LD  $@"
	$(LLD) $(LDFLAGS) -o $@ \
	    kernel/arch/x86_64/entry.o \
	    kernel/arch/x86_64/gdt_flush.o \
	    kernel/arch/x86_64/idt_stubs.o \
	    kernel/arch/x86_64/context_switch.o \
	    $(KERNEL_C_ELF)
	@echo "  Kernel size: $$(python -c "import os; print(os.path.getsize('kernel.bin'), 'bytes')")"

# ── Create bootable disk image ──
$(DISK_IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	@echo "  IMG $@"
	$(PYTHON) build.py create-image $@ $^

# ── Run in QEMU (requires QEMU installed) ──
run: $(DISK_IMG)
	qemu-system-x86_64 \
	    -drive format=raw,file=$(DISK_IMG),if=ide \
	    -serial stdio \
	    -m 128M \
	    -no-reboot \
	    -no-shutdown \
	    -display none

# Run with interrupt log for debugging
run-debug: $(DISK_IMG)
	qemu-system-x86_64 \
	    -drive format=raw,file=$(DISK_IMG),if=ide \
	    -serial stdio \
	    -m 128M \
	    -no-reboot \
	    -d int,cpu_reset \
	    -D qemu.log \
	    -display none

clean:
	@echo Cleaning...
	@if exist kernel.bin del kernel.bin
	@if exist actoros.img del actoros.img
	@if exist qemu.log del qemu.log
	@for /r kernel %%f in (*.o) do del "%%f" 2>NUL
	@if exist bootloader\stage1.bin del bootloader\stage1.bin
	@if exist bootloader\stage2.bin del bootloader\stage2.bin
	@echo Done.
