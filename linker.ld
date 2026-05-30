/* ============================================================
 * linker.ld — HoneyOS Kernel Linker Script
 * CMSC 125 — Operating Systems, UP Cebu
 *
 * Controls how the linker arranges the kernel's sections in memory.
 * GRUB loads the kernel at physical address 0x100000 (1 MiB),
 * which is the standard Multiboot load address for x86 kernels.
 * ============================================================ */

/* The kernel entry point — must match "global _start" in boot/boot.asm */
ENTRY(_start)

SECTIONS {
    /* Start placing sections at 1 MiB — the standard Multiboot load address.
     * Below 1 MiB is reserved for BIOS, VGA memory, and legacy hardware. */
    . = 0x100000;

    /* ── .text — Executable Code ──
     * All machine instructions from boot.asm and every compiled .c file.
     * The Multiboot header in boot.asm must land within the first 8KB
     * of the binary so GRUB can locate it during boot. */
    .text : {
        *(.text)
    }

    /* ── .data — Initialized Read/Write Data ──
     * Global and static variables that have explicit initial values.
     * Example: the HONEYUI_THEME color struct in honeyui.c. */
    .data : {
        *(.data)
    }

    /* ── .bss — Uninitialized / Zero-Initialized Data ──
     * Variables declared but not explicitly initialized (defaulted to 0).
     * The kernel stack (stack_bottom/stack_top in boot.asm) lives here.
     * No space is consumed in the binary file — the bootloader zeroes this region. */
    .bss : {
        *(.bss)
    }
}
