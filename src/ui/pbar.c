#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "ui/pbar.h"

void term_size(uint64_t* rows, uint64_t* cols) {
    struct winsize ws = { 0 };
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return;
    }

    *rows = ws.ws_row;
    *cols = ws.ws_col;
}

void progress_bar(const char* label, size_t completed, size_t total) {
    uint64_t cols = 0;
    uint64_t rows = 0;
    term_size(&rows, &cols);

    if (total == 0) {
        printf("\r%s: %zu", label, completed);
        fflush(stdout);
        return;
    }

    double ratio = (double) completed / (double) total;
    if (ratio >= 1.0) {
        ratio = 1.0;
    }

    int8_t percent = (int8_t) (ratio * 100.0);
    char suffix[128];
    int32_t suffix_len = snprintf(suffix, sizeof(suffix), " %3d%% %zu/%zu", percent, completed, total);
    int32_t label_len = snprintf(NULL, 0, "%s: ", label);

    int64_t bar_width = cols - label_len - suffix_len - 4;
    if (bar_width < 10) {
        bar_width = 10;
    }

    int64_t filled = (int64_t) (ratio * bar_width);
    printf("\r%s: [", label);
    for (int64_t i = 0; i < bar_width; ++i) {
        if (i < filled) {
            putchar('=');
        } else if (i == filled) {
            putchar('>');
        } else {
            putchar(' ');
        }
    }

    printf("]%s", suffix);
    fflush(stdout);

    if (completed >= total) {
        putchar('\n');
    }
}
