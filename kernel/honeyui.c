#include "honeyui.h"

/*
 * honeyui.c — HoneyOS VGA Text-Mode UI Library
 *
 * All screen drawing in HoneyOS goes through this file.
 * It writes directly to VGA text memory at 0xB8000. Each cell
 * is 2 bytes: [color byte][ASCII byte], laid out as an 80x25 grid.
 *
 * The software cursor (ui_cursor_x, ui_cursor_y) tracks the current
 * draw position. It does NOT control the hardware blinking cursor —
 * HoneyOS uses a fully software-rendered cursor in the editor.
 */

/* ── Software Cursor State ── */
static int ui_cursor_x = 0;    /* Current column (0-79) */
static int ui_cursor_y = 0;    /* Current row (0-24) */

/*
 * HONEYUI_THEME — Default HoneyOS color theme.
 * All UI widgets use these semantic roles to stay visually consistent.
 * Colors are defined using UI_COLOR(foreground, background).
 */
const UITheme HONEYUI_THEME = {
    UI_COLOR(UI_LIGHT_MAGENTA, UI_BLACK),    /* frame   — panel borders */
    UI_COLOR(UI_WHITE, UI_BLACK),            /* title   — headings */
    UI_COLOR(UI_LIGHT_GRAY, UI_BLACK),       /* text    — body text */
    UI_COLOR(UI_DARK_GRAY, UI_BLACK),        /* muted   — brackets, hints */
    UI_COLOR(UI_LIGHT_CYAN, UI_BLACK),       /* accent  — prompts, keys */
    UI_COLOR(UI_LIGHT_GREEN, UI_BLACK),      /* success — [OK] messages */
    UI_COLOR(UI_LIGHT_RED, UI_BLACK)         /* danger  — [ERR] messages */
};

/* ── Internal Helper ── */

/* ui_strlen — Count characters in a string (no stdlib available) */
static int ui_strlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

/*
 * ui_put_at — Write a single character directly to VGA memory at (x, y).
 * Bounds-checks to avoid corrupting memory outside the 80x25 screen.
 */
static void ui_put_at(int x, int y, char c, unsigned char color) {
    unsigned short *vga = (unsigned short*)UI_VGA_ADDRESS;
    if (x < 0 || x >= UI_WIDTH || y < 0 || y >= UI_HEIGHT) return;
     /* VGA cell format: high byte = color attribute, low byte = ASCII */
    vga[y * UI_WIDTH + x] = (unsigned short)(c | (color << 8));
}

/*
 * ui_clear — Fill the entire 80x25 screen with spaces in the given color.
 * Also resets the software cursor to (0, 0).
 */
void ui_clear(unsigned char color) {
    unsigned short *vga = (unsigned short*)UI_VGA_ADDRESS;
    unsigned short blank = ' ' | (color << 8);
    for (int i = 0; i < UI_WIDTH * UI_HEIGHT; i++) vga[i] = blank;
    ui_cursor_x = 0;
    ui_cursor_y = 0;
}

/* ui_set_cursor — Move the software cursor, clamped to screen bounds */
void ui_set_cursor(int x, int y) {
    ui_cursor_x = x;
    ui_cursor_y = y;
    if (ui_cursor_x < 0) ui_cursor_x = 0;
    if (ui_cursor_y < 0) ui_cursor_y = 0;
    if (ui_cursor_x >= UI_WIDTH) ui_cursor_x = UI_WIDTH - 1;
    if (ui_cursor_y >= UI_HEIGHT) ui_cursor_y = UI_HEIGHT - 1;
}

int ui_get_cursor_x() { return ui_cursor_x; }
int ui_get_cursor_y() { return ui_cursor_y; }

/*
 * ui_putchar — Write one character at the current cursor position and advance.
 *
 * Handles:
 *   '\n' — move to start of next line
 *   '\r' — carriage return (move to column 0)
 *   other — write character, advance x; wrap to next line if needed
 *
 * If writing would go past row 24 (the last row), the screen scrolls
 * up by one row by copying all VGA cells one row upward.
 */
void ui_putchar(char c, unsigned char color) {
    if (c == '\n') {
        ui_cursor_x = 0;
        ui_cursor_y++;
    } else if (c == '\r') {
        ui_cursor_x = 0;
    } else {
        ui_put_at(ui_cursor_x, ui_cursor_y, c, color);
        ui_cursor_x++;
        if (ui_cursor_x >= UI_WIDTH) {    /* Line wrap */
            ui_cursor_x = 0;
            ui_cursor_y++;
        }
    }

    /* Scroll if cursor moves past the last row */
    if (ui_cursor_y >= UI_HEIGHT) {
        unsigned short *vga = (unsigned short*)UI_VGA_ADDRESS;
        /* Shift all rows up by one: copy row[i+1] into row[i] */
        for (int i = 0; i < (UI_HEIGHT - 1) * UI_WIDTH; i++) {
            vga[i] = vga[i + UI_WIDTH];
        }
         /* Clear the newly vacated last row */
        unsigned short blank = ' ' | (UI_COLOR(UI_WHITE, UI_BLACK) << 8);
        for (int i = (UI_HEIGHT - 1) * UI_WIDTH; i < UI_HEIGHT * UI_WIDTH; i++) {
            vga[i] = blank;
        }
        ui_cursor_y = UI_HEIGHT - 1;
    }
}

