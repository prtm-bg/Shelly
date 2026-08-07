#include "shell.h"


/* Global variables for shell home directory */
int g_has_shell_home = 0;
char g_shell_home[MAX_PATH];


/* ---- Signal Handlers ---- */
/* SIGCHLD handler for background jobs */
void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

volatile sig_atomic_t g_sigint_received = 0;
/* SIGINT handler to avoid killing the shell prompt */
void sigint_handler(int sig) {
    (void)sig;
    g_sigint_received = 1;
    write(STDOUT_FILENO, "\n", 1);
}


/* ---- Shell Modes ---- */
// struct termios {
//     tcflag_t c_iflag;      /* input modes */
//     tcflag_t c_oflag;      /* output modes */
//     tcflag_t c_cflag;      /* control modes */
//     tcflag_t c_lflag;      /* local modes */
//     cc_t     c_cc[NCCS];   /* special characters */
// }

// int tcgetattr(int fd, struct termios *termios_p);
// int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);

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
    /* NOTE: For DEBUGGING, might be removed in future iterations */
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
        fprintf(stderr,
                "Warning: VMIN/VTIME not set correctly (VMIN=%d, VTIME=%d)\n",
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


/* ---- Input Handling ---- */

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


/* ---- Helper functions for interactive read from STDIN ---- */

/* Function to calculate the visible length of a string containing ANSI escape codes */
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
        }
        else {
            len++;
            str++;
        }
    }
    return len;
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

        /* Handle escape sequences (arrow keys, home, end, delete) */
        if (c == '\x1b') {
            char seq[3];
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
                case '1': // Home (ESC [ 1 ~)
                case '7': // Home (ESC [ 7 ~)
                    if (read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~') {
                        cursor = 0;
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
            redraw_input_line(prompt, prompt_len_raw, prompt_len_vis, buf,
                              len, cursor);
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
            redraw_input_line(prompt, prompt_len_raw, prompt_len_vis, buf,
                              len, cursor);
            continue;
        }

        /* Ctrl+W */
        if (c == 23) {
            if (cursor > 0) {
                delete_word_left(buf, &len, &cursor);
            }
            redraw_input_line(prompt, prompt_len_raw, prompt_len_vis, buf,
                              len, cursor);
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


/* Check if string is empty or contains only whitespace */
static int is_empty_line(const char *str) {
    if (!str)
        return 1;
    while (*str) {
        if (!isspace((unsigned char)*str))
            return 0;
        str++;
    }
    return 1;
}

int g_use_color = 1;


/* Check command line arguments for --no-color, -n, --color=never */
void init_color_support(int argc, char *argv[]) {
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-color") == 0 || strcmp(argv[i], "-n") == 0 ||
            strcmp(argv[i], "--color=never") == 0) {
            g_use_color = 0;
            return;
        }
    }

    /* Check standard NO_COLOR environment variable (http://no-color.org/) */
    if (getenv("NO_COLOR") != NULL) {
        g_use_color = 0;
        return;
    }

    /* Check SHELLY_NO_COLOR environment variable */
    if (getenv("SHELLY_NO_COLOR") != NULL) {
        g_use_color = 0;
        return;
    }

    /* Check TERM environment variable for dumb/unsupported terminal */
    const char *term = getenv("TERM");
    if (term == NULL || strcmp(term, "dumb") == 0) {
        g_use_color = 0;
        return;
    }

    /* If stdout is not a TTY (e.g. redirected to a file), disable colors */
    if (!isatty(STDOUT_FILENO)) {
        g_use_color = 0;
        return;
    }

    g_use_color = 1;
}



