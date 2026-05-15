#ifndef KBASE_LOADER_H
#define KBASE_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum file_buffer_kind {
    FILE_BUFFER_HEAP,
    FILE_BUFFER_MMAP,
} file_buffer_kind_t;

typedef struct file_buffer {
    const uint8_t* data;
    size_t len;

    file_buffer_kind_t kind;

    void* owned;
    size_t owned_len;

#ifdef _WIN32
    void* file_handle;
    void* mapping_handle;
#else
    int fd;
#endif
} file_buffer_t;

bool loader_load(const char* path, size_t mmap_threshold, file_buffer_t* out);
void loader_free(file_buffer_t* file);

#endif // KBASE_LOADER_H
