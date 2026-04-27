; HoneyOS Bootloader
MBOOT_MAGIC     equ 0x1BADB002
MBOOT_FLAGS     equ 0x3
MBOOT_CHECKSUM  equ -(MBOOT_MAGIC + MBOOT_FLAGS)

section .text
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM

global _start
extern kmain

_start:
    cli
    mov esp, stack_top
    call kmain
    hlt

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
