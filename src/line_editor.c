#include "shell.h"
#include "line_editor.h"
#include "terminal.h"
#include "history_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

/* Read a line from STDIN using read() system call */
char *read_line(void) {
    size_t cap = 128;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        perror("malloc() failed");
        return NULL;
    }

    g_sigint_received = 0;

    while (1) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n < 0) {
            if (errno == EINTR) {
                if (g_sigint_received) {
                    /* Discard current input line on Ctrl+C and return empty string */
                    g_sigint_received = 0;
                    buf[0] = '\0';
                    return buf;
                }
                continue;
            }
            free(buf);
            return NULL;
        }
        if (n == 0) {
            /* EOF reached */
            if (len == 0) {
                free(buf);
                return NULL;
            }
            break;
        }
        if (c == '\n') {
            break;
        }
        buf[len++] = c;
        if (len + 1 >= cap) {
            cap *= 2;
            char *new_buf = (char *)realloc(buf, cap);
            if (!new_buf) {
                perror("realloc() failed");
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }
    }

    buf[len] = '\0';
    return buf;
}

/* Function to redraw the input line, updating the cursor position */
static void redraw_input_line(const char *prompt, size_t prompt_len_raw,
                              size_t prompt_len_vis, const char *buf,
                              size_t len, size_t cursor) {

    write(STDOUT_FILENO, "\r\033[K", 4);
    if (prompt)
        write(STDOUT_FILENO, prompt, prompt_len_raw);
    write(STDOUT_FILENO, buf, len);

    char pos[32];
    if (prompt_len_vis + cursor > 0) {
        snprintf(pos, sizeof(pos), "\r\033[%zuC", prompt_len_vis + cursor);
        write(STDOUT_FILENO, pos, strlen(pos));
    } else {
        write(STDOUT_FILENO, "\r", 1);
    }
}

/* Function to delete the word to left of the cursor */
static void delete_word_left(char *buf, size_t *len, size_t *cursor) {
    size_t pos = *cursor;

    while (pos > 0 && isspace((unsigned char)buf[pos - 1])) {
        pos--;
    }
    while (pos > 0 && !isspace((unsigned char)buf[pos - 1])) {
        pos--;
    }

    memmove(buf + pos, buf + *cursor, *len - *cursor + 1);
    *len -= *cursor - pos;
    *cursor = pos;
}

/* Function to move the cursor left by one word */
static void move_cursor_word_left(const char *buf, size_t len, size_t *cursor) {
    size_t pos = *cursor;
    // Skip any trailing whitespace
    while (pos > 0 && isspace((unsigned char)buf[pos - 1])) {
        pos--;
    }
    // Skip the word characters
    while (pos > 0 && !isspace((unsigned char)buf[pos - 1])) {
        pos--;
    }
    *cursor = pos;
}

/* Function to move the cursor right by one word */
static void move_cursor_word_right(const char *buf, size_t len, size_t *cursor) {
    size_t pos = *cursor;
    // Skip any non-whitespace characters (end of current word)
    while (pos < len && !isspace((unsigned char)buf[pos])) {
        pos++;
    }
    // Skip any whitespace
    while (pos < len && isspace((unsigned char)buf[pos])) {
        pos++;
    }
    *cursor = pos;
}

