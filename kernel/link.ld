/*
 * ActorOS Kernel Linker Script
 * Output: flat binary loaded at physical 0x10000
 */

ENTRY(kernel_entry)

SECTIONS
{
    /* Kernel loaded at 64KB physical */
    . = 0x10000;

    /* Entry stub MUST be first — stage2 jumps to 0x10000 */
    .text.entry :
    {
        *(.text.entry)
    }

    /* All other code */
    .text :
    {
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    . = ALIGN(16);
    .rodata :
    {
        *(.rodata)
        *(.rodata.*)
        *(.rdata)       /* MinGW puts const strings here */
    }

    /* Writable data */
    . = ALIGN(16);
    .data :
    {
        *(.data)
        *(.data.*)
    }

    /* BSS — zeroed by entry.asm */
    . = ALIGN(16);
    __bss_start = .;
    .bss :
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    __bss_end = .;

    /* Discard sections that make no sense in a flat binary */
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.note*)
        *(.pdata)
        *(.xdata)
        *(.drectve)
        *(.idata*)
        *(.edata*)
        *(.reloc)
        *(.debug*)
    }
}
