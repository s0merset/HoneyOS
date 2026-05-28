# HoneyOS Kernel

### CMSC 125 - Operating Systems | Phase 2

**University of the Philippines Cebu**

HoneyOS is a small 32-bit x86 operating system kernel booted through GRUB. It now includes a persistent FAT32-backed filesystem stored on a virtual disk image and a Docker-based build workflow for generating and inspecting the bootable ISO.

## Overview

HoneyOS uses two separate virtual media devices:

| Device | File | Purpose |
| --- | --- | --- |
| Boot ISO | `honeyos.iso` | Boots GRUB and loads the HoneyOS kernel |
| FAT32 disk | `honeyfs.img` / `honeyfs.vdi` | Stores user-created files persistently |

The ISO is used only for booting. User files are written to the attached FAT32 virtual hard disk, which is why files can persist after a full VirtualBox shutdown.

## Project Structure

```text
HoneyOS/
├── boot/
│   └── boot.asm                    # Multiboot entry point
├── include/
│   ├── disk.h                      # ATA disk driver declarations
│   ├── honeyfs.h                   # FAT32 filesystem API and structs
│   ├── honeyui.h                   # VGA interface library API
│   └── keyboard.h                  # Keyboard input helpers
├── kernel/
│   ├── disk.c                      # ATA PIO read/write driver
│   ├── honeyfs.c                   # Active FAT32 filesystem implementation
│   ├── honeyui.c                   # Reusable VGA UI widgets
│   ├── main.c                      # Kernel entry, menu, shell commands
│   └── editor.c                    # Editor support
├── isodir/
│   └── boot/grub/grub.cfg          # GRUB bootloader config
├── iso-extract/                    # Extracted ISO contents
├── Dockerfile                      # Linux build environment
├── .dockerignore                   # Excludes stale host build artifacts
├── Makefile                        # Build rules
├── linker.ld                       # Kernel linker script
├── honeyos.iso                     # Bootable ISO
├── honeyfs.img                     # Raw FAT32 disk image
├── honeyfs.vdi                     # VirtualBox disk image
└── README.md
```

## Startup Interface

HoneyOS starts with a main menu:

```text
HONEYOS
File Workspace

[1] File Manager Shell
[2] Browse Files
[3] Command Guide
[4] Shutdown

Commands in shell: ls, create, write, read, delete, exit
```

The first screen is intentionally focused on user actions. Technical FAT32 geometry is not shown in the main menu.

## HoneyUI Interface Library

HoneyOS includes a small built-in VGA interface library named HoneyUI. It is not an external desktop GUI toolkit; it is a bare-metal text-mode UI layer designed for the current kernel environment.

HoneyUI provides reusable primitives for:

- clearing and writing to VGA text memory
- cursor control
- centered headings
- bordered panels
- menu rows
- status bars
- prompts
- themed colors

The API is declared in `include/honeyui.h`, and the implementation is in `kernel/honeyui.c`.

The main menu, file browser, command guide, shell header, and shutdown screen use HoneyUI instead of hand-written screen layout code.

## Shell Commands

| Command | Description |
| --- | --- |
| `ls` | List files in the FAT32 root directory |
| `create <file>` | Create an empty file |
| `write <file> <text>` | Write text to a file, creating it if needed |
| `read <file>` | Print file contents |
| `delete <file>` | Delete a file and free its cluster chain |
| `clear` | Clear the shell screen |
| `help` | Show command help |
| `exit` | Return to the main menu |

Example:

```text
create persist.txt
write persist.txt saved after reboot
read persist.txt
ls
```

## FAT32 Implementation

The active filesystem implementation is in `kernel/honeyfs.c`. The public API and structures are declared in `include/honeyfs.h`.

### Disk Layout

The kernel expects the FAT32 volume to begin at LBA `2048`, a 1 MiB offset:

```text
2048 sectors * 512 bytes = 1,048,576 bytes
```

The disk image layout is:

```text
honeyfs.img
├── LBA 0-2047             Reserved / unused space before partition
└── LBA 2048+              FAT32 volume
    ├── Boot sector / BPB
    ├── FSInfo sector
    ├── FAT table 1
    ├── FAT table 2
    └── Data region
        └── Root directory cluster
```

