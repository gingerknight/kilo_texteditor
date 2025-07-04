/*** includes ***/

/*
 * These standard headers are needed for basic system and terminal manipulation:
 */

#include <ctype.h>      // iscntrl(), checks for control characters like Ctrl-C
#include <errno.h>      // errno variable and error codes
#include <stdio.h>      // printf(), perror()
#include <stdlib.h>     // exit(), atexit()
#include <sys/ioctl.h>  // TIOCGWINSZ (Terminal IOCtl Get WINdow SiZe)
#include <termios.h>    // terminal I/O interfaces (tcgetattr(), tcsetattr())
#include <unistd.h>     // read(), STDIN_FILENO

/*** defines ***/

/*
 * Detect specific ctrl+keys combinations based on the bytes output we disabled below as raw input.
 * Ctrl versions of letters are just the normal characters bitwise-ANDed with 0x1f
 * Ctrl+Q --> Quit
 *    0111 0001   ('q')
 * &  0001 1111   (0x1f)
 * ------------
 *    0001 0001   = 17 = Ctrl-Q
 *
 */

#define CTRL_KEY(letter) ((letter) & 0x1f)

/*** data ***/

/*
 * Store the original terminal settings here so we can restore them later
 * when the program exits or crashes. This prevents the terminal from staying
 * in raw mode after quitting the editor.
 */
struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

/*
 * die() prints an error message and exits.
 * It uses perror() to describe the most recent system call failure.
 */
void die(const char *s) {
    // Clear screen on exit
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

/*
 * disableRawMode() restores the terminal settings to their original state.
 * This gets registered with atexit() so it's called when the program exits,
 * even if it crashes or exits early.
 */
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

/*
 * enableRawMode() puts the terminal into "raw mode", turning off features like
 * line buffering, echo, signal generation, etc. This is needed for real-time
 * key input handling in a text editor.
 */
void enableRawMode() {
    // Get the current terminal attributes and store them for later restoration
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

    // Ensure disableRawMode() is called on program exit
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    /*
     * Input flags (c_iflag):
     *  - BRKINT: disable Ctrl-C generating a SIGINT when break condition is detected
     *  - ICRNL: disable conversion of CR to NL (allows raw handling of Enter)
     *  - INPCK and ISTRIP: legacy/rare input features, we disable them for raw
     *  - IXON: disable software flow control (Ctrl-S, Ctrl-Q)
     */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    /*
     * Output flags (c_oflag):
     *  - OPOST: disable all post-processing of output (e.g., converting `\n` to `\r\n`)
     */
    raw.c_oflag &= ~(OPOST);

    /*
     * Control flags (c_cflag):
     *  - CS8: set character size to 8 bits per byte (standard)
     */
    raw.c_cflag |= (CS8);

    /*
     * Local flags (c_lflag):
     *  - ECHO: disable echoing input characters to the screen
     *  - ICANON: disable canonical mode (so input is read byte-by-byte)
     *  - IEXTEN: disable Ctrl-V and other extended input processing
     *  - ISIG: disable signal generation from keys like Ctrl-C (SIGINT), Ctrl-Z (SIGTSTP)
     */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /*
     * Control characters (c_cc):
     *  - VMIN = 0: read returns as soon as there's any input (non-blocking read)
     *  - VTIME = 1: set a 100ms timeout for read()
     *    This allows read to block briefly and return if no input is given,
     *    so we don't burn CPU in a tight polling loop.
     */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    // Apply the modified attributes immediately
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/* Wait for one keypress and return it
 * <Currently cannot handle multi-byte things like Arrow keys>
 */
char editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

/*
 * Get window size col and row
 */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** output ***/

/*
 * Write column of ~ like vim
 */
void editorDrawRows() {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}
/*
 * write 4 bytes with escape sequence. Using the vt100 escape sequences.
 * The \x1b is escape character 27
 * https://vt100.net/docs/vt100-ug/chapter3.html#ED
 * <esc>[2J --> Erase all of the display – all lines are erased, changed to single-width, and the cursor does not move.
 * */
void editorRefreshScreen() {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    editorDrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** input ***/

void editorProcessKeypress() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** init ***/

/*
 * This is to initilize all the fiedls in the E struct
 */
void initEditor() {
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

/*
 * Entry point for the program. Enables raw mode and enters an input loop.
 * Pressing 'q' exits the program.
 */
int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