/* ui_print — Write each character of a null-terminated string */
void ui_print(const char *s, unsigned char color) {
    for (int i = 0; s[i]; i++) ui_putchar(s[i], color);
}

/* ui_println — Write a string followed by a newline */
void ui_println(const char *s, unsigned char color) {
    ui_print(s, color);
    ui_putchar('\n', color);
}

/*
 * ui_print_int — Print a non-negative integer as decimal digits.
 * Builds the digit string in reverse in a local buffer, then prints forwards.
 */
void ui_print_int(int n, unsigned char color) {
    if (n == 0) {
        ui_putchar('0', color);
        return;
    }

    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    for (int j = i - 1; j >= 0; j--) ui_putchar(buf[j], color);
}

/* ui_backspace — Move cursor left one column and erase that character */
void ui_backspace(unsigned char color) {
    if (ui_cursor_x > 0) ui_cursor_x--;
    ui_put_at(ui_cursor_x, ui_cursor_y, ' ', color);
}

/* ui_space — Print N consecutive space characters */
void ui_space(int count, unsigned char color) {
    for (int i = 0; i < count; i++) ui_putchar(' ', color);
}

/* ui_center — Print a string centered horizontally on a given row */
void ui_center(const char *s, int row, unsigned char color) {
    int len = ui_strlen(s);
    int left = (UI_WIDTH - len) / 2;
    if (left < 0) left = 0;
    ui_set_cursor(left, row);
    ui_print(s, color);
}

/* ui_rule — Draw a horizontal line of '-' characters at a given position */
void ui_rule(int x, int y, int width, unsigned char color) {
    ui_set_cursor(x, y);
    for (int i = 0; i < width; i++) ui_putchar('-', color);
}

/*
 * ui_panel — Draw a bordered rectangular panel with a title in the top edge.
 *
 * Layout (example 20-wide, 5-tall panel at x=0, y=0):
 *   +-- Title ----------+
 *   |                   |
 *   |    (content)      |
 *   |                   |
 *   +-------------------+
 *
 * Corner characters use '+', horizontal edges use '-', vertical edges use '|'.
 * The interior is filled with spaces so any prior content is cleared.
 */
void ui_panel(int x, int y, int width, int height, const char *title, const UITheme *theme) {
    unsigned char frame = theme->frame;
    unsigned char text = theme->text;

    /* Draw corners */
    ui_put_at(x, y, '+', frame);
    ui_put_at(x + width - 1, y, '+', frame);
    ui_put_at(x, y + height - 1, '+', frame);
    ui_put_at(x + width - 1, y + height - 1, '+', frame);

    /* Draw top and bottom horizontal edges */
    for (int i = 1; i < width - 1; i++) {
        ui_put_at(x + i, y, '-', frame);
        ui_put_at(x + i, y + height - 1, '-', frame);
    }

    /* Draw side edges and clear interior */
    for (int row = 1; row < height - 1; row++) {
        ui_put_at(x, y + row, '|', frame);
        ui_put_at(x + width - 1, y + row, '|', frame);
        for (int col = 1; col < width - 1; col++) {
            ui_put_at(x + col, y + row, ' ', text);
        }
    }

     /* Embed title text into the top border: "+-- Title --+" */
    if (title && title[0]) {
        ui_set_cursor(x + 3, y);
        ui_print(" ", frame);
        ui_print(title, theme->title);
        ui_print(" ", frame);
    }
}

/*
 * ui_menu_item — Render one menu row at the given Y coordinate.
 * Format: "    [key] Label              hint"
 */
void ui_menu_item(int y, char key, const char *label, const char *hint, const UITheme *theme) {
    ui_set_cursor(16, y);
    ui_print("[", theme->muted);
    ui_putchar(key, theme->accent);    /* Highlighted key character */
    ui_print("] ", theme->muted);
    ui_print(label, theme->title);

    /* Pad with spaces to align hint column at position 44 */
    int label_len = ui_strlen(label);
    int hint_x = 16 + 4 + label_len;
    while (hint_x < 44) {
        ui_putchar(' ', theme->text);
        hint_x++;
    }

    ui_print(hint, theme->muted);
}

/*
 * ui_status_bar — Fill the bottom row with a colored bar and print left/right labels.
 * Used on every screen to show context (e.g., "HoneyOS ready" | "Choose 1-4").
 */
void ui_status_bar(const char *left, const char *right, const UITheme *theme) {
    int right_len = ui_strlen(right);
    /* Fill entire last row with the frame color */
    ui_set_cursor(0, UI_HEIGHT - 1);
    for (int i = 0; i < UI_WIDTH; i++) ui_putchar(' ', theme->frame);

    /* Print left label */
    ui_set_cursor(2, UI_HEIGHT - 1);
    ui_print(left, theme->title);

    /* Print right label flush to the right edge */
    ui_set_cursor(UI_WIDTH - right_len - 2, UI_HEIGHT - 1);
    ui_print(right, theme->title);
}

/* ui_prompt — Print a blank line then the shell prompt string */
void ui_prompt(const char *prompt, const UITheme *theme) {
    ui_putchar('\n', theme->text);
    ui_print(prompt, theme->accent);
}
