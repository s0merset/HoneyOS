/* ============================================================
 * HoneyOS Kernel - main.c
 * Interactive Shell + HoneyFS
 * CMSC 125 - Phase 2
 * ============================================================ */

#include "honeyfs.h"
#include "keyboard.h"

#define VGA_ADDRESS     0xB8000
#define VGA_WIDTH       80
#define VGA_HEIGHT      25

#define COLOR_BLACK         0x0
#define COLOR_YELLOW        0xE
#define COLOR_WHITE         0xF
#define COLOR_LIGHT_CYAN    0xB
#define COLOR_LIGHT_GREEN   0xA
#define COLOR_LIGHT_RED     0xC
#define COLOR_LIGHT_MAGENTA 0xD

#define MAKE_COLOR(fg, bg) ((bg << 4) | fg)

static int cursor_x = 0;
static int cursor_y = 0;
static HoneyFS honey_fs;

/* ── String helpers ── */
static int str_len(const char *s) { int i=0; while(s[i]) i++; return i; }
static int str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}
static void str_cpy(char *d, const char *s, int max) {
    int i=0; while(s[i] && i<max-1){ d[i]=s[i]; i++; } d[i]='\0';
}
static int str_starts(const char *s, const char *prefix) {
    int i=0;
    while(prefix[i]){ if(s[i]!=prefix[i]) return 0; i++; }
    return 1;
}

/* ============================================================
 * VGA
 * ============================================================ */
void vga_clear() {
    unsigned short *vga = (unsigned short*)VGA_ADDRESS;
    unsigned short blank = ' ' | (MAKE_COLOR(COLOR_WHITE, COLOR_BLACK) << 8);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) vga[i] = blank;
    cursor_x = 0; cursor_y = 0;
}

void vga_putchar(char c, unsigned char color) {
    unsigned short *vga = (unsigned short*)VGA_ADDRESS;
    if (c == '\n') { cursor_x = 0; cursor_y++; goto scroll; }
    if (c == '\r') { cursor_x = 0; return; }
    vga[cursor_y * VGA_WIDTH + cursor_x] = (unsigned short)(c | (color << 8));
    if (++cursor_x >= VGA_WIDTH) { cursor_x = 0; cursor_y++; }
scroll:
    if (cursor_y >= VGA_HEIGHT) {
        for (int i = 0; i < (VGA_HEIGHT-1)*VGA_WIDTH; i++) vga[i] = vga[i+VGA_WIDTH];
        unsigned short blank = ' ' | (MAKE_COLOR(COLOR_WHITE,COLOR_BLACK) << 8);
        for (int i = (VGA_HEIGHT-1)*VGA_WIDTH; i < VGA_HEIGHT*VGA_WIDTH; i++) vga[i] = blank;
        cursor_y = VGA_HEIGHT - 1;
    }
}

void print(const char *s, unsigned char color) {
    for (int i = 0; s[i]; i++) vga_putchar(s[i], color);
}
void println(const char *s, unsigned char color) { print(s, color); vga_putchar('\n', color); }
void print_line(char c, int n, unsigned char color) {
    for (int i = 0; i < n; i++) vga_putchar(c, color);
    vga_putchar('\n', color);
}
void print_int(int n, unsigned char color) {
    if (n == 0) { vga_putchar('0', color); return; }
    char buf[12]; int i=0;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    for (int j = i-1; j >= 0; j--) vga_putchar(buf[j], color);
}

/* Erase last character on screen */
void vga_backspace() {
    unsigned short *vga = (unsigned short*)VGA_ADDRESS;
    if (cursor_x > 0) cursor_x--;
    vga[cursor_y * VGA_WIDTH + cursor_x] = ' ' | (MAKE_COLOR(COLOR_WHITE,COLOR_BLACK) << 8);
}

/* ============================================================
 * SHELL HELPERS
 * ============================================================ */
void print_prompt() {
    print("\nhoney> ", MAKE_COLOR(COLOR_YELLOW, COLOR_BLACK));
}

void print_ok(const char *msg) {
    print("  [OK] ", MAKE_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK));
    println(msg, MAKE_COLOR(COLOR_WHITE, COLOR_BLACK));
}

