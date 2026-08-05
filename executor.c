#include "shell.h"  /* Pipe functions */

/* Adding all pipe nodes to an AST node list */
void collect_pipeline_nodes(AST *node, AST **nodes, int *count) {
  if (node == NULL) {
    return;
  }

  if (node->type == NODE_PIPE) {
    collect_pipeline_nodes(node->left, nodes, count);
    collect_pipeline_nodes(node->right, nodes, count);
    return;
  }
  nodes[*count] = node;
  (*count)++;
}
/* Execute a single child command in pipe */
void run_child_command(AST *node) {
  char current_dir[MAX_PATH];
  char **arguments = node->argv;

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
    const char *target = arguments[1];

    if (target == NULL) {
      if (g_has_shell_home) {
        target = g_shell_home;
      } else {
        target = getenv("HOME");
      }
    }
    if (target != NULL && chdir(target) == 0) {
      exit(0);
    }
  } else if (strcmp(arguments[0], "pwd") == 0) {
    if (getcwd(current_dir, sizeof(current_dir)) != NULL) {
      printf("%s\n", current_dir);
      exit(0);
    }
  } else if (strcmp(arguments[0], "clear") == 0) {
    printf("\033[H\033[J");
    fflush(stdout);
    exit(0);
  } else if (strcmp(arguments[0], "exit") == 0) {
    exit(0);
  } else {
    execvp(arguments[0], arguments);
    perror(arguments[0]);
  }
  exit(1);
}
/* Execute pipeline commands */
int run_pipeline(AST *node) {
  AST *nodes[MAX_SUB_CMD_SIZE];
  pid_t pids[MAX_SUB_CMD_SIZE];
  int count = 0;
  int prev_read = -1;
  int i;

  if (node == NULL) {
    return 0;
  }

  collect_pipeline_nodes(node, nodes, &count);
  if (count <= 0) {
    return -1;
  }

  for (i = 0; i < count; i++) {
    int fds[2];
    pid_t pid;

    if (i < count - 1) {
      if (pipe(fds) < 0) {
        perror("pipe() failed");
        return 1;
      }
    }

    pid = fork();
    if (pid < 0) {
      perror("fork() failed");
      return 1;
    }

    if (pid == 0) {
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
      perror("waitpid");
      return 1;
    }

    if (i == count - 1) {
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return 0;
      }
      return 1;
    }
  }
  return 0;
}

/* Helper to execute builtin or external commands */
int execute_single_command(char **arguments) {
  pid_t pid;
  int status = 0;
  char current_dir[MAX_PATH];

  if (arguments == NULL || arguments[0] == NULL) {
    return 0;
  }

  /* Internal commands */
  if (strcmp(arguments[0], "cd") == 0) {
    const char *target = arguments[1];
    if (target == NULL) {
      if (g_has_shell_home)
        target = g_shell_home;
      else {
        target = getenv("HOME");
        if (target == NULL) {
          fprintf(stderr, "cd: HOME not set\n");
          return 1;
        }
      }
    }
    if (chdir(target) != 0) {
      perror("cd");
      return 1;
    }
    return 0;
  } else if (strcmp(arguments[0], "pwd") == 0) {
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
      perror("pwd");
      return 1;
    } else {
      printf("%s\n", current_dir);
    }
    return 0;
  } else if (strcmp(arguments[0], "clear") == 0) {
    printf("\033[H\033[J");
    fflush(stdout);
    return 0;
  } else if (strcmp(arguments[0], "exit") == 0) {
    exit(0);
  }

  /* External commands */
  pid = fork();
  if (pid < 0) {
    perror("fork");
    return 1;
  }
  if (pid == 0) {
    execvp(arguments[0], arguments);
    perror(arguments[0]);
    exit(1);
  }
  if (waitpid(pid, &status, 0) < 0) {
    perror("waitpid");
    return 1;
  }
  if (WIFEXITED(status)) {
    if (WEXITSTATUS(status) == 0) {
      return 0;
    } else {
      return 1;
    }
  }
  return -1;
}

/* Executes a single command with redirections */
int run_command(AST *node) {
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
      node->background = 0;
      exit(execute_ast(node));
    } else {
      printf("[PID] %d\n", bg_pid);
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
    if (left_status == -1) {
      return -1;
    }
    if (left_status == 0) {
      return execute_ast(node->right);
    }
    return left_status;
  }
  // OR (||) node
  else if (node->type == NODE_OR) {
    left_status = execute_ast(node->left);
    if (left_status == -1) {
      return -1;
    }
    if (left_status != 0) {
      return execute_ast(node->right);
    }
    return 0;
  }

  return -1;
}

/* Operator precedence :
    1. | (pipe) [left to right]
    2. && (logical AND), || (logical OR) [left to right]
    4. ; (command separator)   [left to right]
*/