The partition offset is defined in `kernel/honeyfs.c`:

```c
#define FAT32_PARTITION_LBA 2048
```

### Mount Process

At boot, `kmain()` calls `fs_init(&honey_fs)`. The FAT32 mount process:

1. Reads the FAT32 boot sector from LBA `2048`.
2. Checks the boot signature `0x55 0xAA`.
3. Parses BPB fields:
   - bytes per sector
   - sectors per cluster
   - reserved sectors
   - number of FATs
   - sectors per FAT
   - root directory cluster
   - total sectors
4. Computes FAT and data region LBAs.
5. Marks the filesystem as mounted.

If the BPB is invalid or the disk is missing, the status screen reports that FAT32 is not mounted.

### Directory Entries

HoneyOS currently supports FAT32 short 8.3 filenames:

```text
test.txt    -> TEST.TXT
notes.md    -> NOTES.MD
persist.txt -> PERSIST.TXT
```

Long filename, directory, deleted, and volume-label entries are skipped by the root directory scanner.

The FAT32 directory entry structure is:

```c
typedef struct __attribute__((packed)) {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attributes;
    uint8_t  reserved[10];
    uint16_t cluster_high;
    uint16_t time;
    uint16_t date;
    uint16_t cluster_low;
    uint32_t file_size;
} FAT32DirEntry;
```

### FAT Operations

HoneyOS reads and writes FAT32 cluster chains directly. FAT32 entries are 32-bit values, but only the lower 28 bits are used.

Important values:

| Value | Meaning |
| --- | --- |
| `0x00000000` | Free cluster |
| `0x0FFFFFF7` | Bad cluster |
| `0x0FFFFFF8` and above | End of chain |
| `0x0FFFFFFF` | End-of-chain marker written by HoneyOS |

The implementation supports:

- scanning for free clusters
- writing FAT entries to both FAT copies
- linking clusters during writes
- clearing cluster contents
- freeing cluster chains during overwrite and delete
- reading files by following FAT chains

### File Operation Flow

Create:

1. Convert the filename to FAT 8.3 format.
2. Check for duplicates.
3. Find a free root directory entry.
4. Allocate one free cluster.
5. Mark the cluster as end-of-chain.
6. Write the directory entry.

Write:

1. Locate or create the file.
2. Free the old cluster chain.
3. Allocate enough clusters for the new content.
4. Write data sector by sector.
5. Link clusters in the FAT.
6. Update file size and first cluster.
7. Write the root directory entry.

Read:

1. Locate the file entry.
2. Read the first cluster.
3. Follow the FAT chain until the file size is satisfied.
4. Copy bytes into the shell output buffer.

Delete:

1. Locate the file entry.
2. Free its cluster chain.
3. Mark the directory entry as deleted with `0xE5`.
4. Write the root directory entry.

## Disk Driver

The disk driver in `kernel/disk.c` uses ATA PIO through x86 I/O ports.

| Port | Purpose |
| --- | --- |
| `0x1F0` | Data |
| `0x1F2` | Sector count |
| `0x1F3` | LBA low |
| `0x1F4` | LBA mid |
| `0x1F5` | LBA high |
| `0x1F6` | Drive/head |
| `0x1F7` | Command/status |

Disk API:

```c
void read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);
void write_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);
```

Multi-sector reads and writes loop one sector at a time so each LBA is explicitly addressed.

## Limits

Current FAT32 support is intentionally minimal:

- Root directory only.
- Short 8.3 filenames only.
- No subdirectories.
- No long filename support.
- No timestamps.
- No append mode.
- No dynamic root directory expansion.
- Maximum file size: `4096` bytes.

## Containerized Build

Docker is the recommended build path, especially on macOS. The project requires Linux-compatible tools for building a 32-bit freestanding kernel and GRUB ISO.

The `Dockerfile` installs:

```text
nasm
gcc
gcc-multilib
make
grub-pc-bin
grub-common
xorriso
mtools
```

Docker is needed on macOS because the host linker does not support the Linux kernel-linking flags used here:

```text
ld -m elf_i386 -T linker.ld -nostdlib
```

### Build the ISO

