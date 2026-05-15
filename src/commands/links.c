#include "commands/links.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rg_regex.h"
#include "rg_walker.h"

#include "search/collector.h"

typedef struct link_item {
    char* path;
    char* text;
    char* target;

    search_location_t location;
} link_item_t;

typedef struct link_results {
    link_item_t* items;
    size_t len;
    size_t cap;
} link_results_t;

typedef struct links_parse_ctx {
    link_results_t* results;
} links_parse_ctx_t;

typedef struct backlinks_parse_ctx {
    link_results_t* results;
    const char* wanted;
} backlinks_parse_ctx_t;

typedef struct backlinks_walk_ctx {
    rg_matcher_t* matcher;
    const char* wanted;
    link_results_t* results;
} backlinks_walk_ctx_t;

static bool link_results_push(
    link_results_t* results,
    const char* path,
    const char* text,
    size_t text_len,
    const char* target,
    size_t target_len,
    search_location_t location
) {
    if (!results || !path || !text || !target) {
        return false;
    }

    if (results->len == results->cap) {
        size_t new_cap = results->cap ? results->cap * 2 : 64;
        link_item_t* items = realloc(results->items, new_cap * sizeof(*items));

        if (!items) {
            return false;
        }

        results->items = items;
        results->cap = new_cap;
    }

    char* owned_path = strdup(path);
    char* owned_text = strndup(text, text_len);
    char* owned_target = strndup(target, target_len);
    if (!owned_path || !owned_text || !owned_target) {
        free(owned_path);
        free(owned_text);
        free(owned_target);
        return false;
    }

    results->items[results->len++] = (link_item_t) {
        .path = owned_path,
        .text = owned_text,
        .target = owned_target,
        .location = location,
    };

    return true;
}

static void link_results_free(link_results_t* results) {
    if (!results) {
        return;
    }

    for (size_t i = 0; i < results->len; i++) {
        free(results->items[i].path);
        free(results->items[i].text);
        free(results->items[i].target);
    }

    free(results->items);
    *results = (link_results_t) { 0 };
}

static rg_matcher_t* build_link_matcher(void) {
    rg_regex_matcher_opts_t ropts = { 0 };
    rg_regex_default_opts(&ropts);
    ropts.multi_line = true;

    const char* patterns[] = {
        "!?\\[[^\\]]+\\]\\([^\\)]+\\)",
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
        fprintf(stderr, "Error: failed to build link matcher (%d)\n", error);
        return NULL;
    }

    return matcher;
}

static bool parse_markdown_link(
    const char* path,
    const file_buffer_t* file,
    const search_match_t* match,
    link_results_t* results
) {
    const char* s = (const char*) file->data + match->match_span.start;
    size_t len = match->match_span.end - match->match_span.start;

    if (len == 0) {
        return true;
    }

    if (s[0] == '!') {
        return true;
    }

    const char* end = s + len;
    const char* close_text = memchr(s, ']', len);

    if (!close_text || close_text + 1 >= end || close_text[1] != '(') {
        return true;
    }

    const char* text = s + 1;
    const char* target = close_text + 2;
    const char* close_target = memchr(target, ')', (size_t) (end - target));

    if (!close_target) {
        return true;
    }

    return link_results_push(
        results,
        path,
        text,
        (size_t) (close_text - text),
        target,
        (size_t) (close_target - target),
        match->location
    );
}

static bool links_hit_cb(
    const char* path,
    const file_buffer_t* file,
    const search_match_t* match,
    void* payload
) {
    links_parse_ctx_t* ctx = payload;
    return parse_markdown_link(path, file, match, ctx->results);
}

