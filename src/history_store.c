#include "history_store.h"
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>

#define MAX_HISTORY_DEFAULT 1000

static char **g_history = NULL;
static int g_history_count = 0;
static int g_history_cap = 0;
static int g_history_max = MAX_HISTORY_DEFAULT;
static char g_history_path[MAX_PATH] = {0};

/* Get or determine the path to the history file (~/.shelly_history) */
const char *get_history_file_path(void) {
    if (g_history_path[0] != '\0') {
        return g_history_path;
    }

    const char *home = getenv("HOME");
    if (!home && g_has_shell_home) {
        home = g_shell_home;
    }

    if (home) {
        snprintf(g_history_path, sizeof(g_history_path), "%s/.shelly_history", home);
    } else {
        snprintf(g_history_path, sizeof(g_history_path), ".shelly_history");
    }

    return g_history_path;
}

/* Returns the total number of history entries */
int history_count(void) {
    return g_history_count;
}

/* Returns the history entry at a specific index (0 is oldest) */
const char *history_get(int index) {
    if (index < 0 || index >= g_history_count) {
        return NULL;
    }
    return g_history[index];
}

/* Internal helper to check if a string is empty or whitespace-only */
static int is_whitespace_str(const char *s) {
    if (!s) return 1;
    while (*s) {
        if (!isspace((unsigned char)*s))
            return 0;
        s++;
    }
    return 1;
}

/* Initialize history subsystem and load persistent history from disk */
void history_init(void) {
    if (g_history == NULL) {
        g_history_cap = 64;
        g_history = (char **)malloc((size_t)g_history_cap * sizeof(char *));
        if (!g_history) {
            perror("malloc() failed for history");
            return;
        }
        g_history_count = 0;
    }

    const char *path = get_history_file_path();
    history_load(path);
}

/* Append a command to the persistent history file */
static void append_to_history_file(const char *cmd) {
    const char *path = get_history_file_path();
    if (!path || path[0] == '\0') return;

    /* Opening history file */
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;

    (void)fchmod(fd, 0600);
    size_t cmd_len = strlen(cmd);
    write(fd, cmd, cmd_len);
    write(fd, "\n", 1);
    close(fd);
}

/* Add a command entry to history */
void history_add(const char *cmd) {
    if (!cmd || is_whitespace_str(cmd)) {
        return;
    }

    /* Skip consecutive duplicate entries (bash HISTCONTROL=ignoredups) */
    if (g_history_count > 0 && strcmp(g_history[g_history_count - 1], cmd) == 0) {
        return;
    }

    /* If history reaches max capacity, remove oldest entry */
    if (g_history_count >= g_history_max) {
        free(g_history[0]);
        memmove(&g_history[0], &g_history[1], (size_t)(g_history_count - 1) * sizeof(char *));
        g_history_count--;
    }

    /* Grow storage capacity if needed */
    if (g_history_count >= g_history_cap) {
        int new_cap = (g_history_cap == 0) ? 64 : g_history_cap * 2;
        char **new_history = (char **)realloc(g_history, (size_t)new_cap * sizeof(char *));
        if (!new_history) {
            perror("realloc() failed for history");
            return;
        }
        g_history = new_history;
        g_history_cap = new_cap;
    }

    char *entry = strdup(cmd);
    if (!entry) {
        perror("strdup() failed for history entry");
        return;
    }

    g_history[g_history_count++] = entry;
    append_to_history_file(cmd);
}

/* Load history entries from disk file using read() system call */
void history_load(const char *path) {
    if (!path) return;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return;

    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size <= 0) {
        close(fd);
        return;
    }

    char *buf = (char *)malloc((size_t)st.st_size + 1);
    if (!buf) {
        close(fd);
        return;
    }

    ssize_t total = 0;
    while (total < st.st_size) {
        ssize_t n = read(fd, buf + total, (size_t)(st.st_size - total));
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    close(fd);

    /* Tokenize lines from buffer */
    char *line_start = buf;
    for (ssize_t i = 0; i <= total; i++) {
        if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\0') {
            buf[i] = '\0';
            if (line_start < &buf[i] && !is_whitespace_str(line_start)) {
                /* Skip consecutive duplicates during load */
                if (g_history_count == 0 || strcmp(g_history[g_history_count - 1], line_start) != 0) {
                    /* Maintain max capacity */
                    if (g_history_count >= g_history_max) {
                        free(g_history[0]);
                        memmove(&g_history[0], &g_history[1], (size_t)(g_history_count - 1) * sizeof(char *));
                        g_history_count--;
                    }

                    if (g_history_count >= g_history_cap) {
                        int new_cap = (g_history_cap == 0) ? 64 : g_history_cap * 2;
                        char **new_history = (char **)realloc(g_history, (size_t)new_cap * sizeof(char *));
                        if (!new_history) {
                            perror("realloc() failed for history");
                            free(buf);
                            return;
                        }
                        g_history = new_history;
                        g_history_cap = new_cap;
                    }

                    char *entry = strdup(line_start);
                    if (entry) {
                        g_history[g_history_count++] = entry;
                    }
                }
            }
            line_start = &buf[i + 1];
        }
    }

    free(buf);
}

/* Save full history to disk file using write() system call */
void history_save(const char *path) {
    if (!path) return;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) return;

    (void)fchmod(fd, S_IRUSR | S_IWUSR);
    for (int i = 0; i < g_history_count; i++) {
        write(fd, g_history[i], strlen(g_history[i]));
        write(fd, "\n", 1);
    }

    close(fd);
}

/* Clear all history entries (history -c) */
void history_clear(void) {
    if (g_history) {
        for (int i = 0; i < g_history_count; i++) {
            free(g_history[i]);
        }
        g_history_count = 0;
    }

    const char *path = get_history_file_path();
    if (path) {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (fd >= 0) {
            (void)fchmod(fd, S_IRUSR | S_IWUSR);
            close(fd);
        }
    }
}

/* Print command history with line numbers using write() system call (bash style) */
void history_print(void) {
    char num_buf[32];
    for (int i = 0; i < g_history_count; i++) {
        int n = snprintf(num_buf, sizeof(num_buf), "%5d  ", i + 1);
        if (n > 0) {
            write(STDOUT_FILENO, num_buf, (size_t)n);
        }
        write(STDOUT_FILENO, g_history[i], strlen(g_history[i]));
        write(STDOUT_FILENO, "\n", 1);
    }
}

/* Free all history allocated memory */
void history_free(void) {
    if (g_history) {
        for (int i = 0; i < g_history_count; i++) {
            free(g_history[i]);
        }
        free(g_history);
        g_history = NULL;
    }
    g_history_count = 0;
    g_history_cap = 0;
}
