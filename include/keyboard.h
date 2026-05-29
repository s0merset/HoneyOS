#ifndef KEYBOARD_H
#define KEYBOARD_H

/* Read a byte from an I/O port */
static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Keyboard ports */
#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64

/* Special key codes (outside ASCII range) */
#define KEY_UP     0x100
#define KEY_DOWN   0x101
#define KEY_LEFT   0x102
#define KEY_RIGHT  0x103

/* US QWERTY scancode to ASCII map */
static const char scancode_map[128] = {
    0,  27,  '1','2','3','4','5','6','7','8','9','0','-','=', '\b', 
  '\t', 'q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
   '*',  0, ' '
};

/* Read one key (blocking). Returns ASCII or KEY_* for arrows. */
static inline int keyboard_read_key() {
    unsigned char scancode;
    while (1) {
        if (inb(KEYBOARD_STATUS_PORT) & 0x1) {
            scancode = inb(KEYBOARD_DATA_PORT);

            if (scancode == 0xE0) {
                /* Extended scancode (arrow keys) */
                while (!(inb(KEYBOARD_STATUS_PORT) & 0x1)) { }
                scancode = inb(KEYBOARD_DATA_PORT);
                if (scancode & 0x80) continue; /* Ignore releases */

                switch (scancode) {
                    case 0x48: return KEY_UP;
                    case 0x50: return KEY_DOWN;
                    case 0x4B: return KEY_LEFT;
                    case 0x4D: return KEY_RIGHT;
                    default: continue;
                }
            }

            /* Only handle key press (not release) */
            if (!(scancode & 0x80)) {
                char c = scancode_map[scancode & 0x7F];
                if (c) return (int)c;
            }
        }
    }
}

/* Read one ASCII character (blocking — waits for keypress) */
static inline char keyboard_read() {
    while (1) {
        int key = keyboard_read_key();
        if (key >= 0 && key <= 0x7F) return (char)key;
    }
}

#endif
