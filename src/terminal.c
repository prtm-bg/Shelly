#include "shell.h"
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>

/* Global to store original terminal attributes */
static struct termios g_orig_termios;
static int g_raw_mode_active = 0;

/* Switches shell to raw mode for input handling */
void enable_raw_mode(void) {
    struct termios shell;
    if (tcgetattr(STDIN_FILENO, &shell) == -1) {
        perror("tcgetattr() failed");
        exit(1);
    }

    /* Saving original for restoration */
    g_orig_termios = shell;
    g_raw_mode_active = 1;

    /* configured according to cfmakeraw(), removing unnecessary flags */
    shell.c_iflag &= ~(IXON | ICRNL | ISTRIP);         // disable Ctrl+S/Q, CR->NL, and preserves 8th bit (for UTF-8)
    shell.c_oflag &= ~OPOST;                           // disable output processing
    shell.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // Disable echo, canonical mode, extended input processing, and signals

    shell.c_cc[VMIN] = 1;  // read() returns when 1B available
    shell.c_cc[VTIME] = 0; // no timeout

    if (tcsetattr(STDIN_FILENO, TCSANOW, &shell) == -1) {
        perror("tcsetattr() failed");
        exit(1);
    }

    /* Verifying if tcsetattr() succeded, as it  returns 0 if any one param
     * changes successfully */
    struct termios verify;
    if (tcgetattr(STDIN_FILENO, &verify) == -1) {
        perror("tcgetattr() verification failed");
        exit(1);
    }
    /* Check critical flags */
    if ((verify.c_lflag & (ECHO | ICANON | ISIG | IEXTEN)) != 0) {
        fprintf(stderr, "Warning: lflag not fully raw (got 0%lo)\n", (unsigned long)verify.c_lflag);
    }
    if ((verify.c_iflag & (IXON | ICRNL | ISTRIP)) != 0) {
        fprintf(stderr, "Warning: iflag not fully raw (got 0%lo)\n", (unsigned long)verify.c_iflag);
    }
    if ((verify.c_oflag & OPOST) != 0) {
        fprintf(stderr, "Warning: oflag not fully raw (got 0%lo)\n", (unsigned long)verify.c_oflag);
    }
    if (verify.c_cc[VMIN] != 1 || verify.c_cc[VTIME] != 0) {
        fprintf(stderr, "Warning: VMIN/VTIME not set correctly (VMIN=%d, VTIME=%d)\n",
                verify.c_cc[VMIN], verify.c_cc[VTIME]);
    }
}

/* Switches shell back to interactive mode */
void disable_raw_mode(void) {
    if (!g_raw_mode_active)
        return;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios) == -1) {
        perror("tcsetattr() restore failed");
        /* best effort restore */
    }
    g_raw_mode_active = 0;
}

/* Calculate the visible length of a string containing ANSI escape codes */
size_t get_visible_len(const char *str) {
    size_t len = 0;
    while (*str) {
        if (*str == '\x1b') {
            str++;
            if (*str == '[') {
                str++;
                while (*str && !isalpha((unsigned char)*str)) {
                    str++;
                }
                if (*str) {
                    str++;
                }
            }
        } else {
            len++;
            str++;
        }
    }
    return len;
}
