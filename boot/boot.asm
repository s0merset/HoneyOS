; ============================================================
; HoneyOS Bootloader Entry Point - boot.asm
; CMSC 125 - Operating Systems, University of the Philippines Cebu
;
; This file is the first code that runs when HoneyOS boots.
; It satisfies the Multiboot specification so GRUB can load
; the kernel, sets up an initial kernel stack, then hands
; control to kmain() in kernel/main.c.
; ============================================================

; ── Multiboot Header Constants ──
; GRUB looks for this magic signature in the first 8KB of the binary.
MBOOT_MAGIC     equ 0x1BADB002      ; Magic number GRUB expects to find
MBOOT_FLAGS     equ 0x3             ; Flags: bit 0 = align modules, bit 1 = provide memory map
MBOOT_CHECKSUM  equ -(MBOOT_MAGIC + MBOOT_FLAGS)  ; Checksum: magic + flags + checksum must == 0

section .text
align 4
    ; ── Multiboot Header ──
    ; Must be 4-byte aligned and appear early in the binary.
    dd MBOOT_MAGIC        ; Magic value GRUB scans for
    dd MBOOT_FLAGS        ; Feature flags requested from bootloader
    dd MBOOT_CHECKSUM     ; Verification checksum

global _start    ; Expose _start as the kernel entry point for the linker
extern kmain     ; kmain() is defined in kernel/main.c

_start:
    cli
    mov esp, stack_top    ; Disable hardware interrupts — we're not ready to handle them yet
    call kmain            ; Jump into the C kernel — kmain() takes over from here
    hlt                   ; If kmain ever returns (it shouldn't), halt the CPU

; ── Kernel Stack ──
; Reserved in the BSS segment (zero-initialized, no space in binary).
; 16KB is more than enough for HoneyOS's shallow call stack.
section .bss
align 16            ; x86 requires 16-byte stack alignment for some instructions
stack_bottom:
    resb 16384      ; Reserve 16,384 bytes (16KB) for the kernel stack 
stack_top:          ; esp is set to this label — stack grows downward toward stack_bottom
