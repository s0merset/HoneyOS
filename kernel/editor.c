#include "editor.h"
#include "keyboard.h"

// Hardware VGA mapping
#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

// Editor Theme Colors
#define COLOR_TEXT    0x0F // White text on Black background
#define COLOR_STATUS  0x0F // White text on Black background
#define COLOR_CURSOR  0x70 // Gray background for the cursor

static char text_buffer[EDITOR_MAX_ROWS][EDITOR_MAX_COLS];
static int e_cursor_x = 0;
static int e_cursor_y = 0;

/* Direct VGA write to avoid messing with main.c's shell cursor */
static void editor_draw_char(char c, int x, int y, unsigned char color) {
    unsigned short *vga = (unsigned short*)VGA_ADDRESS;
    vga[y * VGA_WIDTH + x] = (unsigned short)(c | (color << 8));
}

static void editor_print_status(const char* msg) {
    // Clear the bottom row with the status bar color
    for (int i = 0; i < VGA_WIDTH; i++) {
        editor_draw_char(' ', i, 24, COLOR_STATUS);
    }
    // Print the message
    for (int i = 0; msg[i] && i < VGA_WIDTH; i++) {
        editor_draw_char(msg[i], i, 24, COLOR_STATUS);
    }
}

static void editor_refresh() {
    // Draw the text buffer
    for (int y = 0; y < EDITOR_MAX_ROWS; y++) {
        for (int x = 0; x < EDITOR_MAX_COLS; x++) {
            char c = text_buffer[y][x];
            if (c == '\0' || c == '\n') c = ' '; // Render nulls/newlines as spaces
            editor_draw_char(c, x, y, COLOR_TEXT);
        }
    }
    
    // Draw the status bar
    editor_print_status(" HoneyEdit 1.0  |  ESC: Save & Exit  |  Type your text...");

    // Render a software cursor (highlight the character under the cursor)
    char cursor_c = text_buffer[e_cursor_y][e_cursor_x];
    if (cursor_c == '\0' || cursor_c == '\n') cursor_c = ' ';
    editor_draw_char(cursor_c, e_cursor_x, e_cursor_y, COLOR_CURSOR);
}

void editor_start(HoneyFS *fs, const char* filename) {
    // 1. Clear the buffer
    for(int y = 0; y < EDITOR_MAX_ROWS; y++) {
        for(int x = 0; x < EDITOR_MAX_COLS; x++) {
            text_buffer[y][x] = ' ';
        }
    }
    e_cursor_x = 0;
    e_cursor_y = 0;

    // 2. Load existing file if it exists
    char file_buf[FS_MAX_FILESIZE + 1];
    for (int i = 0; i < FS_MAX_FILESIZE + 1; i++) file_buf[i] = '\0';
    
    if (fs_read(fs, filename, file_buf) == FS_OK) {
        int tx = 0, ty = 0;
        for (int i = 0; file_buf[i] != '\0' && i < FS_MAX_FILESIZE; i++) {
            if (file_buf[i] == '\n') {
                ty++; tx = 0;
                if (ty >= EDITOR_MAX_ROWS) break;
            } else {
                text_buffer[ty][tx++] = file_buf[i];
                if (tx >= EDITOR_MAX_COLS) { tx = 0; ty++; }
                if (ty >= EDITOR_MAX_ROWS) break;
            }
        }
        if (ty >= EDITOR_MAX_ROWS) {
            e_cursor_y = EDITOR_MAX_ROWS - 1;
            e_cursor_x = EDITOR_MAX_COLS - 1;
        } else {
            e_cursor_y = ty;
            e_cursor_x = tx;
            if (e_cursor_x >= EDITOR_MAX_COLS) e_cursor_x = EDITOR_MAX_COLS - 1;
        }
    }

    // 3. Main Editor Loop
    while(1) {
        editor_refresh();
        int key = keyboard_read_key();

        if (key == 27) { // ASCII 27 is ESC
            // Flatten the 2D buffer back to 1D to save
            int idx = 0;
            int last_row = EDITOR_MAX_ROWS - 1;
            while (last_row > 0) {
                int has_content = 0;
                for (int x = 0; x < EDITOR_MAX_COLS; x++) {
                    if (text_buffer[last_row][x] != ' ') { has_content = 1; break; }
                }
                if (has_content) break;
                last_row--;
            }

            for (int y = 0; y <= last_row; y++) {
                // Find the last non-space character to trim trailing whitespace
                int last_char = EDITOR_MAX_COLS - 1;
                while(last_char >= 0 && text_buffer[y][last_char] == ' ') last_char--;
                
                for (int x = 0; x <= last_char; x++) {
                    if (idx < FS_MAX_FILESIZE) file_buf[idx++] = text_buffer[y][x];
                }
                // Add newlines between rows
                if (y < last_row && idx < FS_MAX_FILESIZE) file_buf[idx++] = '\n';
            }
            file_buf[idx] = '\0';
            
            // Overwrite/Save file
            fs_write(fs, filename, file_buf);
            break; // Exit editor loop
        }
        else if (key == KEY_UP) {
            if (e_cursor_y > 0) e_cursor_y--;
        }
        else if (key == KEY_DOWN) {
            if (e_cursor_y < EDITOR_MAX_ROWS - 1) e_cursor_y++;
        }
        else if (key == KEY_LEFT) {
            if (e_cursor_x > 0) e_cursor_x--;
            else if (e_cursor_y > 0) { e_cursor_y--; e_cursor_x = EDITOR_MAX_COLS - 1; }
        }
        else if (key == KEY_RIGHT) {
            if (e_cursor_x < EDITOR_MAX_COLS - 1) e_cursor_x++;
            else if (e_cursor_y < EDITOR_MAX_ROWS - 1) { e_cursor_y++; e_cursor_x = 0; }
        }
        else if (key == '\b') { // Backspace
            if (e_cursor_x > 0) {
                e_cursor_x--;
                text_buffer[e_cursor_y][e_cursor_x] = ' ';
            } else if (e_cursor_y > 0) {
                e_cursor_y--;
                e_cursor_x = EDITOR_MAX_COLS - 1;
            }
        } 
        else if (key == '\n' || key == '\r') { // Enter
            if (e_cursor_y < EDITOR_MAX_ROWS - 1) {
                e_cursor_y++;
                e_cursor_x = 0;
            }
        } 
        else if (key >= 32 && key <= 126) { // Printable characters
            text_buffer[e_cursor_y][e_cursor_x] = key;
            e_cursor_x++;
            if (e_cursor_x >= EDITOR_MAX_COLS) {
                e_cursor_x = 0;
                if (e_cursor_y < EDITOR_MAX_ROWS - 1) e_cursor_y++;
            }
        }
    }
}
