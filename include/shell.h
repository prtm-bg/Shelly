#ifndef SHELL_H
#define SHELL_H

#define _GNU_SOURCE

#include <limits.h>
#include <signal.h>
#include <stddef.h>

/* Path and Command Size Limits */
#define MAX_PATH PATH_MAX
#define MAX_SUB_CMD_SIZE 128
#define MAX_CMD_SIZE 64

/* ANSI Color & Style Codes (Dynamic based on g_use_color) */
extern int g_use_color;

#define COL_RESET   (g_use_color ? "\033[0m" : "")
#define COL_BOLD    (g_use_color ? "\033[1m" : "")
#define COL_DIM     (g_use_color ? "\033[2m" : "")
#define COL_RED     (g_use_color ? "\033[31m" : "")
#define COL_GREEN   (g_use_color ? "\033[32m" : "")
#define COL_YELLOW  (g_use_color ? "\033[33m" : "")
#define COL_BLUE    (g_use_color ? "\033[34m" : "")
#define COL_MAGENTA (g_use_color ? "\033[35m" : "")
#define COL_CYAN    (g_use_color ? "\033[36m" : "")
#define COL_WHITE   (g_use_color ? "\033[37m" : "")
#define COL_BRED    (g_use_color ? "\033[1;31m" : "")
#define COL_BGREEN  (g_use_color ? "\033[1;32m" : "")
#define COL_BYELLOW (g_use_color ? "\033[1;33m" : "")
#define COL_BBLUE   (g_use_color ? "\033[1;34m" : "")
#define COL_BCYAN   (g_use_color ? "\033[1;36m" : "")

/* Global Variables */
extern int g_has_shell_home;
extern char g_shell_home[MAX_PATH];
extern volatile sig_atomic_t g_sigint_received;

/* Prompt */
void build_prompt(char *buf, size_t size);

#endif /* SHELL_H */
