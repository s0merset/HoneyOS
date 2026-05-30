#ifndef KEYBOARD_H
#define KEYBOARD_H

/*
 * keyboard.h — PS/2 Keyboard Input (Polling Mode)
 *
 * Provides a blocking keyboard_read() that spins until a key is
 * pressed and returns the corresponding ASCII character.
 *
 * HoneyOS does not use interrupts for keyboard input — it polls
 * the keyboard status port directly. This keeps the code simple
 * since there is no interrupt handler infrastructure yet.
 *
 * All functions are static inline so they live in this header
 * and do not need a separate .c file.
 */

/*
 * inb — Read one byte from an x86 I/O port.
 * Used to communicate with hardware like the keyboard controller.
 * "volatile" prevents the compiler from optimizing away the I/O access.
 */
static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ── PS/2 Keyboard Controller I/O Ports ── */
#define KEYBOARD_DATA_PORT    0x60    /* Read scancodes and keyboard data from here */
#define KEYBOARD_STATUS_PORT  0x64    /* Bit 0 = output buffer full (data ready to read) */

/* Special key codes (outside ASCII range) */
#define KEY_UP     0x100
#define KEY_DOWN   0x101
#define KEY_LEFT   0x102
#define KEY_RIGHT  0x103

/* US QWERTY scancode to ASCII map */
static const char scancode_map[128] = {
    0,  27,  '1','2','3','4','5','6','7','8','9','0','-','=', '\b',    /* 0x00–0x0E: ESC, digits, backspace */
  '\t', 'q','w','e','r','t','y','u','i','o','p','[',']','\n',    /* 0x0F–0x1C: tab, QWERTY top row, enter */
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',    /* 0x1D–0x29: ctrl, home row, backtick */
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,    /* 0x2A–0x35: shift, bottom row */
   '*',  0, ' '    /* 0x37–0x39: keypad *, alt, spacebar */
};

/* Read one key (blocking). Returns ASCII or KEY_* for arrows. */
static inline int keyboard_read_key() {
    unsigned char scancode;
    while (1) {
        /* Wait until the keyboard controller signals data is available */
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
