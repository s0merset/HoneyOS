CC      = gcc
CFLAGS  = -m32 -ffreestanding -O2 -fno-stack-protector -fno-builtin -nostdlib -Wall -I./include
AS      = nasm
ASFLAGS = -f elf32
LD      = ld
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

OBJS = boot.o kernel.o honeyfs.o honeyui.o editor.o disk.o

all: honeyos.iso

boot.o: boot/boot.asm
	$(AS) $(ASFLAGS) boot/boot.asm -o boot.o

kernel.o: kernel/main.c
	$(CC) $(CFLAGS) -c kernel/main.c -o kernel.o

honeyfs.o: kernel/honeyfs.c
	$(CC) $(CFLAGS) -c kernel/honeyfs.c -o honeyfs.o

honeyui.o: kernel/honeyui.c
	$(CC) $(CFLAGS) -c kernel/honeyui.c -o honeyui.o

editor.o: kernel/editor.c
	$(CC) $(CFLAGS) -c kernel/editor.c -o editor.o

disk.o: kernel/disk.c
	$(CC) $(CFLAGS) -c kernel/disk.c -o disk.o

honeyos.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o honeyos.bin $(OBJS)
	@echo "[OK] Kernel binary built"

honeyos.iso: honeyos.bin
	cp honeyos.bin isodir/boot/
	grub-mkrescue -o honeyos.iso isodir/
	@echo "[OK] Bootable ISO created"

clean:
	rm -f *.o *.bin *.iso
	rm -f isodir/boot/honeyos.bin

.PHONY: all clean
