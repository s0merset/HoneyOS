/* ============================================================
 * HoneyOS Kernel - main.c
 * CMSC 125 — Operating Systems, UP Cebu
 *
 * This is the top-level kernel file. It is the first C function
 * called after boot.asm sets up the stack and jumps to kmain().
 *
 * Responsibilities:
 *   - Initialize the screen and filesystem
 *   - Display the main menu and dispatch to feature screens
 *   - Run the interactive File Manager Shell
 *   - Parse and execute shell commands (ls, create, write, read, delete)
 * ============================================================ */

#include "honeyfs.h"
#include "keyboard.h"
#include "honeyui.h"
#include "editor.h"

#define INPUT_MAX 256    /* Maximum characters in a shell input line */

/* Global filesystem context — shared by all functions in this file */
static HoneyFS honey_fs;

/* ── String Helpers (no stdlib in bare-metal) ── */

/* str_cmp — Return 0 if strings are equal, nonzero otherwise */
static int str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

/* str_cpy — Copy at most max-1 characters from s to d, always null-terminate */
static void str_cpy(char *d, const char *s, int max) {
    int i=0; while(s[i] && i<max-1){ d[i]=s[i]; i++; } d[i]='\0';
}

/* str_starts — Return 1 if s begins with the given prefix, 0 otherwise */
static int str_starts(const char *s, const char *prefix) {
    int i=0;
    while(prefix[i]){ if(s[i]!=prefix[i]) return 0; i++; }
    return 1;
}

/* ── Screen Setup ── */

/* init_screen — Clear the screen with black background before anything is drawn */
void init_screen() { 
    ui_clear(UI_COLOR(UI_WHITE, UI_BLACK)); 
}

/* These wrappers let honeyfs.c call print/println without including honeyui.h directly */
void vga_putchar(char c, unsigned char color) { ui_putchar(c, color); }
void print(const char *s, unsigned char color) { ui_print(s, color); }
void println(const char *s, unsigned char color) { ui_println(s, color); }

/* wait_for_key — Display a "press any key" prompt and block until input */
static void wait_for_key() {
    ui_println("", HONEYUI_THEME.text);
    ui_center("Press any key to return", 22, HONEYUI_THEME.accent);
    keyboard_read();
}

/* ── Shell Output Helpers ── */
void print_prompt() { ui_prompt("honey:/ > ", &HONEYUI_THEME); }
void print_ok(const char *msg) { ui_print("  [OK] ", HONEYUI_THEME.success); ui_println(msg, HONEYUI_THEME.text); }
void print_err(const char *msg) { ui_print("  [ERR] ", HONEYUI_THEME.danger); ui_println(msg, HONEYUI_THEME.text); }

/* cmd_help — Print a formatted table of all available shell commands */
static void shell_reset_screen() {
    ui_clear(UI_COLOR(UI_WHITE, UI_BLACK));
    ui_center("HoneyOS File Manager", 1, HONEYUI_THEME.frame);
    ui_rule(8, 3, 64, HONEYUI_THEME.accent);
    ui_set_cursor(2, 5);
    ui_println("Type 'help' to view commands. Type 'exit' to return.", HONEYUI_THEME.text);
    ui_status_bar("File Manager Shell", "exit: menu", &HONEYUI_THEME);
}

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
    ui_print("  edit <file.txt>           ", HONEYUI_THEME.accent);  ui_println("- open the text editor", HONEYUI_THEME.text);
    ui_print("  read <file.txt>           ", HONEYUI_THEME.accent);  ui_println("- read file contents", HONEYUI_THEME.text);
    ui_print("  delete <file.txt>         ", HONEYUI_THEME.accent);  ui_println("- delete a file", HONEYUI_THEME.text);
    ui_print("  clear                     ", HONEYUI_THEME.accent);  ui_println("- clear the screen", HONEYUI_THEME.text);
    ui_print("  exit                      ", HONEYUI_THEME.accent);  ui_println("- return to the main menu", HONEYUI_THEME.text);
    ui_print("  help                      ", HONEYUI_THEME.accent);  ui_println("- show this help menu", HONEYUI_THEME.text);
}

/* cmd_ls — List all files in the root directory with decorative borders */
void cmd_ls() {
    ui_println("", HONEYUI_THEME.text);
    ui_rule(2, ui_get_cursor_y(), 40, HONEYUI_THEME.frame);
    ui_println("", HONEYUI_THEME.text);
    fs_list(&honey_fs);
    ui_rule(2, ui_get_cursor_y(), 40, HONEYUI_THEME.frame);
    ui_println("", HONEYUI_THEME.text);
}

/* ── Command Argument Parser ── */

/* first_space — Return the index of the first space in s, or -1 if none */
static int first_space(const char *s) { for (int i = 0; s[i]; i++) if (s[i] == ' ') return i; return -1; }

/* get_arg1 — Extract the first argument after the command word */
static void get_arg1(const char *input, char *out, int max) {
    int sp = first_space(input);
    if (sp < 0) { out[0] = '\0'; return; }
    str_cpy(out, input + sp + 1, max);
    /* Truncate at the next space (arg1 ends at second word) */
    for (int i = 0; out[i]; i++) if (out[i] == ' ') { out[i] = '\0'; break; }
}

/* get_arg2_rest — Extract everything after the second word (used for "write <file> <text>") */
static void get_arg2_rest(const char *input, char *out, int max) {
    int sp = first_space(input);
    if (sp < 0) { out[0] = '\0'; return; }
    int i = sp + 1;
    while (input[i] && input[i] != ' ') i++;    /* Skip first argument */
    while (input[i] == ' ') i++;    /* Skip any extra spaces */
    str_cpy(out, input + i, max);
}

