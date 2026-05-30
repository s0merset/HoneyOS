# ============================================================
# HoneyOS Makefile
# CMSC 125 — Operating Systems, UP Cebu
#
# Builds the HoneyOS kernel into a bootable ISO using GRUB.
# Requires: nasm, gcc (i686-elf or multilib), grub-mkrescue, xorriso
#
# Targets:
#   make honeyos.iso   — Build the bootable ISO (default)
#   make clean         — Remove all build artifacts
# ============================================================

# ── Compiler and Assembler Settings ──
CC      = gcc
AS      = nasm
LD      = ld

# Compiler flags for a 32-bit freestanding kernel:
#   -m32              — Generate 32-bit x86 code
#   -ffreestanding    — No standard library, no main() entry point assumption
#   -O2               — Optimize for speed (safe for kernel code)
#   -Wall -Wextra     — Enable all warnings to catch bugs early
#   -Iinclude         — Look in the include/ folder for header files
CFLAGS  = -m32 -ffreestanding -O2 -Wall -Wextra -Iinclude

# Assembler flags:
#   -f elf32          — Output 32-bit ELF object file (compatible with our linker)
ASFLAGS = -f elf32

# Linker flags:
#   -m elf_i386       — Link for 32-bit x86 ELF target
#   -T linker.ld      — Use our custom linker script to control memory layout
#   --oformat binary  — (Not used here, but linker.ld controls the output format)
LDFLAGS = -m elf_i386 -T linker.ld

# ── Source Files ──
# All C source files in the kernel/ directory
C_SOURCES = kernel/main.c kernel/disk.c kernel/honeyfs.c kernel/honeyui.c kernel/editor.c

# The boot assembly entry point
ASM_SOURCES = boot/boot.asm

# ── Object Files (compiled into build/) ──
C_OBJECTS   = $(C_SOURCES:kernel/%.c=build/%.o)
ASM_OBJECTS = build/boot.o

# ── Default Target ──
# Running "make" with no arguments builds the ISO
all: honeyos.iso

# ── Build the Bootable ISO ──
# grub-mkrescue packages the kernel ELF and grub.cfg into a bootable ISO
honeyos.iso: kernel.elf
	mkdir -p isodir/boot/grub
	cp kernel.elf isodir/boot/kernel.elf
	cp isodir/boot/grub/grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o honeyos.iso isodir

# ── Link the Kernel ELF ──
# Combines all object files into a single ELF binary using our linker script
kernel.elf: $(ASM_OBJECTS) $(C_OBJECTS)
	$(LD) $(LDFLAGS) -o kernel.elf $(ASM_OBJECTS) $(C_OBJECTS)

# ── Compile C Source Files ──
# Each .c file in kernel/ becomes a .o file in build/
build/%.o: kernel/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

# ── Assemble Boot Entry Point ──
build/boot.o: boot/boot.asm | build
	$(AS) $(ASFLAGS) boot/boot.asm -o build/boot.o

# ── Create build/ Directory If Missing ──
build:
	mkdir -p build

# ── Clean Build Artifacts ──
# Removes compiled objects, the kernel ELF, and the ISO
clean:
	rm -rf build/ kernel.elf honeyos.iso

.PHONY: all clean
