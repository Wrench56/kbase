#ifndef KBASE_SEARCHER_H
#define KBASE_SEARCHER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct search_range {
    size_t start;
    size_t end;
} search_range_t;

typedef struct search_location {
    size_t line;
    size_t column;
} search_location_t;

typedef struct search_match {
    search_range_t match_span;
    search_range_t line_span;
    search_location_t location;
} search_match_t;

typedef struct line_index {
    size_t* starts;
    size_t len;
    size_t cap;
} line_index_t;

bool line_index_build(line_index_t* index, const uint8_t* data, size_t len);
void line_index_free(line_index_t* index);

search_match_t search_locate_match(
    const line_index_t* index,
    size_t len,
    search_range_t match
);

#endif // KBASE_SEARCHER_H
