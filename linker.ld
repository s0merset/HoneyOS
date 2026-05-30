/* ============================================================
 * linker.ld — HoneyOS Kernel Linker Script
 * CMSC 125 — Operating Systems, UP Cebu
 *
 * This script tells the linker exactly how to lay out the kernel
 * binary in memory. GRUB loads the kernel at physical address
 * 0x100000 (1 MiB), which is the standard load address for
 * Multiboot-compliant kernels on x86.
 *
 * The Multiboot header (in boot.asm) must appear within the first
 * 8KB of the binary, so the .text section comes first.
 * ============================================================ */

/* The entry point symbol — must match "global _start" in boot.asm */
ENTRY(_start)

SECTIONS
{
    /* Load the kernel starting at physical address 1 MiB (0x100000).
     * The "." is the location counter — it advances as sections are placed. */
    . = 0x100000;

    /* ── .text — Executable Code ──
     * Contains all machine instructions: boot.asm entry point,
     * and all compiled C functions (kmain, fs_init, etc.).
     * KEEP ensures the Multiboot header is not removed by the linker's
     * dead-code elimination even if it has no callers. */
    .text :
    {
        *(.multiboot)   /* Multiboot header — must be first for GRUB to find it */
        *(.text)        /* All code sections from all object files */
    }

    /* ── .rodata — Read-Only Data ──
     * String literals, const arrays (e.g., scancode_map, HONEYUI_THEME).
     * Aligned to 4-byte boundary. */
    . = ALIGN(4);
    .rodata :
    {
        *(.rodata)
    }

    /* ── .data — Initialized Read/Write Data ──
     * Global and static variables that have an explicit initial value.
     * Example: ui_cursor_x = 0 in honeyui.c. */
    . = ALIGN(4);
    .data :
    {
        *(.data)
    }

    /* ── .bss — Uninitialized / Zero-Initialized Data ──
     * The kernel stack (stack_bottom/stack_top in boot.asm) lives here.
     * The bootloader zeros this region before _start runs, so no disk
     * space is used for it in the ISO.
     * COMMON holds tentative definitions (uninitialized globals in C). */
    . = ALIGN(16);
    .bss :
    {
        *(COMMON)
        *(.bss)
    }
}
