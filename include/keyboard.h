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

/* US QWERTY scancode to ASCII map */
static const char scancode_map[128] = {
    0,   0,  '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
  '\t', 'q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
   '*',  0, ' '
};

/* Read one ASCII character (blocking — waits for keypress) */
static inline char keyboard_read() {
    unsigned char scancode;
    /* Wait until keyboard buffer is full */
    while (1) {
        if (inb(KEYBOARD_STATUS_PORT) & 0x1) {
            scancode = inb(KEYBOARD_DATA_PORT);
            /* Only handle key press (not release) */
            if (!(scancode & 0x80)) {
                char c = scancode_map[scancode & 0x7F];
                if (c) return c;
            }
        }
    }
}

#endif
