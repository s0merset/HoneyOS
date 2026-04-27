# HoneyOS Kernel
### CMSC 125 - Operating Systems | Phase 2
**University of the Philippines Cebu**

---

## Project Structure
```
HoneyOS-kernel/
├── boot/
│   └── boot.asm              # Multiboot bootloader (Assembly)
├── kernel/
│   ├── main.c                # Kernel entry point (MINIX line 07100 style)
│   └── honeyfs.c             # HoneyFS filesystem implementation
├── include/
│   └── honeyfs.h             # HoneyFS header / API
├── isodir/
│   └── boot/grub/grub.cfg    # GRUB bootloader config
├── linker.ld                 # Memory layout
├── Makefile                  # Build system
├── Dockerfile                # Docker build environment
└── README.md
```

---

## HoneyFS - Filesystem Features

| Operation   | Function                                    | Description                        |
|-------------|---------------------------------------------|------------------------------------|
| Create      | `fs_create(&honey_fs, "file.txt")`          | Creates a new empty .txt file      |
| Write       | `fs_write(&honey_fs, "file.txt", "hello")`  | Writes/overwrites file content     |
| Read        | `fs_read(&honey_fs, "file.txt", buf)`       | Reads file content into buffer     |
| Delete      | `fs_delete(&honey_fs, "file.txt")`          | Deletes a file                     |
| List        | `fs_display_list()`                         | Displays all files in VGA output   |

### Limits
- Max files: **16**
- Max filename: **32 characters**
- Max file size: **512 bytes**
- Only `.txt` files are supported

---

## How to Build

### Option A: Docker (Recommended)
```bash
docker build -t honeyos-kernel .
docker create --name honeyos-build honeyos-kernel
docker cp honeyos-build:/honeyos/honeyos.iso ./honeyos.iso
docker rm honeyos-build
```

### Option B: Native Linux / WSL
```bash
sudo apt install -y nasm gcc gcc-multilib make \
    grub-pc-bin grub-common xorriso mtools
make honeyos.iso
```

---

## How to Run

### VirtualBox
1. New VM → Type: Other → Version: Other/Unknown (32-bit)
2. RAM: 64MB | No hard disk needed
3. Settings → System → **uncheck Enable EFI**
4. Settings → System → Boot Order → **Optical first**
5. Settings → Storage → IDE → attach `honeyos.iso`
6. Click **Start**

### QEMU (faster for testing)
```bash
qemu-system-i386 -cdrom honeyos.iso
```

---

## Expected Output
```
============================================================
      HoneyOS  OPERATING SYSTEM  Phase 2 - CMSC 125
============================================================

Booting HoneyOS...
  [OK] Kernel initialized
  [OK] HoneyFS initialized

------------------------------------------------------------
  HoneyOS is ready!
------------------------------------------------------------

============================================================
  HONEYFS DEMO
============================================================
  >> CREATE FILES
  CREATE hello.txt -> [OK] hello.txt
  CREATE notes.txt -> [OK] notes.txt
  ...
  >> WRITE TO FILES
  WRITE hello.txt  -> [OK] hello.txt
  ...
  >> READ FILES
  READ hello.txt   -> "Hello from HoneyOS! CMSC 125 Phase 2."
  ...
  >> DELETE FILES
  DELETE log.txt   -> [OK] log.txt
```

---

## What to Implement Next (For Teammates)

Uncomment and implement these in `kernel/main.c`:

| Function              | Description                          | Study from MINIX         |
|-----------------------|--------------------------------------|--------------------------|
| `init_memory()`       | Memory management                    | `kernel/main.c`          |
| `init_interrupts()`   | GDT/IDT setup                        | `kernel/protect.c`       |
| `init_processes()`    | Process table + scheduler            | `kernel/proc.c`          |
| `init_keyboard()`     | Keyboard driver (port 0x60)          | `drivers/tty/keyboard.c` |
| `init_drivers()`      | Disk + timer drivers                 | `drivers/`               |

---

## References
- MINIX 3 Source: https://github.com/Stichting-MINIX-Research-Foundation/minix
- OSDev Wiki: https://wiki.osdev.org
- Multiboot Spec: https://www.gnu.org/software/grub/manual/multiboot/
