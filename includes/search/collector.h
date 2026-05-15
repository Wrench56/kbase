#ifndef KBASE_COLLECTOR_H
#define KBASE_COLLECTOR_H

#include <stdbool.h>
#include <stddef.h>

#include "rg_regex.h"
#include "search/loader.h"
#include "search/searcher.h"

typedef struct search_hit {
    char* path;
    search_match_t match;
} search_hit_t;

typedef struct search_results {
    search_hit_t* hits;
    size_t len;
    size_t cap;
} search_results_t;

typedef bool (*search_hit_fn)(
    const char* path,
    const file_buffer_t* file,
    const search_match_t* match,
    void* payload
);

bool search_scan_file(
    const char* path,
    rg_matcher_t* matcher,
    search_hit_fn on_hit,
    void* payload
);

bool search_results_push(
    search_results_t* results,
    const char* path,
    const search_match_t* match
);

void search_results_free(search_results_t* results);

bool search_collect_file(
    const char* path,
    rg_matcher_t* matcher,
    search_results_t* results
);

#endif // KBASE_COLLECTOR_H
