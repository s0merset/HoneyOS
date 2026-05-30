# ============================================================
# HoneyOS Makefile
# CMSC 125 — Operating Systems, UP Cebu
#
# Builds the HoneyOS kernel into a bootable ISO using GRUB.
# Requires: nasm, gcc (with multilib), grub-mkrescue, xorriso
#
# Targets:
#   make            — Build honeyos.iso (default)
#   make clean      — Remove all build artifacts
# ============================================================

# ── Compiler Settings ──
CC      = gcc

# Compiler flags for a 32-bit freestanding kernel:
#   -m32                — Generate 32-bit x86 code
#   -ffreestanding      — No standard library assumed; no implicit main()
#   -O2                 — Optimize for speed
#   -fno-stack-protector — Disable stack canary (no stdlib to support it)
#   -fno-builtin        — Do not use GCC built-in substitutions for stdlib functions
#   -nostdlib           — Do not link against the C standard library
#   -Wall               — Enable all standard compiler warnings
#   -I./include         — Search the include/ directory for header files
CFLAGS  = -m32 -ffreestanding -O2 -fno-stack-protector -fno-builtin -nostdlib -Wall -I./include

# ── Assembler Settings ──
AS      = nasm

# -f elf32 — Output a 32-bit ELF object file compatible with our linker
ASFLAGS = -f elf32

# ── Linker Settings ──
LD      = ld

# -m elf_i386  — Link for 32-bit x86 ELF target
# -T linker.ld — Use our custom linker script to control memory layout
# -nostdlib    — Do not link standard startup files or libraries
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

# ── Object Files ──
# All compiled units that get linked together into the final kernel binary
OBJS = boot.o kernel.o honeyfs.o honeyui.o editor.o disk.o

# ── Default Target ──
all: honeyos.iso

# ── Assemble Boot Entry Point ──
# boot.asm is the first code GRUB runs; it sets up the stack and calls kmain()
boot.o: boot/boot.asm
	$(AS) $(ASFLAGS) boot/boot.asm -o boot.o

# ── Compile Kernel Entry (main.c) ──
# Contains kmain(), the shell, menu screens, and command dispatcher
kernel.o: kernel/main.c
	$(CC) $(CFLAGS) -c kernel/main.c -o kernel.o

# ── Compile FAT32 Filesystem Driver ──
# Handles all file creation, reading, writing, deletion, and listing
honeyfs.o: kernel/honeyfs.c
	$(CC) $(CFLAGS) -c kernel/honeyfs.c -o honeyfs.o

# ── Compile VGA UI Library ──
# Provides all screen drawing primitives used by every HoneyOS screen
honeyui.o: kernel/honeyui.c
	$(CC) $(CFLAGS) -c kernel/honeyui.c -o honeyui.o

# ── Compile Text Editor ──
# HoneyEdit — the built-in full-screen text editor (opened via the edit command)
editor.o: kernel/editor.c
	$(CC) $(CFLAGS) -c kernel/editor.c -o editor.o

# ── Compile ATA Disk Driver ──
# Low-level ATA PIO driver that reads/writes raw 512-byte sectors via x86 I/O ports
disk.o: kernel/disk.c
	$(CC) $(CFLAGS) -c kernel/disk.c -o disk.o

# ── Link Kernel Binary ──
# Combines all object files into a flat binary using linker.ld's memory layout
honeyos.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o honeyos.bin $(OBJS)
	@echo "[OK] Kernel binary built"

# ── Build Bootable ISO ──
# Copies the kernel binary into the ISO directory tree and packages it with GRUB
honeyos.iso: honeyos.bin
	cp honeyos.bin isodir/boot/
	grub-mkrescue -o honeyos.iso isodir/
	@echo "[OK] Bootable ISO created"

# ── Clean Build Artifacts ──
# Removes all object files, binaries, and the ISO
clean:
	rm -f *.o *.bin *.iso
	rm -f isodir/boot/honeyos.bin

.PHONY: all clean
