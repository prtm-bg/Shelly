#ifndef TERMINAL_H
#define TERMINAL_H

#include <stddef.h>

void enable_raw_mode(void);
void disable_raw_mode(void);
size_t get_visible_len(const char *str);

#endif /* TERMINAL_H */
