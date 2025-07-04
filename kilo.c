#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    /*
        * To disable a specific feature like ECHO (which causes typed characters to appear on the screen), we use a bitmask operation. ECHO is a bitflag with a value like 00000000000000000000000000001000 in binary. When we apply the bitwise-NOT operator (~ECHO), we get a mask where the ECHO bit is zero and all other bits are one: 11111111111111111111111111110111.
        * By bitwise-ANDing this with the existing c_lflag, we clear only the ECHO bit, while leaving all other bits unchanged. This is a standard technique in C for disabling a specific flag.
        * // ICANON - Canonical input (erase and kill processing).
        * // ISIG - Enable signals
    */
    raw.c_iflag &= ~(ICRNL | IXON);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
        if (iscntrl(c)) {
            printf("%d\n", c);
        } else {
            printf("%d ('%c')\n", c, c);
        }
    }

    return 0;
}
