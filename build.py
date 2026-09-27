#!/usr/bin/env python3
"""
ActorOS build helper.
Usage:
  python build.py create-image <output.img> <stage1.bin> <stage2.bin> <kernel.bin>
  python build.py check-tools
"""

import sys
import os
import struct
import subprocess
import shutil

SECTOR_SIZE = 512

# Disk layout:
#   Sector  0       : Stage1 (MBR, 512 bytes)
#   Sectors 1-8     : Stage2 (4KB = 8 sectors)
#   Sectors 9-138   : reserved gap
#   Sector  10      : Kernel binary (up to 64KB = 128 sectors)
#
# Must match constants in stage2.asm:
#   KERNEL_LBA_START equ 10
#   STAGE2_SECTORS   equ 8

STAGE1_LBA   = 0
STAGE2_LBA   = 1
STAGE2_SECTS = 8
KERNEL_LBA   = 10
DISK_SIZE    = 2 * 1024 * 1024  # 2MB disk image


def create_image(output, stage1_path, stage2_path, kernel_path):
    with open(stage1_path, 'rb') as f:
        stage1 = f.read()
    with open(stage2_path, 'rb') as f:
        stage2 = f.read()
    with open(kernel_path, 'rb') as f:
        kernel = f.read()

    # Validate stage1
    if len(stage1) != SECTOR_SIZE:
        print(f"ERROR: stage1 is {len(stage1)} bytes, expected {SECTOR_SIZE}")
        sys.exit(1)
    if stage1[-2:] != b'\x55\xAA':
        print("ERROR: stage1 missing boot signature 0x55AA")
        sys.exit(1)

    # Validate stage2
    if stage2[:2] != b'\x07\xB0':  # little-endian 0xB007
        print(f"WARNING: stage2 magic mismatch (got {stage2[:2].hex()})")

    # Create blank disk image
    disk = bytearray(DISK_SIZE)

    # Write stage1 at sector 0
    disk[STAGE1_LBA * SECTOR_SIZE : STAGE1_LBA * SECTOR_SIZE + len(stage1)] = stage1

    # Write stage2 at sector 1
    s2_start = STAGE2_LBA * SECTOR_SIZE
    disk[s2_start : s2_start + len(stage2)] = stage2

    # Write kernel at sector 10
    k_start = KERNEL_LBA * SECTOR_SIZE
    if k_start + len(kernel) > DISK_SIZE:
        print(f"ERROR: Kernel too large ({len(kernel)} bytes, max {DISK_SIZE - k_start})")
        sys.exit(1)
    disk[k_start : k_start + len(kernel)] = kernel

    with open(output, 'wb') as f:
        f.write(disk)

    print(f"  Image: {output}")
    print(f"    Stage1:  {len(stage1):6,} bytes at LBA {STAGE1_LBA}")
    print(f"    Stage2:  {len(stage2):6,} bytes at LBA {STAGE2_LBA}")
    print(f"    Kernel:  {len(kernel):6,} bytes at LBA {KERNEL_LBA}")
    print(f"    Total:   {DISK_SIZE:6,} bytes ({DISK_SIZE // 1024} KB)")


def check_tools():
    required = {
        'nasm':              ['nasm', '--version'],
        'gcc':               ['gcc', '--version'],
        'ld':                ['ld',  '--version'],
        'objcopy':           ['objcopy', '--version'],
        'qemu-system-x86_64': ['qemu-system-x86_64', '--version'],
    }
    optional = {
        'qemu-system-x86_64': 'Install from https://www.qemu.org/download/#windows',
    }

    all_ok = True
    print("Checking build tools:")
    for name, cmd in required.items():
        found = shutil.which(cmd[0]) is not None
        status = "OK" if found else "MISSING"
        if not found:
            all_ok = False
            hint = optional.get(name, '')
            print(f"  [{status}] {name}" + (f" — {hint}" if hint else ""))
        else:
            try:
                r = subprocess.run(cmd, capture_output=True, text=True)
                version = r.stdout.split('\n')[0] if r.stdout else r.stderr.split('\n')[0]
                print(f"  [ OK ] {name}: {version.strip()[:60]}")
            except Exception:
                print(f"  [ OK ] {name}")

    if not all_ok:
        print("\nInstall missing tools:")
        print("  NASM:  https://www.nasm.us/pub/nasm/releasebuilds/")
        print("  MinGW: https://winlibs.com/ (already installed via WinGet)")
        print("  QEMU:  https://www.qemu.org/download/#windows")
        print("         or: winget install SoftwareFreedomConservancy.QEMU")
    return all_ok


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    cmd = sys.argv[1]
    if cmd == 'create-image':
        if len(sys.argv) != 6:
            print("Usage: python build.py create-image <output> <stage1> <stage2> <kernel>")
            sys.exit(1)
        create_image(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif cmd == 'check-tools':
        ok = check_tools()
        sys.exit(0 if ok else 1)
    else:
        print(f"Unknown command: {cmd}")
        sys.exit(1)


if __name__ == '__main__':
    main()
