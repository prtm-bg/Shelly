#include "shell.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

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
