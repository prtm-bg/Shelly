#include "executor.h"
#include "shell.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int builtin_pwd(void) {
    char current_dir[MAX_PATH];
    if (getcwd(current_dir, sizeof(current_dir)) != NULL) {
        write(STDOUT_FILENO, current_dir, strlen(current_dir));
        write(STDOUT_FILENO, "\n", 1);
        return 0;
    }
    perror("pwd");
    return 1;
}
