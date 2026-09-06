#include "shell.h"
#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "executor.h"
#include "history_store.h"
#include "line_editor.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>

/* Global variables for shell home directory */
int g_has_shell_home = 0;
char g_shell_home[MAX_PATH];
volatile sig_atomic_t g_sigint_received = 0;

/* ---- Signal Handlers ---- */
void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

void sigint_handler(int sig) {
    (void)sig;
    g_sigint_received = 1;
    write(STDOUT_FILENO, "\n", 1);
}

/* Check if string is empty or contains only whitespace */
static int is_empty_line(const char *str) {
    if (!str){
        return 1;
    }

    while (*str) {
        if (!isspace((unsigned char)*str))
            return 0;
        str++;
    }
    return 1;
}

/* driver fn */
int main(int argc, char *argv[]) {
    char *input = NULL;
    Token *tokens = NULL;
    int token_count = 0;
    AST *root = NULL;

    /* Initialize color support */
    init_color_support(argc, argv);

    /* Get home directory from environment or passwd */
    const char *home_dir = NULL;
    if (geteuid() == getuid()) {
        home_dir = getenv("HOME");
        if (home_dir != NULL && home_dir[0] == '\0') {
            home_dir = NULL;
        }
    }
    if (home_dir == NULL) {
        struct passwd *pw = getpwuid(getuid());
        if (pw != NULL && pw->pw_dir != NULL && pw->pw_dir[0] != '\0') {
            home_dir = pw->pw_dir;
        }
    }
    /* Change to home directory if found */
    if (home_dir != NULL) {
        if (chdir(home_dir) != 0) {
            perror("chdir() to home directory failed");
        }
    }
    /* Setting shell home dir */
    if (getcwd(g_shell_home, sizeof(g_shell_home)) == NULL) {
        perror("getcwd() failed");
        exit(1);
    }
    g_has_shell_home = 1;

    /* Welcome banner: display only in interactive mode */
    if (isatty(STDIN_FILENO)) {
        /* Current Time Tracker */
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%a %b %d %H:%M:%S %Z %Y", tm);

        /* Shelly Native Last-Login Tracker */
        char last_login[128] = "First time opening Shelly!";
        char login_file[MAX_PATH];

        const char *home = getenv("HOME");
        if (!home && g_has_shell_home) {
            home = g_shell_home;
        }

        if (home) {
            snprintf(login_file, sizeof(login_file), "%s/.shelly_lastlogin", home);
        } else {
            snprintf(login_file, sizeof(login_file), ".shelly_lastlogin");
        }

        int fd = open(login_file, O_RDONLY);
        if (fd != -1) {
            ssize_t bytes_read = read(fd, last_login, sizeof(last_login) - 1);
            if (bytes_read > 0) {
                last_login[bytes_read] = '\0';
                last_login[strcspn(last_login, "\n")] = '\0';
            }
            close(fd);
        }

        /* Save current time for the NEXT login */
        fd = open(login_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd != -1) {
            write(fd, time_str, strlen(time_str));
            write(fd, "\n", 1);
            close(fd);
        }

        /* Printing the Banner in the Terminal */
        printf("\n");
        printf("* Last login: %s\n", last_login);
        printf("\n");
        printf("%s  ____  _          _ _       %s\n", COL_BCYAN, COL_RESET);
        printf("%s / ___|| |__   ___| | |_   _ %s\n", COL_BCYAN, COL_RESET);
        printf("%s \\___ \\| '_ \\ / _ \\ | | | | |%s\n", COL_BCYAN, COL_RESET);
        printf("%s  ___) | | | |  __/ | | |_| |%s\n", COL_BCYAN, COL_RESET);
        printf("%s |____/|_| |_|\\___|_|_|\\__, |%s\n", COL_BCYAN, COL_RESET);
        printf("%s                        |___/ %s\n", COL_BCYAN, COL_RESET);

        printf("\n");
        printf("%s  Version 2.1.22 (MIT License)%s\n", COL_DIM, COL_RESET);
        printf("%s  Developed by @prtm-bg & @ConsoleCzar-2%s\n", COL_DIM, COL_RESET);
        printf("%s  https://github.com/ConsoleCzar-2/Shelly%s\n", COL_DIM, COL_RESET);

        printf("\n");
        printf("* System information as of %s\n", time_str);
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
    sa_int.sa_flags = 0; /* Do not restart syscalls on SIGINT so read() returns EINTR */
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("sigaction(SIGINT)");
        exit(1);
    }

    /* Initialize command history (load persistent history file) */
    history_init();

    do {
        /* (1.) show the shell prompt */
        char prompt_str[1024]; /* Now safely smaller */
        build_prompt(prompt_str, sizeof(prompt_str));

        /* (2.) read a line */
        if (isatty(STDIN_FILENO)) {
            input = read_line_cursor(prompt_str); // Interactive TTY input with prompt
        }
        else {
            write(STDOUT_FILENO, prompt_str, strlen(prompt_str));
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
        } else {
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