static const char* path_basename(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool link_target_matches(const char* target, const char* wanted) {
    const char* wanted_base = path_basename(wanted);

    return strcmp(target, wanted) == 0 || strcmp(target, wanted_base) == 0;
}

static bool parse_backlink(
    const char* path,
    const file_buffer_t* file,
    const search_match_t* match,
    backlinks_parse_ctx_t* ctx
) {
    const char* s = (const char*) file->data + match->match_span.start;
    size_t len = match->match_span.end - match->match_span.start;
    if (len == 0) {
        return true;
    }

    if (s[0] == '!') {
        return true;
    }

    const char* end = s + len;
    const char* close_text = memchr(s, ']', len);
    if (!close_text || close_text + 1 >= end || close_text[1] != '(') {
        return true;
    }

    const char* text = s + 1;
    const char* target = close_text + 2;
    const char* close_target = memchr(target, ')', (size_t) (end - target));
    if (!close_target) {
        return true;
    }

    size_t target_len = (size_t) (close_target - target);
    char* owned_target = strndup(target, target_len);
    if (!owned_target) {
        return false;
    }

    bool matches = link_target_matches(owned_target, ctx->wanted);
    free(owned_target);
    if (!matches) {
        return true;
    }

    return link_results_push(
        ctx->results,
        path,
        text,
        (size_t) (close_text - text),
        target,
        target_len,
        match->location
    );
}

static bool backlinks_hit_cb(
    const char* path,
    const file_buffer_t* file,
    const search_match_t* match,
    void* payload
) {
    backlinks_parse_ctx_t* ctx = payload;
    return parse_backlink(path, file, match, ctx);
}

static void print_links(const link_results_t* results) {
    char* prev_path = "";
    for (size_t i = 0; i < results->len; i++) {
        const link_item_t* item = &results->items[i];

        if (strcmp(prev_path, item->path) != 0) {
            prev_path = item->path;
            printf("[=] %s:\n", prev_path);
        }

        printf(
            "[ ]   %zu:%zu: %s -> %s\n",
            item->location.line,
            item->location.column,
            item->text,
            item->target
        );
    }
}

static void links_collect_outgoing(const char* file_path) {
    rg_matcher_t* matcher = build_link_matcher();
    if (!matcher) {
        return;
    }

    link_results_t results = { 0 };
    links_parse_ctx_t ctx = {
        .results = &results,
    };

    if (!search_scan_file(file_path, matcher, links_hit_cb, &ctx)) {
        fprintf(stderr, "Error: failed to scan %s\n", file_path);
    }

    print_links(&results);

    link_results_free(&results);
    rg_regex_matcher_free(matcher);
}

static rg_walker_walkstate_t backlinks_walker_fn(
    const rg_walker_entry_t* dirent,
    void* payload
) {
    backlinks_walk_ctx_t* ctx = payload;

    if (dirent->kind != RG_WALKER_FILE) {
        return RG_WALKER_CONTINUE;
    }

    backlinks_parse_ctx_t parse_ctx = {
        .results = ctx->results,
        .wanted = ctx->wanted,
    };

    if (!search_scan_file(
            dirent->path,
            ctx->matcher,
            backlinks_hit_cb,
            &parse_ctx
        )) {
        fprintf(stderr, "Error: failed to scan %s\n", dirent->path);
    }

    return RG_WALKER_CONTINUE;
}

static void links_collect_backlinks(const char* wanted) {
    rg_matcher_t* matcher = build_link_matcher();
    if (!matcher) {
        return;
    }

    rg_walker_t* walker = NULL;
    rg_walker_opts_t wopts = { 0 };
    rg_walker_default_opts(&wopts);

    const char* paths[] = { "." };
    int32_t error = rg_walker_build(
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

    link_results_t results = { 0 };
    backlinks_walk_ctx_t ctx = {
        .matcher = matcher,
        .wanted = wanted,
        .results = &results,
    };

    error = rg_walker_run(walker, backlinks_walker_fn, &ctx);

    if (error != 0) {
        fprintf(stderr, "Error: rg_walker_run() failed! (%d)\n", error);
    }

    print_links(&results);

    link_results_free(&results);
    rg_walker_free(walker);
    rg_regex_matcher_free(matcher);
}

void cmd_links(int32_t argc, char** argv) {
    bool backlinks = false;

    size_t opti = 2;
    int32_t opt = getopt(argc, argv, "b");
    switch (opt) {
        case 'b':
            ++opti;
            backlinks = true;
            break;
        default:
            break;
    }

    for (size_t i = opti; i < argc; i++) {
        const char* file_path = argv[i];
        if (backlinks) {
            links_collect_backlinks(file_path);
        } else {
            links_collect_outgoing(file_path);
        }
    }
}
