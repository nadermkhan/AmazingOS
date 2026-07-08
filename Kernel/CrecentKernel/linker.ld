ENTRY(boot_entry)

SECTIONS
{
    /* The kernel is loaded at 1MB */
    . = 0x100000;

    .text :
    {
        /* Place the multiboot header first */
        KEEP(*(.multiboot_header))
        *(.text)
        *(.text.*)
    }

    .rdata :
    {
        *(.rodata)
        *(.rodata.*)
        *(.rdata)
        *(.rdata.*)
    }

    .data :
    {
        *(.data)
        *(.data.*)
    }

    .bss :
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.pdata)
        *(.xdata)
        *(.eh_frame)
        *(.note.GNU-stack)
    }
}
