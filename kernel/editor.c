#include "editor.h"
#include "keyboard.h"

/*
 * editor.c — HoneyEdit Full-Screen Text Editor
 * CMSC 125 — Operating Systems, UP Cebu
 *
 * HoneyEdit is launched from the File Manager Shell via the "edit <file>"
 * command. It provides a simple full-screen editing experience directly
 * on VGA text memory — no OS services, no standard library.
 *
 * Layout:
 *   Rows 0–23 : Editable text area (80 × 24 character buffer)
 *   Row 24    : Status bar (filename, key hints — always visible)
 *
 * Controls:
 *   Any printable key (ASCII 32–126) — type character at cursor
 *   Backspace ('\b')                 — erase character to the left
 *   Enter ('\n' or '\r')             — move cursor to start of next row
 *   ESC (ASCII 27)                   — save buffer to disk and exit
 *
 * The editor writes directly to VGA memory (0xB8000) using its own
 * draw functions, separate from HoneyUI, so it does not disturb the
 * shell's software cursor state in honeyui.c.
 */

/* ── VGA Hardware Constants ── */
#define VGA_ADDRESS 0xB8000    /* Physical address of the VGA text mode framebuffer */
#define VGA_WIDTH   80         /* Characters per row */
#define VGA_HEIGHT  25         /* Total rows (24 editable + 1 status bar) */

/* ── Editor Theme Colors ── */
#define COLOR_TEXT    0x1F    /* White text (0xF) on Blue background (0x1) — main editing area */
#define COLOR_STATUS  0xF0    /* Black text (0x0) on White background (0xF) — status bar row */
#define COLOR_CURSOR  0x70    /* Black text on Light Gray background — highlights the cursor position */

/*
 * text_buffer — The in-memory representation of the file being edited.
 * A 2D array of characters: text_buffer[row][col].
 * Pre-filled with spaces so unwritten cells render as blank, not garbage.
 */
static char text_buffer[EDITOR_MAX_ROWS][EDITOR_MAX_COLS];

/* Software cursor position within the text buffer */
static int e_cursor_x = 0;    /* Current column (0–79) */
static int e_cursor_y = 0;    /* Current row (0–23) */

/*
 * editor_draw_char — Write one character directly to VGA text memory at (x, y).
 *
 * This is intentionally separate from ui_putchar() in honeyui.c so that
 * the editor does not affect the shell's software cursor position.
 *
 * VGA cell format: high byte = color attribute, low byte = ASCII character.
 */
static void editor_draw_char(char c, int x, int y, unsigned char color) {
    unsigned short *vga = (unsigned short*)VGA_ADDRESS;
    vga[y * VGA_WIDTH + x] = (unsigned short)(c | (color << 8));
}

/*
 * editor_print_status — Render a message on the status bar (row 24).
 *
 * First clears the entire bottom row with the status color,
 * then prints the message left-aligned up to VGA_WIDTH characters.
 */
static void editor_print_status(const char* msg) {
    // Clear the bottom row with the status bar color
    for (int i = 0; i < VGA_WIDTH; i++) {
        editor_draw_char(' ', i, 24, COLOR_STATUS);
    }
    // Print the message left-to-right, stopping at screen edge or null terminator
    for (int i = 0; msg[i] && i < VGA_WIDTH; i++) {
        editor_draw_char(msg[i], i, 24, COLOR_STATUS);
    }
}

/*
 * editor_refresh — Redraw the entire editor screen from the text buffer.
 *
 * Called once per keypress (before reading the next key) to keep
 * the display in sync with the buffer state. Three steps:
 *   1. Draw all 24 rows of the text buffer (nulls/newlines → spaces)
 *   2. Draw the status bar on row 24
 *   3. Highlight the character under the cursor with COLOR_CURSOR
 */
static void editor_refresh() {
    // Draw the text buffer — render every cell in the 80×24 edit area
    for (int y = 0; y < EDITOR_MAX_ROWS; y++) {
        for (int x = 0; x < EDITOR_MAX_COLS; x++) {
            char c = text_buffer[y][x];
            if (c == '\0' || c == '\n') c = ' '; // Render nulls/newlines as spaces
            editor_draw_char(c, x, y, COLOR_TEXT);
        }
    }
    
    // Draw the status bar — always visible on the last row
    editor_print_status(" HoneyEdit 1.0  |  ESC: Save & Exit  |  Type your text...");

    // Render a software cursor (highlight the character under the cursor)
    // The cursor cell is redrawn with COLOR_CURSOR to make it visible
    char cursor_c = text_buffer[e_cursor_y][e_cursor_x];
    if (cursor_c == '\0' || cursor_c == '\n') cursor_c = ' ';
    editor_draw_char(cursor_c, e_cursor_x, e_cursor_y, COLOR_CURSOR);
}

