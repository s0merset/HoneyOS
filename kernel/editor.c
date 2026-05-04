#include "editor.h"
#include "keyboard.h"

// Hardware VGA mapping
#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

// Editor Theme Colors
#define COLOR_TEXT    0x1F // White text on Blue background
#define COLOR_STATUS  0xF0 // Black text on White background
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
    char file_buf[512]; // Match your 512 max limit from main.c
    for(int i=0; i<512; i++) file_buf[i] = '\0';
    
    if (fs_read(fs, filename, file_buf) == FS_OK) {
        int tx = 0, ty = 0;
        for(int i = 0; file_buf[i] != '\0' && i < 512; i++) {
            if (file_buf[i] == '\n') {
                ty++; tx = 0;
                if (ty >= EDITOR_MAX_ROWS) break;
            } else {
                text_buffer[ty][tx++] = file_buf[i];
                if (tx >= EDITOR_MAX_COLS) { tx = 0; ty++; }
            }
        }
    }

    // 3. Main Editor Loop
    while(1) {
        editor_refresh();
        char key = keyboard_read();

        if (key == 27) { // ASCII 27 is ESC
            // Flatten the 2D buffer back to 1D to save
            int idx = 0;
            for(int y = 0; y <= e_cursor_y; y++) {
                // Find the last non-space character to trim trailing whitespace
                int last_char = EDITOR_MAX_COLS - 1;
                while(last_char >= 0 && text_buffer[y][last_char] == ' ') last_char--;
                
                for(int x = 0; x <= last_char; x++) {
                    if (idx < 511) file_buf[idx++] = text_buffer[y][x];
                }
                // Add newlines between rows
                if (y < e_cursor_y && idx < 511) file_buf[idx++] = '\n';
            }
            file_buf[idx] = '\0';
            
            // Overwrite/Save file
            fs_write(fs, filename, file_buf);
            break; // Exit editor loop
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
