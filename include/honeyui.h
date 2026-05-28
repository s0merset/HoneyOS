#ifndef HONEYUI_H
#define HONEYUI_H

#define UI_VGA_ADDRESS 0xB8000
#define UI_WIDTH       80
#define UI_HEIGHT      25

#define UI_BLACK         0x0
#define UI_BLUE          0x1
#define UI_GREEN         0x2
#define UI_CYAN          0x3
#define UI_RED           0x4
#define UI_MAGENTA       0x5
#define UI_BROWN         0x6
#define UI_LIGHT_GRAY    0x7
#define UI_DARK_GRAY     0x8
#define UI_LIGHT_BLUE    0x9
#define UI_LIGHT_GREEN   0xA
#define UI_LIGHT_CYAN    0xB
#define UI_LIGHT_RED     0xC
#define UI_LIGHT_MAGENTA 0xD
#define UI_YELLOW        0xE
#define UI_WHITE         0xF

#define UI_COLOR(fg, bg) (((bg) << 4) | (fg))

typedef struct {
    unsigned char frame;
    unsigned char title;
    unsigned char text;
    unsigned char muted;
    unsigned char accent;
    unsigned char success;
    unsigned char danger;
} UITheme;

extern const UITheme HONEYUI_THEME;

void ui_clear(unsigned char color);
void ui_set_cursor(int x, int y);
int  ui_get_cursor_x();
int  ui_get_cursor_y();
void ui_putchar(char c, unsigned char color);
void ui_print(const char *s, unsigned char color);
void ui_println(const char *s, unsigned char color);
void ui_print_int(int n, unsigned char color);
void ui_backspace(unsigned char color);

void ui_space(int count, unsigned char color);
void ui_center(const char *s, int row, unsigned char color);
void ui_rule(int x, int y, int width, unsigned char color);
void ui_panel(int x, int y, int width, int height, const char *title, const UITheme *theme);
void ui_menu_item(int y, char key, const char *label, const char *hint, const UITheme *theme);
void ui_status_bar(const char *left, const char *right, const UITheme *theme);
void ui_prompt(const char *prompt, const UITheme *theme);

#endif