/* driver fn */
int main(int argc, char *argv[])
{
    uid_t euid;
    struct passwd *user_data;
    char cwd[MAX_PATH];
    char display_path[260]; /* Capped to 256 + "..." + null terminator */
    char *input = NULL;
    Token *tokens = NULL;
    int token_count = 0;
    AST *root = NULL;

    /* Initialize color support */
    init_color_support(argc, argv);

    /* Setting shell home dir */
    if (getcwd(g_shell_home, sizeof(g_shell_home)) == NULL) {
        perror("getcwd() failed");
        exit(1);
    }
    g_has_shell_home = 1;

    /* Initialize command history subsystem */
    history_init();

    /* Welcome banner: display only in interactive mode */
    if (isatty(STDIN_FILENO)) {
        printf("\n");
        printf("%s  ____  _          _ _       %s\n", COL_BCYAN, COL_RESET);
        printf("%s / ___|| |__   ___| | |_   _ %s\n", COL_BCYAN, COL_RESET);
        printf("%s \\___ \\| '_ \\ / _ \\ | | | | |%s\n", COL_BCYAN, COL_RESET);
        printf("%s  ___) | | | |  __/ | | |_| |%s\n", COL_BCYAN, COL_RESET);
        printf("%s |____/|_| |_|\\___|_|_|\\__, |%s\n", COL_BCYAN, COL_RESET);
        printf("%s                        |___/ %s\n", COL_BCYAN, COL_RESET);
        printf("%s  Version 2.1.22%s\n", COL_DIM, COL_RESET);
        printf("\n");
        fflush(stdout);
    }

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction(SIGCHLD)");
        exit(1);
    }

    struct sigaction sa_int;
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags =
        0; /* Do not restart syscalls on SIGINT so read() returns EINTR */
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("sigaction(SIGINT)");
        exit(1);
    }

    do {
        /* (1.) show the shell prompt */
        euid = geteuid();
        user_data = getpwuid(euid);
        const char *user_name = user_data ? user_data->pw_name : "user";

        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            strncpy(hostname, "localhost", sizeof(hostname) - 1);
            hostname[sizeof(hostname) - 1] = '\0';
        }

        if (getcwd(cwd, MAX_PATH) == NULL) {
            perror("getcwd() failed");
            exit(1);
        }

        /* If shell home is set */
        if (g_has_shell_home) {
            size_t home_len = strlen(g_shell_home);
            /* Home dir represented by ~ */
            if (strcmp(cwd, g_shell_home) == 0) {
                snprintf(display_path, sizeof(display_path), "~");
            }
            /* Sub-directory of home dir */
            else if (strncmp(cwd, g_shell_home, home_len) == 0 && cwd[home_len] == '/') {
                display_path[0] = '~';
                strncpy(display_path + 1, cwd + home_len, sizeof(display_path) - 2);
                display_path[sizeof(display_path) - 1] = '\0';
            }
            /* Other directories */
            else {
                strncpy(display_path, cwd, sizeof(display_path) - 1);
                display_path[sizeof(display_path) - 1] = '\0';
            }
        }
        else {
            strncpy(display_path, cwd, sizeof(display_path) - 1);
            display_path[sizeof(display_path) - 1] = '\0';
        }

        /* Cap display_path to 256 chars to prevent prompt overflow */
        if (strlen(display_path) > 256) {
            display_path[256] = '\0';
            strcat(display_path, "...");
        }

        /* Build prompt string */
        char prompt_str[1024]; /* Now safely smaller */
        if ((int)euid == 0) {
            snprintf(prompt_str, sizeof(prompt_str), "%s%s%s@%s%s%s%s:%s[%s]%s%s#%s ",
                     COL_BGREEN, user_name, COL_DIM, COL_RESET, COL_GREEN, hostname,
                     COL_RESET, COL_BCYAN, display_path, COL_RESET, COL_BYELLOW,
                     COL_RESET);
        }
        else {
            snprintf(prompt_str, sizeof(prompt_str), "%s%s%s@%s%s%s%s:%s[%s]%s ",
                     COL_BGREEN, user_name, COL_DIM, COL_RESET, COL_GREEN, hostname,
                     COL_RESET, COL_BCYAN, display_path, COL_RESET);
        }

        /* (2.) read a line */
        if (isatty(STDIN_FILENO)) {
            input = read_line_cursor(prompt_str); // Interactive TTY input with prompt
        }
        else {
            printf("%s", prompt_str); // Print prompt for non-TTY
            fflush(stdout);
            input = read_line(); // Non-TTY input (piped/scripted)
        }

        /* Handle EOF */
        if (!input) {
            break;
        }

        /* Skip blank lines */
        if (is_empty_line(input)) {
            free(input);
            input = NULL;
            continue;
        }

        /* Record valid command in history (interactive sessions only) */
        if (isatty(STDIN_FILENO)) {
            history_add(input);
        }

        /* (3.) parse cmd into tokens */
        if (tokenize_input(input, &tokens, &token_count) < 0) {
            fprintf(stderr, "%serror: %stokenization failed\n", COL_BRED, COL_RESET);
            free(input);
            input = NULL;
            continue;
        }

        /* If only EOF token (e.g. comment-only line), skip execution */
        if (token_count <= 1 || tokens[0].type == TOK_EOF) {
            free_tokens(tokens, token_count);
            tokens = NULL;
            token_count = 0;

            free(input);
            input = NULL;
            continue;
        }

        /* (4.) parse tokens to AST */
        if (parse_tokens_to_ast(tokens, token_count, &root) == 0) {
            /* (5.) execute AST */
            execute_ast(root);
            free_ast(root);
            root = NULL;
        }
        else {
            root = NULL;
        }

        /* Cleanup */
        free_tokens(tokens, token_count);
        tokens = NULL;
        token_count = 0;
        free(input);
        input = NULL;

    } while (1);

    history_free();
    return 0;
}