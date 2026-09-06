#include "executor.h"
#include "shell.h"
#include "history_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

/* Adding all pipe nodes to an AST node list */
static void collect_pipeline_nodes(AST *node, AST **nodes, int *count) {
    if (node == NULL) {
        return;
    }

    if (node->type == NODE_PIPE) {
        collect_pipeline_nodes(node->left, nodes, count);
        collect_pipeline_nodes(node->right, nodes, count);
        return;
    }
    if (*count < MAX_SUB_CMD_SIZE) {
        nodes[*count] = node;
        (*count)++;
    }
}

/* Execute a single child command in pipe */
static void run_child_command(AST *node) {
    char **arguments = node->argv;

    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);

    if (node->file_in) {
        int fd = open(node->file_in, O_RDONLY);
        if (fd < 0) {
            perror(node->file_in);
            exit(1);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (node->file_out) {
        int flags = O_WRONLY | O_CREAT | (node->append_out ? O_APPEND : O_TRUNC);
        int fd = open(node->file_out, flags, 0644);
        if (fd < 0) {
            perror(node->file_out);
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    if (arguments == NULL || arguments[0] == NULL) {
        exit(0);
    }

    if (strcmp(arguments[0], "cd") == 0) {
        exit(builtin_cd(arguments));
    }
    else if (strcmp(arguments[0], "pwd") == 0) {
        exit(builtin_pwd());
    }
    else if (strcmp(arguments[0], "clear") == 0) {
        if (write(STDOUT_FILENO, "\033[H\033[J", 7) < 0) {
            perror("clear");
        }
        exit(0);
    }
    else if (strcmp(arguments[0], "exit") == 0) {
        exit(arguments[1] ? atoi(arguments[1]) : 0);
    }
    else if (strcmp(arguments[0], "history") == 0) {
        if (arguments[1] && strcmp(arguments[1], "-c") == 0) {
            write(STDERR_FILENO, "history: -c not supported in pipelines\n", 39);
            exit(1);
        }
        exit(builtin_history(arguments, 0));
    }
    else {
        execvp(arguments[0], arguments);
        perror(arguments[0]);
        exit(errno == ENOENT ? 127 : 126);
    }
}

/* Execute pipeline commands */
static int run_pipeline(AST *node) {
    AST *nodes[MAX_SUB_CMD_SIZE];
    pid_t pids[MAX_SUB_CMD_SIZE];
    int count = 0;
    int prev_read = -1;
    int i;
    int last_status = 0;

    if (node == NULL) {
        return 0;
    }

    collect_pipeline_nodes(node, nodes, &count);
    if (count <= 0) {
        return -1;
    }

    /* Block SIGCHLD while spawning and waiting for foreground pipeline */
    sigset_t mask, prev_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    for (i = 0; i < count; i++) {
        int fds[2];
        pid_t pid;

        if (i < count - 1) {
            if (pipe(fds) < 0) {
                perror("pipe() failed");
                sigprocmask(SIG_SETMASK, &prev_mask, NULL);
                return 1;
            }
        }

        pid = fork();
        if (pid < 0) {
            perror("fork() failed");
            sigprocmask(SIG_SETMASK, &prev_mask, NULL);
            return 1;
        }

        if (pid == 0) {
            sigprocmask(SIG_SETMASK, &prev_mask, NULL);

            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
            }
            if (i < count - 1) {
                dup2(fds[1], STDOUT_FILENO);
            }

            if (prev_read != -1) {
                close(prev_read);
            }
            if (i < count - 1) {
                close(fds[0]);
                close(fds[1]);
            }

            run_child_command(nodes[i]);
            exit(1);
        }

        pids[i] = pid;

        if (prev_read != -1) {
            close(prev_read);
        }
        if (i < count - 1) {
            close(fds[1]);
            prev_read = fds[0];
        }
    }

    if (prev_read != -1) {
        close(prev_read);
    }

    for (i = 0; i < count; i++) {
        int status;

        if (waitpid(pids[i], &status, 0) < 0) {
            if (errno != ECHILD) {
                perror("waitpid");
            }
            if (i == count - 1) {
                last_status = 1;
            }
        }
        else if (i == count - 1) {
            if (WIFEXITED(status)) {
                last_status = WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status)) {
                last_status = 128 + WTERMSIG(status);
            }
            else {
                last_status = 1;
            }
        }
    }

    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    return last_status;
}

/* Helper to execute builtin or external commands */
static int execute_single_command(char **arguments) {
    pid_t pid;
    int status = 0;
    int ret_val = 0;

    if (arguments == NULL || arguments[0] == NULL) {
        return 0;
    }

    /* Internal commands */
    if (strcmp(arguments[0], "cd") == 0) {
        return builtin_cd(arguments);
    }
    else if (strcmp(arguments[0], "pwd") == 0) {
        return builtin_pwd();
    }
    else if (strcmp(arguments[0], "clear") == 0) {
        if (write(STDOUT_FILENO, "\033[H\033[J", 7) < 0) {
            perror("clear");
        }
        return 0;
    }
    else if (strcmp(arguments[0], "exit") == 0) {
        exit(arguments[1] ? atoi(arguments[1]) : 0);
    }
    else if (strcmp(arguments[0], "history") == 0) {
        return builtin_history(arguments, 0);
    }

    /* External commands */
    sigset_t mask, prev_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    pid = fork();
    if (pid < 0) {
        perror("fork");
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        return 1;
    }
    if (pid == 0) {
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        execvp(arguments[0], arguments);
        perror(arguments[0]);
        _exit(errno == ENOENT ? 127 : 126);
    }

    if (waitpid(pid, &status, 0) < 0) {
        if (errno != ECHILD) {
            perror("waitpid");
        }
        ret_val = 1;
    }
    else if (WIFEXITED(status)) {
        ret_val = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status)) {
        ret_val = 128 + WTERMSIG(status);
    }
    else {
        ret_val = 1;
    }

    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    return ret_val;
}