/*
** Read a line from STDIN with full cursor and history support
** (up/down arrows for history, left/right arrows, home, end, delete, backspace)
*/
char *read_line_cursor(const char *prompt) {
    size_t cap = 128;
    size_t len = 0;
    size_t cursor = 0;
    size_t prompt_len_raw = prompt ? strlen(prompt) : 0;
    size_t prompt_len_vis = prompt ? get_visible_len(prompt) : 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        perror("malloc() failed");
        return NULL;
    }
    buf[0] = '\0';

    /* History navigation state for this prompt */
    int hist_count = history_count();
    int hist_index = hist_count; /* points past the end (current uncommitted input) */
    char *saved_draft = NULL;

    /* Enable raw mode for cursor control */
    enable_raw_mode();
    g_sigint_received = 0;

    /* Print the initial prompt */
    if (prompt) {
        write(STDOUT_FILENO, prompt, prompt_len_raw);
    }

    while (1) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);

        // Handling OS-level interrupts
        if (n < 0) {
            if (errno == EINTR) {
                /* Handle SIGINT (Ctrl+C) */
                if (g_sigint_received) {
                    g_sigint_received = 0;
                    if (saved_draft) {
                        free(saved_draft);
                        saved_draft = NULL;
                    }
                    write(STDOUT_FILENO, "\r\n", 2);
                    disable_raw_mode();
                    buf[0] = '\0';
                    return buf;
                }
                continue;
            }
            if (saved_draft) free(saved_draft);
            disable_raw_mode();
            free(buf);
            return NULL;
        }
        if (n == 0) { // EOF (Ctrl+D)
            if (saved_draft) free(saved_draft);
            if (len == 0) {
                disable_raw_mode();
                free(buf);
                return NULL;
            }
            break;
        }

        // Handling User input signals
        /* Ctrl+C (SIGINT) */
        if (c == 3) {
            if (saved_draft) {
                free(saved_draft);
                saved_draft = NULL;
            }
            write(STDOUT_FILENO, "^C\r\n", 4);
            disable_raw_mode();
            buf[0] = '\0';
            return buf;
        }

        /* Ctrl+D (EOF or delete) */
        if (c == 4) {
            if (len == 0) {
                if (saved_draft) free(saved_draft);
                disable_raw_mode();
                free(buf);
                return NULL;
            } else if (cursor < len) {
                memmove(buf + cursor, buf + cursor + 1, len - cursor);
                len--;
                redraw_input_line(prompt, prompt_len_raw, prompt_len_vis, buf, len, cursor);
            }
            continue;
        }

        /* Handle escape sequences (arrow keys, home, end, delete) */
        if (c == '\x1b') {
            char seq[5];
            if (read(STDIN_FILENO, &seq[0], 1) != 1)
                continue;

            if (seq[0] == 127 || seq[0] == '\b') {
                if (cursor > 0) {
                    delete_word_left(buf, &len, &cursor);
                }
                redraw_input_line(prompt, prompt_len_raw, prompt_len_vis, buf,
                                  len, cursor);
                continue;
            }

            if (seq[0] != '[' && seq[0] != 'O')
                continue;

            if (read(STDIN_FILENO, &seq[1], 1) != 1)
                continue;

            switch (seq[1]) {
                case 'A': // Up arrow: previous history entry
                    if (hist_count > 0 && hist_index > 0) {
                        /* If leaving the current input line, save draft */
                        if (hist_index == hist_count) {
                            if (saved_draft) free(saved_draft);
                            saved_draft = strdup(buf);
                        }
                        hist_index--;
                        const char *hist_entry = history_get(hist_index);
                        if (hist_entry) {
                            size_t entry_len = strlen(hist_entry);
                            if (entry_len + 1 > cap) {
                                size_t new_cap = entry_len + 64;
                                char *new_buf = (char *)realloc(buf, new_cap);
                                if (!new_buf) {
                                    perror("realloc() failed");
                                    break;
                                }
                                buf = new_buf;
                                cap = new_cap;
                            }
                            memcpy(buf, hist_entry, entry_len + 1);
                            len = entry_len;
                            cursor = len;
                        }
                    }
                    break;

                case 'B': // Down arrow: next history entry
                    if (hist_index < hist_count) {
                        hist_index++;
                        if (hist_index == hist_count) {
                            /* Restored to bottom: show draft or empty */
                            const char *restore_str = saved_draft ? saved_draft : "";
                            size_t restore_len = strlen(restore_str);
                            if (restore_len + 1 > cap) {
                                size_t new_cap = restore_len + 64;
                                char *new_buf = (char *)realloc(buf, new_cap);
                                if (!new_buf) {
                                    perror("realloc() failed");
                                    break;
                                }
                                buf = new_buf;
                                cap = new_cap;
                            }
                            memcpy(buf, restore_str, restore_len + 1);
                            len = restore_len;
                            cursor = len;
                            if (saved_draft) {
                                free(saved_draft);
                                saved_draft = NULL;
                            }
                        } else {
                            const char *hist_entry = history_get(hist_index);
                            if (hist_entry) {
                                size_t entry_len = strlen(hist_entry);
                                if (entry_len + 1 > cap) {
                                    size_t new_cap = entry_len + 64;
                                    char *new_buf = (char *)realloc(buf, new_cap);
                                    if (!new_buf) {
                                        perror("realloc() failed");
                                        break;
                                    }
                                    buf = new_buf;
                                    cap = new_cap;
                                }
                                memcpy(buf, hist_entry, entry_len + 1);
                                len = entry_len;
                                cursor = len;
                            }
                        }
                    }
                    break;

                case 'C': // Right arrow
                    if (cursor < len)
                        cursor++;
                    break;
                case 'D': // Left arrow
                    if (cursor > 0)
                        cursor--;
                    break;
                case 'H': // Home
                    cursor = 0;
                    break;
                case 'F': // End
                    cursor = len;
                    break;
                case '1': // Home (ESC [ 1 ~) or Ctrl+Left/Right (ESC [ 1 ; 5 ~)
                case '7': // Home (ESC [ 7 ~)
                    if (read(STDIN_FILENO, &seq[2], 1) == 1) {
                        if (seq[2] == '~') {
                            // Home key
                            cursor = 0;
                        } else if (seq[2] == ';') {
                            // Extended sequence: ESC [ 1 ; 5 D/C
                            if (read(STDIN_FILENO, &seq[3], 1) == 1 && seq[3] == '5') {
                                if (read(STDIN_FILENO, &seq[4], 1) == 1) {
                                    if (seq[4] == 'D') { // Ctrl+Left
                                        if (cursor > 0) {
                                            move_cursor_word_left(buf, len, &cursor);
                                        }
                                    } else if (seq[4] == 'C') { // Ctrl+Right
                                        if (cursor < len) {
                                            move_cursor_word_right(buf, len, &cursor);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    break;
                case '4': // End (ESC [ 4 ~)
                case '8': // End (ESC [ 8 ~)
                    if (read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~') {
                        cursor = len;
                    }
                    break;
                case '3': // Delete key (ESC [ 3 ~)
                    if (read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~') {
                        if (cursor < len) {
                            memmove(buf + cursor, buf + cursor + 1, len - cursor);
                            len--;
                        }
                    }
                    break;
            }
            redraw_input_line(prompt, prompt_len_raw, prompt_len_vis, buf, len, cursor);
            continue;
        }

        /* Enter key */
        if (c == '\n' || c == '\r') {
            if (saved_draft) {
                free(saved_draft);
                saved_draft = NULL;
            }
            write(STDOUT_FILENO, "\r\n", 2);
            break;
        }

        /* Backspace (127) or Ctrl+H (8) */
        if (c == 127 || c == '\b') {
            if (cursor > 0) {
                memmove(buf + cursor - 1, buf + cursor, len - cursor + 1);
                cursor--;
                len--;
            }
            redraw_input_line(prompt, prompt_len_raw, prompt_len_vis, buf, len, cursor);
            continue;
        }

        /* Ctrl+W: Delete word left */
        if (c == 23) {
            if (cursor > 0) {
                delete_word_left(buf, &len, &cursor);
            }
            redraw_input_line(prompt, prompt_len_raw, prompt_len_vis, buf, len, cursor);
            continue;
        }

        /* Printable character */
        if (c >= 32 && c <= 126) {
            if (len + 1 >= cap) {
                size_t new_cap = cap * 2;
                char *new_buf = (char *)realloc(buf, new_cap);
                if (!new_buf)
                {
                    perror("realloc() failed");
                    if (saved_draft) free(saved_draft);
                    disable_raw_mode();
                    free(buf);
                    return NULL;
                }
                buf = new_buf;
                cap = new_cap;
            }
            memmove(buf + cursor + 1, buf + cursor, len - cursor + 1);
            buf[cursor] = c;
            cursor++;
            len++;

            write(STDOUT_FILENO, "\r\033[K", 4);
            if (prompt){
                write(STDOUT_FILENO, prompt, prompt_len_raw);
            }
            write(STDOUT_FILENO, buf, len);

            char pos[32];
            if (prompt_len_vis + cursor > 0) {
                snprintf(pos, sizeof(pos), "\r\033[%zuC", prompt_len_vis + cursor);
                write(STDOUT_FILENO, pos, strlen(pos));
            } else {
                write(STDOUT_FILENO, "\r", 1);
            }
        }
    }

    if (saved_draft) {
        free(saved_draft);
        saved_draft = NULL;
    }
    disable_raw_mode();
    buf[len] = '\0';
    return buf;
}
