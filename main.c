#include "shell.h"\n\n/* Global Variables for pwd to be made shell home dir */
int g_has_shell_home = 0;
char g_shell_home[MAX_PATH];

void sigchld_handler(int sig) {
  int saved_errno = errno;
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;
  errno = saved_errno;
}
int main(int argc, char *argv[]) {
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

  struct sigaction sa;
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  do {
    /* (1.) show the shell prompt
       (your login name should be your mysh prompt, get your login name
       programmatically) */
    euid = geteuid();
    egid = getegid();

    user_data = getpwuid(euid);
    if (user_data == NULL) {
      perror("getepwuid() failed");
      exit(1);
    }

    group_data = getgrgid(egid);
    if (group_data == NULL) {
      perror("getgrgid() failed");
      exit(1);
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
    }
    /* Using pwd, as shell home not set */
    else {
      snprintf(display_path, sizeof(display_path), "%s", cwd);
    }

    char prompt = ((int)euid == 0) ? '#' : '$';
    printf("%s@%s:%s%c ", user_data->pw_name, group_data->gr_name, display_path,
           prompt);

    /* (2.) read a line in a string variable, say, cmd */
    if (scanf(" %m[^\n]", &input) != 1) {
      break;
    }

    /* (3.) parse cmd into subcommands, in "command1 ; command2" command1 and
     * command2 are subcommands */
    if (tokenize_input(input, &tokens, &token_count) < 0) {
      fprintf(stderr, "Tokenization failed\n");
      free(input);
      input = NULL;
      continue;
    }

    /* (4.) parse the subcommands for command line arguments */
    if (parse_tokens_to_ast(tokens, token_count, &root) == 0) {
      /* (5.) if a subcommand is internal then do what is necessary for it.
       * (6.) Else if there is an executable for the command then fork() and let
       * the child process execute the executable */
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