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

/*
 * scancode_map — US QWERTY PS/2 Set-1 scancode to ASCII translation table.
 *
 * Index = scancode byte received from port 0x60.
 * Value = corresponding ASCII character (0 = no mapping / special key).
 *
 * Only covers the basic printable characters and control keys
 * (backspace, tab, enter, space) needed by HoneyOS.
 */
static const char scancode_map[128] = {
    0,  27,  '1','2','3','4','5','6','7','8','9','0','-','=', '\b',    /* 0x00–0x0E: ESC, digits, backspace */
  '\t', 'q','w','e','r','t','y','u','i','o','p','[',']','\n',    /* 0x0F–0x1C: tab, QWERTY top row, enter */
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',    /* 0x1D–0x29: ctrl, home row, backtick */
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,    /* 0x2A–0x35: shift, bottom row */
   '*',  0, ' '    /* 0x37–0x39: keypad *, alt, spacebar */
};

/*
 * keyboard_read — Block until a valid ASCII key is pressed, then return it.
 *
 * Polling loop:
 *   1. Read keyboard status port (0x64)
 *   2. If bit 0 is set, the output buffer has a scancode ready
 *   3. Read the scancode from port 0x60
 *   4. Ignore key-release events (scancode bit 7 set)
 *   5. Map scancode to ASCII; if valid (non-zero), return it
 */
static inline char keyboard_read() {
    unsigned char scancode;
    /* Wait until keyboard buffer is full */
    while (1) {
        /* Wait until the keyboard controller signals data is available */
        if (inb(KEYBOARD_STATUS_PORT) & 0x1) {
            scancode = inb(KEYBOARD_DATA_PORT);

            /* Bit 7 of the scancode = key release event; we only want key presses */
            if (!(scancode & 0x80)) {
                char c = scancode_map[scancode & 0x7F];
                if (c) return c; /* Return ASCII if mapping exists, else keep polling */
            }
        }
    }
}

#endif
