ENTRY(boot_entry)

_kernel_start = 0xFFFFFFFF80100000;

SECTIONS
{
    /* The bootloader loads us at 1MB physical */
    . = 0x100000;
    
    .boot :
    {
        KEEP(*(.multiboot_header))
        *boot.o(*)
    }

    /* Page align the end of the boot bootstrap code */
    . = ALIGN(4096);

    /* Relocate location counter to the higher half, continuing contiguously */
    . = 0xFFFFFFFF80000000 + .;

    .text : AT(ADDR(.text) - 0xFFFFFFFF80000000)
    {
        *(.text)
        *(.text.*)
    }

    .rdata : AT(ADDR(.rdata) - 0xFFFFFFFF80000000)
    {
        *(.rodata)
        *(.rodata.*)
        *(.rdata)
        *(.rdata.*)
    }

    .data : AT(ADDR(.data) - 0xFFFFFFFF80000000)
    {
        *(.data)
        *(.data.*)
    }

    .bss : AT(ADDR(.bss) - 0xFFFFFFFF80000000)
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

    _kernel_end = .;
}