void print_err(const char *msg) {
    print("  [ERR] ", MAKE_COLOR(COLOR_LIGHT_RED, COLOR_BLACK));
    println(msg, MAKE_COLOR(COLOR_WHITE, COLOR_BLACK));
}

void cmd_help() {
    unsigned char yellow = MAKE_COLOR(COLOR_YELLOW,      COLOR_BLACK);
    unsigned char cyan   = MAKE_COLOR(COLOR_LIGHT_CYAN,  COLOR_BLACK);
    unsigned char white  = MAKE_COLOR(COLOR_WHITE,       COLOR_BLACK);
    println("", white);
    print_line('-', 50, yellow);
    println("  HoneyOS Shell Commands:", yellow);
    print_line('-', 50, yellow);
    print("  create <file.txt>         ", cyan);  println("- create a new file",         white);
    print("  write <file.txt> <text>   ", cyan);  println("- write text to file",        white);
    print("  read <file.txt>           ", cyan);  println("- read file contents",        white);
    print("  delete <file.txt>         ", cyan);  println("- delete a file",             white);
    print("  ls                        ", cyan);  println("- list all files",            white);
    print("  clear                     ", cyan);  println("- clear the screen",          white);
    print("  help                      ", cyan);  println("- show this help menu",       white);
    print_line('-', 50, yellow);
}

void cmd_ls() {
    unsigned char yellow = MAKE_COLOR(COLOR_YELLOW,     COLOR_BLACK);
    unsigned char white  = MAKE_COLOR(COLOR_WHITE,      COLOR_BLACK);
    unsigned char cyan   = MAKE_COLOR(COLOR_LIGHT_CYAN, COLOR_BLACK);
    unsigned char green  = MAKE_COLOR(COLOR_LIGHT_GREEN,COLOR_BLACK);
    println("", white);
    print_line('-', 40, yellow);
    int found = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (honey_fs.files[i].status == FS_SLOT_USED) {
            print("  ", white);
            print(honey_fs.files[i].name, green);
            print("  (", white);
            print_int(honey_fs.files[i].size, cyan);
            println(" bytes)", white);
            found++;
        }
    }
    if (!found) println("  (no files)", white);
    print_line('-', 40, yellow);
    print("  ", white); print_int(honey_fs.file_count, cyan);
    println(" file(s)", white);
}

/* ============================================================
 * COMMAND PARSER
 * Parses: "cmd arg1 arg2..."
 * ============================================================ */

/* Find position of first space, return -1 if none */
static int first_space(const char *s) {
    for (int i = 0; s[i]; i++) if (s[i] == ' ') return i;
    return -1;
}

/* Get second word (after first space) */
static void get_arg1(const char *input, char *out, int max) {
    int sp = first_space(input);
    if (sp < 0) { out[0] = '\0'; return; }
    str_cpy(out, input + sp + 1, max);
    /* Cut at next space */
    for (int i = 0; out[i]; i++) if (out[i] == ' ') { out[i] = '\0'; break; }
}

/* Get everything after first two words */
static void get_arg2(const char *input, char *out, int max) {
    int sp1 = first_space(input);
    if (sp1 < 0) { out[0] = '\0'; return; }
    const char *rest = input + sp1 + 1;
    int sp2 = first_space(rest);
    if (sp2 < 0) { out[0] = '\0'; return; }
    str_cpy(out, rest + sp2 + 1, max);
}

