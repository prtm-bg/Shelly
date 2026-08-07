#include "shell.h"

/* History storage variables */
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

    /* Open with 0600 permissions from the start (avoids race condition with umask) */
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    if (fd < 0) return;

    (void)fchmod(fd, S_IRUSR | S_IWUSR);
    FILE *f = fdopen(fd, "a");
    if (!f) {
        close(fd);
        return;
    }

    fprintf(f, "%s\n", cmd);
    fclose(f);
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

/* Load history entries from disk file */
void history_load(const char *path) {
    if (!path) return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing \r and \n */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (is_whitespace_str(line)) {
            continue;
        }

        /* Skip consecutive duplicates during load */
        if (g_history_count > 0 && strcmp(g_history[g_history_count - 1], line) == 0) {
            continue;
        }

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
                fclose(f);
                return;
            }
            g_history = new_history;
            g_history_cap = new_cap;
        }

        char *entry = strdup(line);
        if (entry) {
            g_history[g_history_count++] = entry;
        }
    }

    fclose(f);
}

/* Save full history to disk file */
void history_save(const char *path) {
    if (!path) return;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) return;

    (void)fchmod(fd, S_IRUSR | S_IWUSR);
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        return;
    }

    for (int i = 0; i < g_history_count; i++) {
        fprintf(f, "%s\n", g_history[i]);
    }

    fclose(f);
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

/* Print command history with line numbers (bash style) */
void history_print(void) {
    for (int i = 0; i < g_history_count; i++) {
        printf("%5d  %s\n", i + 1, g_history[i]);
    }
    fflush(stdout);
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
