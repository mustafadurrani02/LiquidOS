ENTRY(kernel_entry)

SECTIONS
{
    . = 1M;

    .text : ALIGN(4K)
    {
        KEEP(*(.text.entry))
        *(.text*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    }

    .data : ALIGN(4K)
    {
        *(.data*)
    }

    .bss : ALIGN(4K)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    __kernel_end = ALIGN(4K);
}
