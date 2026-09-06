#include "executor.h"
#include "history_store.h"

#include <string.h>

int builtin_history(char **argv, int argc) {
    (void)argc;
    if (argv[1] && strcmp(argv[1], "-c") == 0) {
        history_clear();
    } else {
        history_print();
    }
    return 0;
}