void handle_command(const char *input) {
    unsigned char white = MAKE_COLOR(COLOR_WHITE, COLOR_BLACK);
    char arg1[FS_MAX_FILENAME];
    char arg2[FS_MAX_FILESIZE];

    println("", white);

    if (str_cmp(input, "") == 0) {
        return;

    } else if (str_cmp(input, "help") == 0) {
        cmd_help();

    } else if (str_cmp(input, "ls") == 0) {
        cmd_ls();

    } else if (str_cmp(input, "clear") == 0) {
        vga_clear();

    } else if (str_starts(input, "create ")) {
        get_arg1(input, arg1, FS_MAX_FILENAME);
        int r = fs_create(&honey_fs, arg1);
        if      (r == FS_OK)           print_ok("File created!");
        else if (r == FS_ERR_EXISTS)   print_err("File already exists.");
        else if (r == FS_ERR_NAME)     print_err("Only .txt files allowed.");
        else if (r == FS_ERR_FULL)     print_err("Filesystem full.");

    } else if (str_starts(input, "write ")) {
        get_arg1(input, arg1, FS_MAX_FILENAME);
        get_arg2(input, arg2, FS_MAX_FILESIZE);
        if (str_len(arg2) == 0) { print_err("No content provided."); return; }
        int r = fs_write(&honey_fs, arg1, arg2);
        if      (r == FS_OK)           print_ok("File written!");
        else if (r == FS_ERR_NOT_FOUND)print_err("File not found.");
        else if (r == FS_ERR_SIZE)     print_err("Content too large (max 512 bytes).");

    } else if (str_starts(input, "read ")) {
        get_arg1(input, arg1, FS_MAX_FILENAME);
        int r = fs_read(&honey_fs, arg1, arg2);
        if (r == FS_OK) {
            print("  >> ", MAKE_COLOR(COLOR_LIGHT_CYAN, COLOR_BLACK));
            println(arg2, white);
        } else {
            print_err("File not found.");
        }

    } else if (str_starts(input, "delete ")) {
        get_arg1(input, arg1, FS_MAX_FILENAME);
        int r = fs_delete(&honey_fs, arg1);
        if      (r == FS_OK)           print_ok("File deleted.");
        else if (r == FS_ERR_NOT_FOUND)print_err("File not found.");

    } else {
        print_err("Unknown command. Type 'help' for commands.");
    }
}

/* ============================================================
 * KEYBOARD INPUT — reads a full line
 * ============================================================ */
#define INPUT_MAX 256

void read_line(char *buf) {
    int i = 0;
    unsigned char white = MAKE_COLOR(COLOR_WHITE, COLOR_BLACK);
    while (1) {
        char c = keyboard_read();
        if (c == '\n') {
            buf[i] = '\0';
            return;
        } else if (c == '\b') {
            if (i > 0) { i--; vga_backspace(); }
        } else if (i < INPUT_MAX - 1) {
            buf[i++] = c;
            vga_putchar(c, white);
        }
    }
}

/* ============================================================
 * KERNEL INIT
 * ============================================================ */
void init_screen()     { vga_clear(); }
void init_kernel()     { println("  [OK] Kernel initialized",  MAKE_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK)); }
void init_filesystem() { fs_init(&honey_fs); println("  [OK] HoneyFS initialized", MAKE_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK)); }

/* ============================================================
 * KMAIN
 * ============================================================ */
void kmain() {
    init_screen();

    unsigned char yellow = MAKE_COLOR(COLOR_YELLOW,     COLOR_BLACK);
    unsigned char white  = MAKE_COLOR(COLOR_WHITE,      COLOR_BLACK);
    unsigned char cyan   = MAKE_COLOR(COLOR_LIGHT_CYAN, COLOR_BLACK);
    unsigned char green  = MAKE_COLOR(COLOR_LIGHT_GREEN,COLOR_BLACK);

    /* Banner */
    print_line('=', 60, yellow);
    println("", white);
    println("      888    888                                     ", yellow);
    println("      888    888                                     ", yellow);
    println("      8888888888  .d88b.  88888b.   .d88b. 888  888 ", yellow);
    println("      888    888 d88  88b 888  88b d8P  Y8b 888  888", yellow);
    println("      888    888 888  888 888  888 88888888 888  888", yellow);
    println("      888    888 Y88..88P 888  888 Y8b.     Y88b 888", yellow);
    println("      888    888  'Y88P'  888  888  'Y8888   'Y88888", yellow);
    println("", white);
    println("               O P E R A T I N G   S Y S T E M     ", cyan);
    println("                      Phase 2 - CMSC 125            ", white);
    println("", white);
    print_line('=', 60, yellow);
    println("", white);

    /* Boot */
    println("Booting HoneyOS...", white);
    println("", white);
    init_kernel();
    init_filesystem();
    println("", white);
    print_line('-', 60, yellow);
    println("  HoneyOS is ready! Type 'help' for commands.", cyan);
    print_line('-', 60, yellow);

    /* Shell loop */
    char input[INPUT_MAX];
    while (1) {
        print_prompt();
        read_line(input);
        handle_command(input);
    }
}
