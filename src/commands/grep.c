#include <stdio.h>
#include <stdlib.h>

#include "commands/grep.h"

#include "rg_regex.h"
#include "rg_walker.h"

#include "search/collector.h"
#include "search/loader.h"

static bool print_hit(
    const char* path,
    const file_buffer_t* file,
    const search_match_t* match,
    void* payload
) {
    (void) payload;

    size_t start = match->line_span.start;
    size_t end = match->line_span.end;
    while (end > start &&
           (file->data[end - 1] == '\n' || file->data[end - 1] == '\r')) {
        end--;
    }

    flockfile(stdout);
    printf("%s:%zu:%zu: ", path, match->location.line, match->location.column);
    fwrite(file->data + start, 1, end - start, stdout);
    putchar('\n');
    funlockfile(stdout);

    return true;
}

static void grep_file(const char* path, rg_matcher_t* matcher) {
    if (!search_scan_file(path, matcher, print_hit, NULL)) {
        fprintf(stderr, "Error: failed to search %s\n", path);
    }
}

static rg_walker_walkstate_t walker_fn(
    const rg_walker_entry_t* dirent,
    void* payload
) {
    const rg_matcher_t* matcher = (rg_matcher_t*) payload;
    if (dirent->kind == RG_WALKER_FILE) {
        grep_file(dirent->path, (rg_matcher_t*) matcher);
    }

    return RG_WALKER_CONTINUE;
}

void cmd_grep(int32_t argc, char** argv) {
    rg_walker_t* walker = NULL;
    rg_walker_opts_t wopts = { 0 };

    rg_walker_default_opts(&wopts);

    const char* paths[] = { "." };
    int32_t error = rg_walker_build(
        &walker,
        &wopts,
        NULL,
        paths,
        sizeof(paths) / sizeof(char*)
    );
    if (error != 0) {
        fprintf(stderr, "Error: rg_walker_build() failed! (%d)\n", error);
        return;
    }

    rg_matcher_t* payload = { 0 };
    rg_regex_matcher_opts_t ropts = { 0 };
    rg_regex_default_opts(&ropts);

    size_t n_patterns = argc - 2;
    const char* patterns[n_patterns];
    for (int32_t i = 0; i < n_patterns; i++) {
        patterns[i] = argv[i + 2];
    }
    error = rg_regex_matcher_build(
        &payload,
        &ropts,
        patterns,
        n_patterns,
        false
    );
    if (error != 0) {
        rg_walker_free(walker);
        if (error == RG_REGEX_NOT_ALLOWED_ERR) {
            fprintf(stderr, "Error: no regex provided!\n");
            return;
        }
        fprintf(
            stderr,
            "Error: rg_regex_matcher_build() failed! (%d)\n",
            error
        );
        return;
    }

    error = rg_walker_run(walker, walker_fn, payload);
    if (error != 0) {
        fprintf(stderr, "Error: rg_walker_run() failed! (%d)\n", error);
    }

    rg_walker_free(walker);
    rg_regex_matcher_free(payload);
}
