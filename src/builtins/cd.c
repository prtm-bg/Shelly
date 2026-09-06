#include "executor.h"
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int builtin_cd(char **argv) {
    const char *target = argv[1];
    char resolved[MAX_PATH * 2];
    const char *home = g_has_shell_home ? g_shell_home : getenv("HOME");
    if (!home) {
        home = getenv("HOME");
    }

    if (target == NULL || strcmp(target, "~") == 0) {
        if (home == NULL) {
            fprintf(stderr, "%scd: %sHOME not set\n", COL_BRED, COL_RESET);
            return 1;
        }
        target = home;
    }
    else if (strncmp(target, "~/", 2) == 0) {
        if (home == NULL) {
            fprintf(stderr, "%scd: %sHOME not set\n", COL_BRED, COL_RESET);
            return 1;
        }
        size_t home_len = strlen(home);
        size_t target_len = strlen(target + 2);
        if (home_len + 1 + target_len >= sizeof(resolved)) {
            fprintf(stderr, "%scd: %spath too long\n", COL_BRED, COL_RESET);
            return 1;
        }
        snprintf(resolved, sizeof(resolved), "%s/%s", home, target + 2);
        target = resolved;
    }

    if (chdir(target) != 0) {
        perror("cd");
        return 1;
    }
    return 0;
}
