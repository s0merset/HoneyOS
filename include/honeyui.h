#ifndef HONEYUI_H
#define HONEYUI_H

/*
 * honeyui.h — HoneyOS VGA Text-Mode UI Library API
 *
 * HoneyUI is HoneyOS's built-in screen drawing library.
 * It writes directly to VGA text memory at 0xB8000, which maps to
 * an 80×25 grid of (character, color) cell pairs.
 *
 * Every screen in HoneyOS — the main menu, file browser, shell header,
 * command guide, and shutdown screen — is drawn using these primitives.
 * If you add a new screen, use HoneyUI functions to keep the style
 * consistent across the OS.
 */

/* ── VGA Text Memory Layout ── */
#define UI_VGA_ADDRESS 0xB8000    /* Physical address of VGA text buffer */
#define UI_WIDTH       80         /* Characters per row */
#define UI_HEIGHT      25         /* Rows on screen */

/* ── VGA Color Palette (foreground/background) ── */
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

/*
 * UI_COLOR(fg, bg) — Pack a foreground + background color into one byte.
 * VGA color byte format: [bg3 bg2 bg1 bg0 fg3 fg2 fg1 fg0]
 * Example: UI_COLOR(UI_WHITE, UI_BLACK) = 0x0F
 */
#define UI_COLOR(fg, bg) (((bg) << 4) | (fg))

/*
 * UITheme — A set of semantic color roles for consistent UI styling.
 * All HoneyUI widgets take a UITheme pointer so colors stay uniform.
 */
typedef struct {
    unsigned char frame;    /* Border/rule color (e.g., magenta) */
    unsigned char title;    /* Heading and label text color */
    unsigned char text;     /* Normal body text color */
    unsigned char muted;    /* Dimmed/secondary text (brackets, hints) */
    unsigned char accent;   /* Highlighted elements (menu keys, prompts) */
    unsigned char success;  /* OK/success messages (green) */
    unsigned char danger;   /* Error/warning messages (red) */
} UITheme;

/* The default HoneyOS bee-themed color palette — defined in honeyui.c */
extern const UITheme HONEYUI_THEME;

/* ── Primitive Drawing Functions ── */
void ui_clear(unsigned char color);    /* Fill screen with a color */
void ui_set_cursor(int x, int y);    /* Move the software cursor */
int  ui_get_cursor_x();    /* Get current cursor column */
int  ui_get_cursor_y();    /* Get current cursor row */
void ui_putchar(char c, unsigned char color);    /* Write one character, advance cursor */
void ui_print(const char *s, unsigned char color);    /* Print a string */
void ui_println(const char *s, unsigned char color);    /* Print a string + newline */
void ui_print_int(int n, unsigned char color);    /* Print a decimal integer */
void ui_backspace(unsigned char color);    /* Erase character behind cursor */

/* ── Layout Helpers ── */
void ui_space(int count, unsigned char color);    /* Print N space characters */
void ui_center(const char *s, int row, unsigned char color);    /* Center string on a row */
void ui_rule(int x, int y, int width, unsigned char color);    /* Draw a horizontal line of '-' */

/* ── Widget Functions ── */
/* ui_panel — Draw a bordered box with a title label (used for all main UI screens) */
void ui_panel(int x, int y, int width, int height, const char *title, const UITheme *theme);

/* ui_menu_item — Render one "[key] Label    hint" row inside a panel */
void ui_menu_item(int y, char key, const char *label, const char *hint, const UITheme *theme);

/* ui_status_bar — Draw a full-width bar on the last row with left/right text */
void ui_status_bar(const char *left, const char *right, const UITheme *theme);

/* ui_prompt — Print an indented shell prompt line (e.g., "honey:/ > ") */
void ui_prompt(const char *prompt, const UITheme *theme);

#endif