```bash
docker build -t honeyos-kernel .
docker create --name honeyos-build honeyos-kernel
docker cp honeyos-build:/honeyos/honeyos.iso ./honeyos.iso
docker rm honeyos-build
```

The `.dockerignore` file prevents stale host object files from being copied into Docker:

```text
*.o
*.bin
honeyos.iso
iso-extract/
```

This avoids Linux `ld` trying to link incompatible macOS-generated `.o` files.

### Native Linux / WSL Build

```bash
sudo apt install -y nasm gcc gcc-multilib make \
    grub-pc-bin grub-common xorriso mtools
make honeyos.iso
```

## ISO Extraction

The ISO can be extracted with Docker and `xorriso`:

```bash
mkdir -p iso-extract
docker run --rm \
  -v /Users/macbookpro/HoneyOS:/work \
  honeyos-kernel \
  xorriso -osirrox on -indev /work/honeyos.iso -extract / /work/iso-extract
```

Expected extracted files:

```text
iso-extract/
├── boot.catalog
└── boot/
    ├── honeyos.bin
    └── grub/
        └── grub.cfg
```

## FAT32 Image Formatting

The FAT32 image must be formatted at the same 1 MiB offset expected by the kernel:

```bash
docker run --rm \
  -v /Users/macbookpro/HoneyOS:/work \
  honeyos-kernel \
  mformat -i /work/honeyfs.img@@1048576 -F -h 255 -s 63 -T 260096 -c 1 -v HONEYOS ::
```

Parameter notes:

| Parameter | Meaning |
| --- | --- |
| `-i /work/honeyfs.img@@1048576` | Use `honeyfs.img` starting at 1 MiB offset |
| `-F` | Format as FAT32 |
| `-h 255` | Disk heads geometry |
| `-s 63` | Sectors per track |
| `-T 260096` | Total FAT32 volume sectors after the offset |
| `-c 1` | One sector per cluster |
| `-v HONEYOS` | Volume label |

Verify the FAT32 image from the host:

```bash
docker run --rm \
  -v /Users/macbookpro/HoneyOS:/work \
  honeyos-kernel \
  mdir -i /work/honeyfs.img@@1048576 ::
```

## VirtualBox Setup

Recommended VM settings:

```text
Type: Other
Version: Other/Unknown (32-bit)
EFI: Disabled
Boot order: Optical first, Hard Disk second
RAM: 64 MB or higher
```

Attach:

```text
Optical drive: honeyos.iso
Hard disk: honeyfs.vdi
```

If only `honeyfs.img` exists, convert it to a VirtualBox disk:

```bash
VBoxManage convertfromraw honeyfs.img honeyfs.vdi --format VDI
```

Then attach `honeyfs.vdi` as the VM hard disk.

## QEMU Run Option

```bash
qemu-system-i386 \
  -drive file=honeyfs.img,format=raw,if=ide,index=0 \
  -cdrom honeyos.iso
```

## Verification

Inside HoneyOS:

1. Open:

```text
[1] File Manager Shell
```

2. Run:

```text
ls
create persist.txt
write persist.txt saved after reboot
read persist.txt
```

3. Fully shut down VirtualBox.
4. Start the VM again.
5. Open the file manager shell again and run:

```text
ls
read persist.txt
```

If `PERSIST.TXT` still appears and `read persist.txt` prints `saved after reboot`, FAT32 persistence is working.

Persistence verifies:

- the FAT32 disk is mounted from the attached hard disk image
- directory entries are written to disk
- FAT cluster chains are written to disk
- file content is written into data clusters
- VirtualBox persists the virtual disk across shutdown
- HoneyOS can remount and read previous filesystem state

## Future Improvements

- Long filename support.
- Subdirectory creation and traversal.
- Larger file support.
- Append and seek operations.
- FSInfo free-cluster updates.
- FAT32 consistency checks.
- Better shell error messages.
- Automated host-side FAT image validation.
- Scripted QEMU or VirtualBox boot tests.

## References

- MINIX 3 Source: https://github.com/Stichting-MINIX-Research-Foundation/minix
- OSDev Wiki: https://wiki.osdev.org
- GNU GRUB Manual: https://www.gnu.org/software/grub/manual/grub/
- FAT Specification: https://academy.cba.mit.edu/classes/networking_communications/SD/FAT.pdf
