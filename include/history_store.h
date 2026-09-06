#ifndef HISTORY_STORE_H
#define HISTORY_STORE_H

void history_init(void);
void history_add(const char *cmd);
int history_count(void);
const char *history_get(int index);
void history_load(const char *path);
void history_save(const char *path);
void history_clear(void);
void history_print(void);
void history_free(void);
const char *get_history_file_path(void);

#endif /* HISTORY_STORE_H */
