/* ============================================================
 * HoneyOS Kernel - main.c
 * Interactive Shell + HoneyFS (FAT32)
 * ============================================================ */

#include "honeyfs.h"
#include "keyboard.h"
#include "honeyui.h"

#define INPUT_MAX 256

static HoneyFS honey_fs;

/* ── String helpers ── */
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

void init_screen() { 
    ui_clear(UI_COLOR(UI_WHITE, UI_BLACK)); 
}

void vga_putchar(char c, unsigned char color) { ui_putchar(c, color); }
void print(const char *s, unsigned char color) { ui_print(s, color); }
void println(const char *s, unsigned char color) { ui_println(s, color); }

static void wait_for_key() {
    ui_println("", HONEYUI_THEME.text);
    ui_center("Press any key to return", 22, HONEYUI_THEME.accent);
    keyboard_read();
}

/* ── Shell Helpers ── */
void print_prompt() { ui_prompt("honey:/ > ", &HONEYUI_THEME); }
void print_ok(const char *msg) { ui_print("  [OK] ", HONEYUI_THEME.success); ui_println(msg, HONEYUI_THEME.text); }
void print_err(const char *msg) { ui_print("  [ERR] ", HONEYUI_THEME.danger); ui_println(msg, HONEYUI_THEME.text); }

void cmd_help() {
    ui_println("", HONEYUI_THEME.text);
    ui_rule(8, ui_get_cursor_y(), 64, HONEYUI_THEME.frame);
    ui_println("", HONEYUI_THEME.text);
    ui_println("  File Manager Commands", HONEYUI_THEME.title);
    ui_rule(8, ui_get_cursor_y(), 64, HONEYUI_THEME.frame);
    ui_println("", HONEYUI_THEME.text);
    ui_print("  ls                        ", HONEYUI_THEME.accent);  ui_println("- list all files", HONEYUI_THEME.text);
    ui_print("  create <file.txt>         ", HONEYUI_THEME.accent);  ui_println("- create an empty file", HONEYUI_THEME.text);
    ui_print("  write <file.txt> <text>   ", HONEYUI_THEME.accent);  ui_println("- write text to a file", HONEYUI_THEME.text);
    ui_print("  read <file.txt>           ", HONEYUI_THEME.accent);  ui_println("- read file contents", HONEYUI_THEME.text);
    ui_print("  delete <file.txt>         ", HONEYUI_THEME.accent);  ui_println("- delete a file", HONEYUI_THEME.text);
    ui_print("  clear                     ", HONEYUI_THEME.accent);  ui_println("- clear the screen", HONEYUI_THEME.text);
    ui_print("  exit                      ", HONEYUI_THEME.accent);  ui_println("- return to the main menu", HONEYUI_THEME.text);
    ui_print("  help                      ", HONEYUI_THEME.accent);  ui_println("- show this help menu", HONEYUI_THEME.text);
}

void cmd_ls() {
    ui_println("", HONEYUI_THEME.text);
    ui_rule(2, ui_get_cursor_y(), 40, HONEYUI_THEME.frame);
    ui_println("", HONEYUI_THEME.text);
    fs_list(&honey_fs);
    ui_rule(2, ui_get_cursor_y(), 40, HONEYUI_THEME.frame);
    ui_println("", HONEYUI_THEME.text);
}

/* ── Parser ── */
static int first_space(const char *s) { for (int i = 0; s[i]; i++) if (s[i] == ' ') return i; return -1; }
static void get_arg1(const char *input, char *out, int max) {
    int sp = first_space(input);
    if (sp < 0) { out[0] = '\0'; return; }
    str_cpy(out, input + sp + 1, max);
    for (int i = 0; out[i]; i++) if (out[i] == ' ') { out[i] = '\0'; break; }
}
static void get_arg2_rest(const char *input, char *out, int max) {
    int sp = first_space(input);
    if (sp < 0) { out[0] = '\0'; return; }
    int i = sp + 1;
    while (input[i] && input[i] != ' ') i++;
    while (input[i] == ' ') i++;
    str_cpy(out, input + i, max);
}

void handle_command(const char *input) {
    char arg1[FS_MAX_FILENAME];
    char arg2[FS_MAX_FILESIZE + 1];
    ui_println("", HONEYUI_THEME.text);

    if (str_cmp(input, "") == 0) return;
    else if (str_cmp(input, "help") == 0) cmd_help();
    else if (str_cmp(input, "ls") == 0) cmd_ls();
    else if (str_cmp(input, "clear") == 0) ui_clear(UI_COLOR(UI_WHITE, UI_BLACK));
    else if (str_starts(input, "create ")) {
        get_arg1(input, arg1, FS_MAX_FILENAME);
        int result = fs_create(&honey_fs, arg1);
        if (result == FS_OK) print_ok("File created.");
        else if (result == FS_ERR_EXISTS) print_err("File already exists.");
        else print_err("Could not create file.");
    } else if (str_starts(input, "write ")) {
        get_arg1(input, arg1, FS_MAX_FILENAME);
        get_arg2_rest(input, arg2, FS_MAX_FILESIZE + 1);
        int result = fs_write(&honey_fs, arg1, arg2);
        if (result == FS_OK) print_ok("File written.");
        else if (result == FS_ERR_SIZE) print_err("Text is too large.");
        else print_err("Could not write file.");
    } else if (str_starts(input, "delete ")) {
        get_arg1(input, arg1, FS_MAX_FILENAME);
        if (fs_delete(&honey_fs, arg1) == FS_OK) print_ok("File deleted.");
        else print_err("File not found.");
    }
    else if (str_starts(input, "read ")) {
        get_arg1(input, arg1, FS_MAX_FILENAME);
        if (fs_read(&honey_fs, arg1, arg2) == FS_OK) {
            ui_print("  >> ", HONEYUI_THEME.accent);
            ui_println(arg2, HONEYUI_THEME.text);
        } else print_err("File not found.");
    } else print_err("Unknown command.");
}

