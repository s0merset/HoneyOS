#include "honeyui.h"

static int ui_cursor_x = 0;
static int ui_cursor_y = 0;

const UITheme HONEYUI_THEME = {
    UI_COLOR(UI_LIGHT_MAGENTA, UI_BLACK),
    UI_COLOR(UI_WHITE, UI_BLACK),
    UI_COLOR(UI_LIGHT_GRAY, UI_BLACK),
    UI_COLOR(UI_DARK_GRAY, UI_BLACK),
    UI_COLOR(UI_LIGHT_CYAN, UI_BLACK),
    UI_COLOR(UI_LIGHT_GREEN, UI_BLACK),
    UI_COLOR(UI_LIGHT_RED, UI_BLACK)
};

static int ui_strlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

static void ui_put_at(int x, int y, char c, unsigned char color) {
    unsigned short *vga = (unsigned short*)UI_VGA_ADDRESS;
    if (x < 0 || x >= UI_WIDTH || y < 0 || y >= UI_HEIGHT) return;
    vga[y * UI_WIDTH + x] = (unsigned short)(c | (color << 8));
}

void ui_clear(unsigned char color) {
    unsigned short *vga = (unsigned short*)UI_VGA_ADDRESS;
    unsigned short blank = ' ' | (color << 8);
    for (int i = 0; i < UI_WIDTH * UI_HEIGHT; i++) vga[i] = blank;
    ui_cursor_x = 0;
    ui_cursor_y = 0;
}

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

void ui_putchar(char c, unsigned char color) {
    if (c == '\n') {
        ui_cursor_x = 0;
        ui_cursor_y++;
    } else if (c == '\r') {
        ui_cursor_x = 0;
    } else {
        ui_put_at(ui_cursor_x, ui_cursor_y, c, color);
        ui_cursor_x++;
        if (ui_cursor_x >= UI_WIDTH) {
            ui_cursor_x = 0;
            ui_cursor_y++;
        }
    }

    if (ui_cursor_y >= UI_HEIGHT) {
        unsigned short *vga = (unsigned short*)UI_VGA_ADDRESS;
        for (int i = 0; i < (UI_HEIGHT - 1) * UI_WIDTH; i++) {
            vga[i] = vga[i + UI_WIDTH];
        }
        unsigned short blank = ' ' | (UI_COLOR(UI_WHITE, UI_BLACK) << 8);
        for (int i = (UI_HEIGHT - 1) * UI_WIDTH; i < UI_HEIGHT * UI_WIDTH; i++) {
            vga[i] = blank;
        }
        ui_cursor_y = UI_HEIGHT - 1;
    }
}

void ui_print(const char *s, unsigned char color) {
    for (int i = 0; s[i]; i++) ui_putchar(s[i], color);
}

void ui_println(const char *s, unsigned char color) {
    ui_print(s, color);
    ui_putchar('\n', color);
}

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

void ui_backspace(unsigned char color) {
    if (ui_cursor_x > 0) ui_cursor_x--;
    ui_put_at(ui_cursor_x, ui_cursor_y, ' ', color);
}

void ui_space(int count, unsigned char color) {
    for (int i = 0; i < count; i++) ui_putchar(' ', color);
}

void ui_center(const char *s, int row, unsigned char color) {
    int len = ui_strlen(s);
    int left = (UI_WIDTH - len) / 2;
    if (left < 0) left = 0;
    ui_set_cursor(left, row);
    ui_print(s, color);
}

void ui_rule(int x, int y, int width, unsigned char color) {
    ui_set_cursor(x, y);
    for (int i = 0; i < width; i++) ui_putchar('-', color);
}

void ui_panel(int x, int y, int width, int height, const char *title, const UITheme *theme) {
    unsigned char frame = theme->frame;
    unsigned char text = theme->text;

    ui_put_at(x, y, '+', frame);
    ui_put_at(x + width - 1, y, '+', frame);
    ui_put_at(x, y + height - 1, '+', frame);
    ui_put_at(x + width - 1, y + height - 1, '+', frame);

    for (int i = 1; i < width - 1; i++) {
        ui_put_at(x + i, y, '-', frame);
        ui_put_at(x + i, y + height - 1, '-', frame);
    }

    for (int row = 1; row < height - 1; row++) {
        ui_put_at(x, y + row, '|', frame);
        ui_put_at(x + width - 1, y + row, '|', frame);
        for (int col = 1; col < width - 1; col++) {
            ui_put_at(x + col, y + row, ' ', text);
        }
    }

    if (title && title[0]) {
        ui_set_cursor(x + 3, y);
        ui_print(" ", frame);
        ui_print(title, theme->title);
        ui_print(" ", frame);
    }
}

void ui_menu_item(int y, char key, const char *label, const char *hint, const UITheme *theme) {
    ui_set_cursor(16, y);
    ui_print("[", theme->muted);
    ui_putchar(key, theme->accent);
    ui_print("] ", theme->muted);
    ui_print(label, theme->title);

    int label_len = ui_strlen(label);
    int hint_x = 16 + 4 + label_len;
    while (hint_x < 44) {
        ui_putchar(' ', theme->text);
        hint_x++;
    }

    ui_print(hint, theme->muted);
}

void ui_status_bar(const char *left, const char *right, const UITheme *theme) {
    int right_len = ui_strlen(right);
    ui_set_cursor(0, UI_HEIGHT - 1);
    for (int i = 0; i < UI_WIDTH; i++) ui_putchar(' ', theme->frame);

    ui_set_cursor(2, UI_HEIGHT - 1);
    ui_print(left, theme->title);

    ui_set_cursor(UI_WIDTH - right_len - 2, UI_HEIGHT - 1);
    ui_print(right, theme->title);
}

void ui_prompt(const char *prompt, const UITheme *theme) {
    ui_putchar('\n', theme->text);
    ui_print(prompt, theme->accent);
}
