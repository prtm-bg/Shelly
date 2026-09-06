#include "shell.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>

void build_prompt(char *prompt_str, size_t size) {
    char cwd[MAX_PATH];
    char display_path[260]; /* Capped to 256 + "..." + null terminator */
    char hostname[256];
    
    uid_t euid = geteuid();
    struct passwd *user_data = getpwuid(euid);
    const char *user_name = user_data ? user_data->pw_name : "user";

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strncpy(hostname, "localhost", sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
    }

    if (getcwd(cwd, MAX_PATH) == NULL) {
        perror("getcwd() failed");
        snprintf(cwd, MAX_PATH, "unknown");
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
            snprintf(display_path, sizeof(display_path), "~%.255s", cwd + home_len);
        }
        /* Other directories */
        else {
            snprintf(display_path, sizeof(display_path), "%.256s", cwd);
        }
    } else {
        snprintf(display_path, sizeof(display_path), "%.256s", cwd);
    }

    /* Cap display_path to 256 chars to prevent prompt overflow */
    if (strlen(display_path) > 256) {
        display_path[256] = '\0';
        strcat(display_path, "...");
    }

    /* Build prompt string */
    if ((int)euid == 0) {
        snprintf(prompt_str, size, "%s%s%s@%s%s%s:%s%s[%s%s%s%s]%s%s#%s ",
                 COL_BGREEN, user_name, COL_DIM, COL_RESET, COL_GREEN, hostname, COL_RESET,
                 COL_BCYAN, COL_YELLOW, display_path, COL_RESET, COL_BCYAN, COL_RESET, COL_BRED,
                 COL_RESET);
    } else {
        snprintf(prompt_str, size, "%s%s%s@%s%s%s:%s%s[%s%s%s%s]%s%s>%s ",
                 COL_BGREEN, user_name, COL_DIM, COL_RESET, COL_GREEN, hostname, COL_RESET,
                 COL_BCYAN, COL_YELLOW, display_path, COL_RESET, COL_BCYAN, COL_RESET,
                 COL_MAGENTA, COL_RESET);
    }
}
