#include "search/collector.h"

#include <stdlib.h>
#include <string.h>

#include "search/loader.h"

#define MMAP_THRESHOLD (16 * 1024 * 1024)

bool search_results_push(
    search_results_t* results,
    const char* path,
    const search_match_t* match
) {
    if (!results || !path || !match) {
        return false;
    }

    if (results->len == results->cap) {
        size_t new_cap = results->cap ? results->cap * 2 : 256;
        search_hit_t* hits = realloc(results->hits, new_cap * sizeof(*hits));

        if (!hits) {
            return false;
        }

        results->hits = hits;
        results->cap = new_cap;
    }

    char* owned_path = strdup(path);
    if (!owned_path) {
        return false;
    }

    results->hits[results->len++] = (search_hit_t) {
        .path = owned_path,
        .match = *match,
    };

    return true;
}

void search_results_free(search_results_t* results) {
    if (!results) {
        return;
    }

    for (size_t i = 0; i < results->len; i++) {
        free(results->hits[i].path);
    }

    free(results->hits);
    *results = (search_results_t) { 0 };
}

bool search_scan_file(
    const char* path,
    rg_matcher_t* matcher,
    search_hit_fn on_hit,
    void* payload
) {
    if (!path || !matcher || !on_hit) {
        return false;
    }

    file_buffer_t file = { 0 };
    line_index_t lines = { 0 };
    bool ok = false;
    if (!loader_load(path, MMAP_THRESHOLD, &file)) {
        return false;
    }

    if (!line_index_build(&lines, file.data, file.len)) {
        goto cleanup;
    }

    ok = true;
    for (size_t at = 0; at <= file.len;) {
        rg_regex_match_t raw = { 0 };
        rg_regex_err_t err = rg_regex_matcher_find_at(
            matcher,
            &raw,
            (const char*) file.data,
            file.len,
            at
        );
        if (err == RG_REGEX_NO_MATCH) {
            break;
        }

        if (err != RG_REGEX_MATCH) {
            ok = false;
            break;
        }

        search_match_t match = search_locate_match(
            &lines,
            file.len,
            (search_range_t) {
                .start = raw.start,
                .end = raw.end,
            }
        );

        if (!on_hit(path, &file, &match, payload)) {
            ok = false;
            break;
        }

        at = raw.end > at ? raw.end : at + 1;
    }

cleanup:
    line_index_free(&lines);
    loader_free(&file);
    return ok;
}

static bool collect_hit(
    const char* path,
    const file_buffer_t* file,
    const search_match_t* match,
    void* payload
) {
    (void) file;
    return search_results_push(payload, path, match);
}

bool search_collect_file(
    const char* path,
    rg_matcher_t* matcher,
    search_results_t* results
) {
    return search_scan_file(path, matcher, collect_hit, results);
}