/*
 * editor_start — Entry point for HoneyEdit. Called from main.c's shell.
 *
 *   fs       : Mounted HoneyFS context (used to read the file in and save on exit)
 *   filename : Name of the file to edit (must already exist on disk)
 *
 * Flow:
 *   1. Clear the text buffer to spaces and reset the cursor to (0, 0)
 *   2. Read the file from disk and unpack its content into the 2D text buffer
 *   3. Enter the main edit loop: refresh screen → read key → handle key
 *   4. On ESC: flatten the 2D buffer back to a 1D string, trim trailing
 *      spaces per row, write to disk, and return to the shell
 */
void editor_start(HoneyFS *fs, const char* filename) {
    // 1. Clear the buffer — fill every cell with a space so no garbage is shown
    for(int y = 0; y < EDITOR_MAX_ROWS; y++) {
        for(int x = 0; x < EDITOR_MAX_COLS; x++) {
            text_buffer[y][x] = ' ';
        }
    }
    e_cursor_x = 0;
    e_cursor_y = 0;

    // 2. Load existing file if it exists
    // file_buf holds the raw file content as a flat null-terminated string
    char file_buf[512]; // Match your 512 max limit from main.c
    for(int i=0; i<512; i++) file_buf[i] = '\0';
    
    if (fs_read(fs, filename, file_buf) == FS_OK) {
        // Unpack the flat string into the 2D text_buffer row by row
        int tx = 0, ty = 0;
        for(int i = 0; file_buf[i] != '\0' && i < 512; i++) {
            if (file_buf[i] == '\n') {
                ty++; tx = 0;    // Newline → advance to next row
                if (ty >= EDITOR_MAX_ROWS) break;    // Don't overflow the buffer
            } else {
                text_buffer[ty][tx++] = file_buf[i];
                if (tx >= EDITOR_MAX_COLS) { tx = 0; ty++; }    // Wrap long lines
            }
        }
    }

    // 3. Main Editor Loop — runs until ESC is pressed
    while(1) {
        editor_refresh();    /* Redraw screen with current buffer state */
        char key = keyboard_read();    /* Block until user presses a key */

        if (key == 27) { // ASCII 27 is ESC
            // Flatten the 2D buffer back to 1D to save
            // Only saves rows up to and including the current cursor row
            int idx = 0;
            for(int y = 0; y <= e_cursor_y; y++) {
                // Find the last non-space character to trim trailing whitespace
                int last_char = EDITOR_MAX_COLS - 1;
                while(last_char >= 0 && text_buffer[y][last_char] == ' ') last_char--;

                // Copy only the non-trailing-space characters from this row
                for(int x = 0; x <= last_char; x++) {
                    if (idx < 511) file_buf[idx++] = text_buffer[y][x];
                }
                // Add newlines between rows (but not after the last row)
                if (y < e_cursor_y && idx < 511) file_buf[idx++] = '\n';
            }
            file_buf[idx] = '\0';    /* Null-terminate the flattened string */
            
            // Overwrite/Save file — persist the buffer content to disk via FAT32
            fs_write(fs, filename, file_buf);
            break; // Exit editor loop — control returns to the shell in main.c
        } 
        else if (key == '\b') { // Backspace — erase character behind the cursor
            if (e_cursor_x > 0) {
                e_cursor_x--;        /* Move left */
                text_buffer[e_cursor_y][e_cursor_x] = ' ';    /* Erase that cell */
            } else if (e_cursor_y > 0) {
                // At column 0 — wrap cursor back to end of previous row
                e_cursor_y--;
                e_cursor_x = EDITOR_MAX_COLS - 1;
            }
        } 
        else if (key == '\n' || key == '\r') { // Enter — move to start of next row
            if (e_cursor_y < EDITOR_MAX_ROWS - 1) {
                e_cursor_y++;    /* Move down one row */
                e_cursor_x = 0;  /* Return to column 0 */
            }
            // If already on the last row, Enter is silently ignored
        } 
        else if (key >= 32 && key <= 126) { // Printable characters (space through '~')
            text_buffer[e_cursor_y][e_cursor_x] = key;    /* Write character into buffer */
            e_cursor_x++;    /* Advance cursor right */
            if (e_cursor_x >= EDITOR_MAX_COLS) {
                // Wrap to the next row when reaching the right edge
                e_cursor_x = 0;
                if (e_cursor_y < EDITOR_MAX_ROWS - 1) e_cursor_y++;
            }
        }
    }
}