/* Executes a single command with redirections */
static int run_command(AST *node) {
    int ret_val = 0;
    int orig_stdin = -1;
    int orig_stdout = -1;

    if (node->file_in || node->file_out) {
        orig_stdin = dup(STDIN_FILENO);
        orig_stdout = dup(STDOUT_FILENO);
        if (node->file_in) {
            int fd = open(node->file_in, O_RDONLY);
            if (fd < 0) {
                perror(node->file_in);
                if (orig_stdin != -1) {
                    dup2(orig_stdin, STDIN_FILENO);
                    close(orig_stdin);
                }
                if (orig_stdout != -1) {
                    dup2(orig_stdout, STDOUT_FILENO);
                    close(orig_stdout);
                }
                return 1;
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        if (node->file_out) {
            int flags = O_WRONLY | O_CREAT | (node->append_out ? O_APPEND : O_TRUNC);
            int fd = open(node->file_out, flags, 0644);
            if (fd < 0) {
                perror(node->file_out);
                if (orig_stdin != -1) {
                    dup2(orig_stdin, STDIN_FILENO);
                    close(orig_stdin);
                }
                if (orig_stdout != -1) {
                    dup2(orig_stdout, STDOUT_FILENO);
                    close(orig_stdout);
                }
                return 1;
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
    }

    ret_val = execute_single_command(node->argv);

    if (orig_stdin != -1) {
        dup2(orig_stdin, STDIN_FILENO);
        close(orig_stdin);
    }
    if (orig_stdout != -1) {
        dup2(orig_stdout, STDOUT_FILENO);
        close(orig_stdout);
    }
    return ret_val;
}

/* execute AST nodes */
int execute_ast(AST *node) {
    int left_status = 0;

    // Empty node
    if (node == NULL) {
        return 0;
    }

    if (node->background) {
        pid_t bg_pid = fork();
        if (bg_pid < 0) {
            perror("fork for background job");
            return 1;
        }
        if (bg_pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            node->background = 0;
            exit(execute_ast(node));
        }
        else {
            char bg_msg[128];
            int n = snprintf(bg_msg, sizeof(bg_msg), "%s[%sPID%s] %s%d\n", COL_DIM, COL_BYELLOW, COL_DIM, COL_RESET, bg_pid);
            if (n > 0) {
                write(STDOUT_FILENO, bg_msg, sizeof(bg_msg) - 1);
            }
            return 0;
        }
    }

    // Command node
    else if (node->type == NODE_COMMAND) {
        return run_command(node);
    }
    // Pipe node
    else if (node->type == NODE_PIPE) {
        return run_pipeline(node);
    }
    // Command sep (;) node
    else if (node->type == NODE_SEMI) {
        left_status = execute_ast(node->left);
        if (left_status == -1) {
            return -1;
        }
        return execute_ast(node->right);
    }
    // AND (&&) node
    else if (node->type == NODE_AND) {
        left_status = execute_ast(node->left);
        if (left_status == 0) {
            return execute_ast(node->right);
        }
        return left_status;
    }
    // OR (||) node
    else if (node->type == NODE_OR) {
        left_status = execute_ast(node->left);
        if (left_status != 0) {
            return execute_ast(node->right);
        }
        return 0;
    }

    return -1;
}
