#include "search/searcher.h"

#include <stdlib.h>
#include <string.h>

static bool line_index_push(line_index_t* index, size_t value) {
    if (index->len == index->cap) {
        size_t new_cap = index->cap ? index->cap * 2 : 1024;

        size_t* new_starts = realloc(
            index->starts,
            new_cap * sizeof(*new_starts)
        );

        if (!new_starts) {
            return false;
        }

        index->starts = new_starts;
        index->cap = new_cap;
    }

    index->starts[index->len++] = value;
    return true;
}

bool line_index_build(
    line_index_t* index,
    const unsigned char* data,
    size_t len
) {
    if (!index || (!data && len != 0)) {
        return false;
    }

    index->starts = NULL;
    index->len = 0;
    index->cap = 0;

    if (!line_index_push(index, 0)) {
        return false;
    }

    const unsigned char* cursor = data;
    const unsigned char* end = data + len;

    while (cursor < end) {
        const unsigned char* newline = memchr(
            cursor,
            '\n',
            (size_t) (end - cursor)
        );

        if (!newline) {
            break;
        }

        if (newline + 1 < end) {
            size_t next_line = (size_t) ((newline + 1) - data);

            if (!line_index_push(index, next_line)) {
                line_index_free(index);
                return false;
            }
        }

        cursor = newline + 1;
    }

    return true;
}

void line_index_free(line_index_t* index) {
    if (!index) {
        return;
    }

    free(index->starts);

    index->starts = NULL;
    index->len = 0;
    index->cap = 0;
}

static size_t line_index_find(const line_index_t* index, size_t offset) {
    size_t lo = 0;
    size_t hi = index->len;

    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (index->starts[mid] <= offset) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    return lo;
}

search_match_t search_locate_match(
    const line_index_t* index,
    size_t len,
    search_range_t match
) {
    if (match.start > len) {
        match.start = len;
    }

    if (match.end > len) {
        match.end = len;
    }

    if (match.end < match.start) {
        match.end = match.start;
    }

    size_t line_idx = line_index_find(index, match.start);
    size_t line_start = index->starts[line_idx];
    size_t line_end = len;

    if (line_idx + 1 < index->len) {
        line_end = index->starts[line_idx + 1];
    }

    return (search_match_t) {
        .match_span = match,
        .line_span = {
            .start = line_start,
            .end = line_end,
        },
        .location = {
            .line = line_idx + 1,
            .column = match.start - line_start + 1,
        },
    };
}
