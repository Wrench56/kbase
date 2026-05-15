#include "commands/todo.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rg_regex.h"
#include "rg_walker.h"

#include "search/collector.h"

#define TODO_RED "\033[31m"
#define TODO_RESET "\033[0m"

typedef struct todo_item {
    char* path;
    char* text;
    char date[11];

    search_location_t location;
    search_range_t line_span;
    search_range_t match_span;
} todo_item_t;

typedef struct todo_results {
    todo_item_t* items;
    size_t len;
    size_t cap;
} todo_results_t;

static bool todo_today(char out[11]) {
    time_t now = time(NULL);
    struct tm tm = { 0 };

    if (now == (time_t) -1) {
        return false;
    }

    if (!localtime_r(&now, &tm)) {
        return false;
    }

    return strftime(out, 11, "%F", &tm) == 10;
}

static bool todo_results_push(
    todo_results_t* results,
    const char* path,
    const char* text,
    size_t text_len,
    const char* date,
    const search_match_t* match
) {
    if (!results || !path || !text || !date || !match) {
        return false;
    }

    if (results->len == results->cap) {
        size_t new_cap = results->cap ? results->cap * 2 : 64;
        todo_item_t* items = realloc(results->items, new_cap * sizeof(*items));

        if (!items) {
            return false;
        }

        results->items = items;
        results->cap = new_cap;
    }

    char* owned_path = strdup(path);
    char* owned_text = strndup(text, text_len);
    if (!owned_path || !owned_text) {
        free(owned_path);
        free(owned_text);
        return false;
    }

    todo_item_t* item = &results->items[results->len++];
    *item = (todo_item_t) {
        .path = owned_path,
        .text = owned_text,
        .location = match->location,
        .line_span = match->line_span,
        .match_span = match->match_span,
    };

    memcpy(item->date, date, 10);
    item->date[10] = '\0';

    return true;
}

static void todo_results_free(todo_results_t* results) {
    if (!results) {
        return;
    }

    for (size_t i = 0; i < results->len; i++) {
        free(results->items[i].path);
        free(results->items[i].text);
    }

    free(results->items);
    *results = (todo_results_t) { 0 };
}

static bool parse_todo_hit(
    const char* path,
    const file_buffer_t* file,
    const search_match_t* match,
    todo_results_t* results
) {
    static const char prefix[] = "[ ] TODO: ";
    const size_t prefix_len = sizeof(prefix) - 1;

    size_t start = match->line_span.start;
    size_t end = match->line_span.end;

    while (end > start &&
           (file->data[end - 1] == '\n' || file->data[end - 1] == '\r')) {
        end--;
    }

    if (end - start < prefix_len + 12) {
        return true;
    }

    const char* line = (const char*) file->data + start;
    size_t line_len = end - start;

    if (memcmp(line, prefix, prefix_len) != 0) {
        return true;
    }

    const char* at = NULL;
    for (size_t i = prefix_len; i + 12 <= line_len; i++) {
        if (line[i] == ' ' && line[i + 1] == '@') {
            at = line + i;
        }
    }

    if (!at) {
        return true;
    }

    const char* date = at + 2;
    if ((size_t) (date - line) + 10 > line_len) {
        return true;
    }

    const char* text = line + prefix_len;
    size_t text_len = (size_t) (at - text);
    while (text_len > 0 && text[text_len - 1] == ' ') {
        text_len--;
    }

    return todo_results_push(results, path, text, text_len, date, match);
}

static bool todo_hit_cb(
    const char* path,
    const file_buffer_t* file,
    const search_match_t* match,
    void* payload
) {
    return parse_todo_hit(path, file, match, payload);
}

static void print_todos(const todo_results_t* results, bool today_only) {
    char today[11] = { 0 };
    if (!todo_today(today)) {
        fprintf(stderr, "Error: failed to get current date\n");
        return;
    }

    char* prev_path = "\0";
    for (size_t i = 0; i < results->len; i++) {
        const todo_item_t* item = &results->items[i];

        int32_t date_cmp = strcmp(item->date, today);

        if (today_only && date_cmp != 0) {
            continue;
        }

        bool overdue = date_cmp < 0;
        if (strcmp(prev_path, item->path) != 0) {
            prev_path = item->path;
            printf("[=] %s:\n", prev_path);
        }

        if (overdue) {
            printf(
                TODO_RED "[ ]   %zu: [%s] %s (overdue)" TODO_RESET "\n",
                item->location.line,
                item->date,
                item->text
            );
        } else {
            printf(
                "[ ]   %zu: [%s] %s\n",
                item->location.line,
                item->date,
                item->text
            );
        }
    }
}

typedef struct todo_walk_ctx {
    rg_matcher_t* matcher;
    todo_results_t* results;
} todo_walk_ctx_t;

static rg_walker_walkstate_t todo_walker_fn(
    const rg_walker_entry_t* dirent,
    void* payload
) {
    todo_walk_ctx_t* ctx = payload;

    if (dirent->kind != RG_WALKER_FILE) {
        return RG_WALKER_CONTINUE;
    }

    if (!search_scan_file(
            dirent->path,
            ctx->matcher,
            todo_hit_cb,
            ctx->results
        )) {
        fprintf(stderr, "Error: failed to scan %s\n", dirent->path);
    }

    return RG_WALKER_CONTINUE;
}

void cmd_todo(int32_t argc, char** argv) {
    (void) argc;
    (void) argv;

    bool just_today = false;

    int32_t opt = getopt(argc, argv, "tn:");
    switch (opt) {
        case 'n':
            printf("[ ] TODO: %s\n", optarg);
            return;
        case 't':
            just_today = true;
            break;
        default:
            break;
    }

    rg_regex_matcher_opts_t ropts = { 0 };
    rg_regex_default_opts(&ropts);
    ropts.case_insensitive = true;
    ropts.multi_line = true;

    const char* patterns[] = {
        "^\\[ \\] TODO: .* @[0-9]{4}-[0-9]{2}-[0-9]{2}",
    };

    rg_matcher_t* matcher = NULL;
    int32_t error = rg_regex_matcher_build(
        &matcher,
        &ropts,
        patterns,
        sizeof(patterns) / sizeof(patterns[0]),
        false
    );

    if (error != 0) {
        fprintf(stderr, "Error: failed to build TODO matcher (%d)\n", error);
        return;
    }

    rg_walker_t* walker = NULL;
    rg_walker_opts_t wopts = { 0 };
    rg_walker_default_opts(&wopts);

    const char* paths[] = { "." };
    error = rg_walker_build(
        &walker,
        &wopts,
        NULL,
        paths,
        sizeof(paths) / sizeof(paths[0])
    );

    if (error != 0) {
        fprintf(stderr, "Error: rg_walker_build() failed! (%d)\n", error);
        rg_regex_matcher_free(matcher);
        return;
    }

    todo_results_t results = { 0 };
    todo_walk_ctx_t ctx = {
        .matcher = matcher,
        .results = &results,
    };

    error = rg_walker_run(walker, todo_walker_fn, &ctx);

    if (error != 0) {
        fprintf(stderr, "Error: rg_walker_run() failed! (%d)\n", error);
    }

    print_todos(&results, just_today);

    todo_results_free(&results);
    rg_walker_free(walker);
    rg_regex_matcher_free(matcher);
}
