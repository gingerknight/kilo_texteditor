/*** includes ***/

/*
 * These standard headers are needed for basic system and terminal manipulation:
 */

#include <ctype.h>      // iscntrl(), checks for control characters like Ctrl-C
#include <errno.h>      // errno variable and error codes
#include <stdio.h>      // printf(), perror()
#include <stdlib.h>     // exit(), atexit()
#include <string.h>     //memcpy()
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
#define RYEDOC_VERSION "0.0.1"

/*** data ***/

/*
 * Store the original terminal settings here so we can restore them later
 * when the program exits or crashes. This prevents the terminal from staying
 * in raw mode after quitting the editor.
 */
struct editorConfig {
    int cx, cy;
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

    if (c == '\x1b') {
        char seq[3];  // grab value after escape sequence

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A':
                    return 'j';
                case 'B':
                    return 'k';
                case 'C':
                    return 'l';
                case 'D':
                    return 'h';
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return -1;
}
/*
 * Get window size col and row
 */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;  // https://vt100.net/docs/vt100-ug/chapter3.html#CUD
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** append buffer ***/

/*
 * Replace write() calls with code that appends the string to a buffer, then writes
 * Prevents flicker affect with multiple write calls for all the ~ and whatever we are typing
 */

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

/*
 * Allocate anough memory via realloc (grow current stack, or free and alloc new size)
 * to hold current string, and what we are appending
 * Use memcpy copy the string after end of current data in buffer then update *ptr and len
 */
void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}
/*
 * Free our struct after we are done, always clean your messes
 */
void abFree(struct abuf *ab) { free(ab->b); }

/*** output ***/

/*
 * Write column of ~ like vim
 */
void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        if (y == E.screenrows / 3) {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), "RyeRye editor --version %s", RYEDOC_VERSION);
            if (welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding--) abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);  // clear each line (erase in line)
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}
/*
 * write 4 bytes with escape sequence. Using the vt100 escape sequences.
 * The \x1b is escape character 27
 * https://vt100.net/docs/vt100-ug/chapter3.html#ED
 * <esc>[2J --> Erase all of the display – all lines are erased, changed to single-width, and the cursor does not
 * move.
 * */
void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1[?25l", 6);  // hide cursor https://vt100.net/docs/vt510-rm/DECTCEM.html
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    // move cursor to E.cx / E.cy
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);  // cursor show

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/

void editorMoveCursor(char key) {
    switch (key) {
        case 'h':
            E.cx--;
            break;
        case 'j':
            E.cy--;
            break;
        case 'k':
            E.cy++;
            break;
        case 'l':
            E.cx++;
            break;
    }
}

void editorProcessKeypress() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case 'h':
        case 'j':
        case 'k':
        case 'l':
            editorMoveCursor(c);
            break;
    }
}

/*** init ***/

/*
 * This is to initilize all the fiedls in the E struct
 */
void initEditor() {
    E.cx = 0;
    E.cy = 0;

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