/*
 * handle_command — Parse and dispatch one shell command.
 *
 * Supported commands:
 *   ls                        — list files
 *   create <name>             — create empty file
 *   write  <name> <content>   — write text to file
 *   read   <name>             — print file contents
 *   delete <name>             — remove file
 *   clear                     — clear screen
 *   help                      — show command reference
 */
void handle_command(const char *input) {
    char arg1[FS_MAX_FILENAME];
    char arg2[FS_MAX_FILESIZE + 1];
    ui_println("", HONEYUI_THEME.text);

    if (str_cmp(input, "") == 0) return;    /* Empty input, do nothing */
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
    } else if (str_starts(input, "edit ")) {
        get_arg1(input, arg1, FS_MAX_FILENAME);
        if (arg1[0] == '\0') {
            print_err("Missing filename.");
        } else {
            FAT32DirEntry entry;
            int result = fs_find_entry(&honey_fs, arg1, &entry);
            if (result == FS_OK) {
                editor_start(&honey_fs, arg1);
                shell_reset_screen();
            } else if (result == FS_ERR_NOT_FOUND) {
                print_err("File not found.");
            } else if (result == FS_ERR_NAME) {
                print_err("Invalid filename.");
            } else {
                print_err("Could not open file.");
            }
        }
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

/* ── Screen Renderers ── */

/* show_menu — Render the main menu and wait for a keypress */
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

/*
 * read_line — Blocking line input from keyboard into buf.
 * Supports backspace. Returns when Enter ('\n') is pressed.
 */
void read_line(char *buf) {
    int i = 0;
    while (1) {
        char c = keyboard_read();
        if (c == '\n') { buf[i] = '\0'; return; }
        else if (c == '\b') { if (i > 0) { i--; ui_backspace(UI_COLOR(UI_WHITE, UI_BLACK)); } }
        else if (i < INPUT_MAX - 1) { buf[i++] = c; ui_putchar(c, HONEYUI_THEME.text); }
    }
}

/* init_filesystem — Mount the FAT32 volume on the virtual hard disk */
void init_filesystem() {
    fs_init(&honey_fs);
}

/* show_file_browser — Read-only view of all files currently on disk */
void show_file_browser() {
    ui_clear(UI_COLOR(UI_WHITE, UI_BLACK));
    ui_center("Saved Files", 2, HONEYUI_THEME.frame);
    ui_panel(10, 4, 60, 16, "Root Directory", &HONEYUI_THEME);
    ui_set_cursor(14, 7);
    fs_list(&honey_fs);
    ui_status_bar("Browse Files", "Any key: back", &HONEYUI_THEME);
    wait_for_key();
}

/* show_command_guide — Static reference screen listing all shell commands */
void show_command_guide() {
    ui_clear(UI_COLOR(UI_WHITE, UI_BLACK));
    ui_center("Command Guide", 2, HONEYUI_THEME.frame);
    ui_panel(8, 4, 64, 18, "File Manager Commands", &HONEYUI_THEME);
    ui_set_cursor(12, 7);
    ui_print("ls                        ", HONEYUI_THEME.accent); ui_println("show all saved files", HONEYUI_THEME.text);
    ui_set_cursor(12, 9);
    ui_print("create <file.txt>         ", HONEYUI_THEME.accent); ui_println("make a new file", HONEYUI_THEME.text);
    ui_set_cursor(12, 11);
    ui_print("write <file.txt> <text>   ", HONEYUI_THEME.accent); ui_println("save text into a file", HONEYUI_THEME.text);
    ui_set_cursor(12, 13);
    ui_print("read <file.txt>           ", HONEYUI_THEME.accent); ui_println("open a file", HONEYUI_THEME.text);
    ui_set_cursor(12, 15);
    ui_print("edit <file.txt>           ", HONEYUI_THEME.accent); ui_println("edit a file", HONEYUI_THEME.text);
    ui_set_cursor(12, 17);
    ui_print("delete <file.txt>         ", HONEYUI_THEME.accent); ui_println("remove a file", HONEYUI_THEME.text);
    ui_set_cursor(12, 19);
    ui_print("exit                      ", HONEYUI_THEME.accent); ui_println("return to main menu", HONEYUI_THEME.text);
    ui_status_bar("Command Guide", "Any key: back", &HONEYUI_THEME);
    wait_for_key();
}

/* launch_shell — Run the interactive File Manager Shell until the user types "exit" */
void launch_shell() {
    shell_reset_screen();
    char input[INPUT_MAX];
    while(1) {
        print_prompt();
        read_line(input);
        if (str_cmp(input, "exit") == 0) break;
        handle_command(input);
    }
}

/* shutdown_system — Halt the CPU; VirtualBox can then be safely closed */
void shutdown_system() {
    ui_clear(UI_COLOR(UI_WHITE, UI_BLACK));
    ui_panel(12, 8, 56, 7, "Shutdown", &HONEYUI_THEME);
    ui_center("HoneyOS has been halted", 11, HONEYUI_THEME.frame);
    ui_center("You may close the virtual machine window.", 12, HONEYUI_THEME.accent);
    while (1) {
        __asm__("hlt");    /* CPU halts here; loop prevents any accidental continuation */
    }
}

/*
 * kmain — Kernel entry point called from boot.asm.
 *
 * 1. Initialize screen and FAT32 filesystem
 * 2. Loop forever: show main menu, read key, dispatch to feature
 */
void kmain() {
    init_screen();
    init_filesystem();
    
    while(1) {
        show_menu();
        
        char choice = keyboard_read(); /* Block until the user presses 1–4 */
        
        if (choice == '1') {
            launch_shell();
        } else if (choice == '2') {
            show_file_browser();
        } else if (choice == '3') {
            show_command_guide();
        } else if (choice == '4') {
            shutdown_system();    /* Any other key simply redraws the menu */
        }
    }
}
