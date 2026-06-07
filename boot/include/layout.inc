; LiquidOS boot image layout.
;
; Stage 1 is the 512-byte BIOS boot sector at logical sector 1.
; Stage 2 starts immediately after it at logical sector 2.
;
; The image uses fixed sectors on purpose. That keeps the beginner boot path
; easy to debug before a real filesystem driver exists.

%define SECTOR_SIZE          512
%define STAGE2_LOAD_SEGMENT  0x0000
%define STAGE2_LOAD_OFFSET   0x8000
%define STAGE2_START_SECTOR  2
%define STAGE2_SECTOR_COUNT  16

%define KERNEL_START_SECTOR  (STAGE2_START_SECTOR + STAGE2_SECTOR_COUNT)
%define KERNEL_TEMP_SEGMENT  0x1000
%define KERNEL_TEMP_ADDRESS  0x00010000
%define KERNEL_LOAD_ADDRESS  0x00100000
%define KERNEL_MAX_SECTORS   1024

%define BOOT_INFO_ADDRESS    0x00007000
%define VBE_MODE_INFO_ADDR   0x00005000

%define PAGE_PML4_ADDRESS    0x00090000
%define PAGE_PDPT_ADDRESS    0x00091000
%define PAGE_PD0_ADDRESS     0x00092000
%define PAGE_PD1_ADDRESS     0x00093000
%define PAGE_PD2_ADDRESS     0x00094000
%define PAGE_PD3_ADDRESS     0x00095000