/* ── Init ── */

void show_menu() {
    ui_clear(UI_COLOR(UI_WHITE, UI_BLACK));
    ui_center("HONEYOS", 2, HONEYUI_THEME.frame);
    ui_center("File Workspace", 3, HONEYUI_THEME.accent);
    ui_panel(10, 5, 60, 13, "Main Actions", &HONEYUI_THEME);

    ui_menu_item(8, '1', "Open File Manager", "create, write, read, delete", &HONEYUI_THEME);
    ui_menu_item(10, '2', "Browse Files", "show saved files", &HONEYUI_THEME);
    ui_menu_item(12, '3', "Command Guide", "view available actions", &HONEYUI_THEME);
    ui_menu_item(14, '4', "Shutdown", "halt the system", &HONEYUI_THEME);

    ui_status_bar("HoneyOS ready", "Choose 1-4", &HONEYUI_THEME);
    ui_set_cursor(16, 20);
    ui_print("Choose an action: ", HONEYUI_THEME.title);
}

void read_line(char *buf) {
    int i = 0;
    while (1) {
        char c = keyboard_read();
        if (c == '\n') { buf[i] = '\0'; return; }
        else if (c == '\b') { if (i > 0) { i--; ui_backspace(UI_COLOR(UI_WHITE, UI_BLACK)); } }
        else if (i < INPUT_MAX - 1) { buf[i++] = c; ui_putchar(c, HONEYUI_THEME.text); }
    }
}

void init_filesystem() {
    fs_init(&honey_fs);
}

void show_file_browser() {
    ui_clear(UI_COLOR(UI_WHITE, UI_BLACK));
    ui_center("Saved Files", 2, HONEYUI_THEME.frame);
    ui_panel(10, 4, 60, 16, "Root Directory", &HONEYUI_THEME);
    ui_set_cursor(14, 7);
    fs_list(&honey_fs);
    ui_status_bar("Browse Files", "Any key: back", &HONEYUI_THEME);
    wait_for_key();
}

void show_command_guide() {
    ui_clear(UI_COLOR(UI_WHITE, UI_BLACK));
    ui_center("Command Guide", 2, HONEYUI_THEME.frame);
    ui_panel(8, 4, 64, 16, "File Manager Commands", &HONEYUI_THEME);
    ui_set_cursor(12, 7);
    ui_print("ls                        ", HONEYUI_THEME.accent); ui_println("show all saved files", HONEYUI_THEME.text);
    ui_set_cursor(12, 9);
    ui_print("create <file.txt>         ", HONEYUI_THEME.accent); ui_println("make a new file", HONEYUI_THEME.text);
    ui_set_cursor(12, 11);
    ui_print("write <file.txt> <text>   ", HONEYUI_THEME.accent); ui_println("save text into a file", HONEYUI_THEME.text);
    ui_set_cursor(12, 13);
    ui_print("read <file.txt>           ", HONEYUI_THEME.accent); ui_println("open a file", HONEYUI_THEME.text);
    ui_set_cursor(12, 15);
    ui_print("delete <file.txt>         ", HONEYUI_THEME.accent); ui_println("remove a file", HONEYUI_THEME.text);
    ui_set_cursor(12, 17);
    ui_print("exit                      ", HONEYUI_THEME.accent); ui_println("return to main menu", HONEYUI_THEME.text);
    ui_status_bar("Command Guide", "Any key: back", &HONEYUI_THEME);
    wait_for_key();
}

void launch_shell() {
    ui_clear(UI_COLOR(UI_WHITE, UI_BLACK));
    ui_center("HoneyOS File Manager", 1, HONEYUI_THEME.frame);
    ui_rule(8, 3, 64, HONEYUI_THEME.accent);
    ui_set_cursor(2, 5);
    ui_println("Type 'help' to view commands. Type 'exit' to return.", HONEYUI_THEME.text);
    ui_status_bar("File Manager Shell", "exit: menu", &HONEYUI_THEME);
    char input[INPUT_MAX];
    while(1) {
        print_prompt();
        read_line(input);
        if (str_cmp(input, "exit") == 0) break;
        handle_command(input);
    }
}

void shutdown_system() {
    ui_clear(UI_COLOR(UI_WHITE, UI_BLACK));
    ui_panel(12, 8, 56, 7, "Shutdown", &HONEYUI_THEME);
    ui_center("HoneyOS has been halted", 11, HONEYUI_THEME.frame);
    ui_center("You may close the virtual machine window.", 12, HONEYUI_THEME.accent);
    while (1) {
        __asm__("hlt");
    }
}

void kmain() {
    init_screen();
    init_filesystem();
    
    while(1) {
        show_menu();
        
        char choice = keyboard_read(); // Wait for key
        
        if (choice == '1') {
            launch_shell();
        } else if (choice == '2') {
            show_file_browser();
        } else if (choice == '3') {
            show_command_guide();
        } else if (choice == '4') {
            shutdown_system();
        }
    }
}
