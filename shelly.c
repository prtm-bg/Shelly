#include "shell.h"

int g_has_shell_home = 0;
char g_shell_home[MAX_PATH];

/* SIGCHLD handler for background jobs */
void sigchld_handler(int sig) {
  (void)sig;
  int saved_errno = errno;
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;
  errno = saved_errno;
}

/* SIGINT handler to avoid killing the shell prompt */
void sigint_handler(int sig) {
  (void)sig;
  write(STDOUT_FILENO, "\n", 1);
}

/* Read a line from STDIN using read() system call */
char *read_line(void) {
  size_t cap = 128;
  size_t len = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) {
    perror("malloc() failed");
    return NULL;
  }

  while (1) {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n < 0) {
      if (errno == EINTR) {
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

/* Check if string is empty or contains only whitespace */
static int is_empty_line(const char *str) {
  while (*str) {
    if (!isspace((unsigned char)*str))
      return 0;
    str++;
  }
  return 1;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  uid_t euid;
  gid_t egid;
  struct passwd *user_data;
  struct group *group_data;
  char cwd[MAX_PATH];
  char display_path[MAX_PATH];
  char *input = NULL;
  Token *tokens = NULL;
  int token_count = 0;
  AST *root = NULL;

  /* Setting shell home dir */
  if (getcwd(g_shell_home, sizeof(g_shell_home)) == NULL) {
    perror("getcwd() failed");
    exit(1);
  }
  g_has_shell_home = 1;

  /* Welcome banner */
  printf("\n");
  printf(COL_BCYAN "  ____  _          _ _       " COL_RESET "\n");
  printf(COL_BCYAN " / ___|| |__   ___| | |_   _ " COL_RESET "\n");
  printf(COL_BCYAN " \\___ \\| '_ \\ / _ \\ | | | | |" COL_RESET "\n");
  printf(COL_BCYAN "  ___) | | | |  __/ | | |_| |" COL_RESET "\n");
  printf(COL_BCYAN " |____/|_| |_|\\___|_|_|\\__, |" COL_RESET "\n");
  printf(COL_BCYAN "                        |___/ " COL_RESET "\n");
  printf(COL_DIM "  Version 2.1.22" COL_RESET "\n");
  printf("\n");
  fflush(stdout);

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
  sa_int.sa_flags = SA_RESTART;
  if (sigaction(SIGINT, &sa_int, NULL) == -1) {
    perror("sigaction(SIGINT)");
    exit(1);
  }

  do {
    /* (1.) show the shell prompt */
    euid = geteuid();
    egid = getegid();

    user_data = getpwuid(euid);
    group_data = getgrgid(egid);

    const char *user_name = user_data ? user_data->pw_name : "user";
    const char *group_name = group_data ? group_data->gr_name : "group";

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
      else if (strncmp(cwd, g_shell_home, home_len) == 0 &&
               cwd[home_len] == '/') {
        snprintf(display_path, sizeof(display_path), "~%s", cwd + home_len);
      }
      /* Other directories */
      else {
        snprintf(display_path, sizeof(display_path), "%s", cwd);
      }
    } else {
      snprintf(display_path, sizeof(display_path), "%s", cwd);
    }

    char prompt = ((int)euid == 0) ? '#' : '$';
    printf(COL_BGREEN "%s" COL_DIM "@" COL_GREEN "%s" COL_RESET ":" COL_BCYAN
                      "%s" COL_BYELLOW "%c " COL_RESET,
           user_name, group_name, display_path, prompt);
    fflush(stdout);

    /* (2.) read a line */
    input = read_line();
    if (input == NULL) {
      break;
    }

    /* Skip blank lines */
    if (is_empty_line(input)) {
      free(input);
      input = NULL;
      continue;
    }

    /* (3.) parse cmd into tokens */
    if (tokenize_input(input, &tokens, &token_count) < 0) {
      fprintf(stderr, COL_BRED "error: " COL_RESET "tokenization failed\n");
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

  return 0;
}