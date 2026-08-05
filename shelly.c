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

volatile sig_atomic_t g_sigint_received = 0;

/* SIGINT handler to avoid killing the shell prompt */
void sigint_handler(int sig) {
  (void)sig;
  g_sigint_received = 1;
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

  g_sigint_received = 0;

  while (1) {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n < 0) {
      if (errno == EINTR) {
        if (g_sigint_received) {
          /* Discard current input line on Ctrl+C and return empty string */
          g_sigint_received = 0;
          buf[0] = '\0';
          return buf;
        }
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

int g_use_color = 1;

void init_color_support(int argc, char *argv[]) {
  /* Check command line arguments for --no-color, -n, --color=never */
  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--no-color") == 0 ||
        strcmp(argv[i], "-n") == 0 ||
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

int main(int argc, char *argv[]) {
  uid_t euid;
  struct passwd *user_data;
  char cwd[MAX_PATH];
  char display_path[MAX_PATH];
  char *input = NULL;
  Token *tokens = NULL;
  int token_count = 0;
  AST *root = NULL;

  /* Initialize color support */
  init_color_support(argc, argv);

  /* Setting shell home dir */
  if (getcwd(g_shell_home, sizeof(g_shell_home)) == NULL) {
    perror("getcwd() failed");
    exit(1);
  }
  g_has_shell_home = 1;

  /* Welcome banner: display only in interactive mode */
  if (isatty(STDIN_FILENO)) {
    printf("\n");
    printf("%s  ____  _          _ _       %s\n", COL_BCYAN, COL_RESET);
    printf("%s / ___|| |__   ___| | |_   _ %s\n", COL_BCYAN, COL_RESET);
    printf("%s \\___ \\| '_ \\ / _ \\ | | | | |%s\n", COL_BCYAN, COL_RESET);
    printf("%s  ___) | | | |  __/ | | |_| |%s\n", COL_BCYAN, COL_RESET);
    printf("%s |____/|_| |_|\\___|_|_|\\__, |%s\n", COL_BCYAN, COL_RESET);
    printf("%s                        |___/ %s\n", COL_BCYAN, COL_RESET);
    printf("%s  Version 2.1.22%s\n", COL_DIM, COL_RESET);
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

  do {
    /* (1.) show the shell prompt */
    euid = geteuid();
    user_data = getpwuid(euid);
    const char *user_name = user_data ? user_data->pw_name : "user";

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
      strncpy(hostname, "localhost", sizeof(hostname) - 1);
      hostname[sizeof(hostname) - 1] = '\0';
    }

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

    if ((int)euid == 0) {
      printf("%s%s%s@%s%s%s%s:%s[%s]%s%s#%s ",
             COL_BGREEN, user_name, COL_DIM, COL_RESET,
             COL_GREEN, hostname, COL_RESET,
             COL_BCYAN, display_path, COL_RESET,
             COL_BYELLOW, COL_RESET);
    } else {
      printf("%s%s%s@%s%s%s%s:%s[%s]%s ",
             COL_BGREEN, user_name, COL_DIM, COL_RESET,
             COL_GREEN, hostname, COL_RESET,
             COL_BCYAN, display_path, COL_RESET);
    }
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

  return 0;
}