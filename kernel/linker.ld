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

    __bss_start = .;

    .stack (NOLOAD) : ALIGN(4K)
    {
        *(.stack*)
    }

    .dma (NOLOAD) : ALIGN(4K)
    {
        *(.dma*)
    }

    .bss (NOLOAD) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    __kernel_end = ALIGN(4K);
}
