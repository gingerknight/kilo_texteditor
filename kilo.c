/*** includes ***/

/*
 * These standard headers are needed for basic system and terminal manipulation:
 */

#include <ctype.h>   // iscntrl(), checks for control characters like Ctrl-C
#include <errno.h>   // errno variable and error codes
#include <stdio.h>   // printf(), perror()
#include <stdlib.h>  // exit(), atexit()
#include <termios.h> // terminal I/O interfaces (tcgetattr(), tcsetattr())
#include <unistd.h>  // read(), STDIN_FILENO

/*** data ***/

/*
 * Store the original terminal settings here so we can restore them later
 * when the program exits or crashes. This prevents the terminal from staying
 * in raw mode after quitting the editor.
 */
struct termios orig_termios;

/*** terminal ***/

/*
 * die() prints an error message and exits.
 * It uses perror() to describe the most recent system call failure.
 */
void
die (const char *s)
{
    perror (s);
    exit (1);
}

/*
 * disableRawMode() restores the terminal settings to their original state.
 * This gets registered with atexit() so it's called when the program exits,
 * even if it crashes or exits early.
 */
void
disableRawMode ()
{
    if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die ("tcsetattr");
}

/*
 * enableRawMode() puts the terminal into "raw mode", turning off features like
 * line buffering, echo, signal generation, etc. This is needed for real-time
 * key input handling in a text editor.
 */
void
enableRawMode ()
{
    // Get the current terminal attributes and store them for later restoration
    if (tcgetattr (STDIN_FILENO, &orig_termios) == -1)
        die ("tcgetattr");

    // Ensure disableRawMode() is called on program exit
    atexit (disableRawMode);

    struct termios raw = orig_termios;

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
    if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die ("tcsetattr");
}

/*** init ***/

/*
 * Entry point for the program. Enables raw mode and enters an input loop.
 * Pressing 'q' exits the program.
 */
int
main ()
{
    enableRawMode ();

    while (1)
        {
            char c = '\0';

            /*
             * Read one byte from standard input.
             * - STDIN_FILENO is file descriptor 0 (standard input)
             * - If no input is available, read() will wait up to 100ms
             * - If read fails with a real error (not just timeout), die
             */
            if (read (STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
                die ("read");

            /*
             * iscntrl() checks if the character is a control character (e.g., Ctrl-C)
             * If so, we print just the numeric value.
             * Otherwise, we print both the number and the actual character.
             */
            if (iscntrl (c))
                {
                    printf ("%d\r\n", c);
                }
            else
                {
                    printf ("%d ('%c')\r\n", c, c);
                }

            // Pressing 'q' exits the loop and quits the program
            if (c == 'q')
                break;
        }

    return 0;
}
