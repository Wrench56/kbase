#include "search/loader.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static bool load_heap(int fd, size_t len, file_buffer_t* out) {
    unsigned char* data = malloc(len + 1);
    if (!data) {
        return false;
    }

    size_t total = 0;

    while (total < len) {
        ssize_t n = read(fd, data + total, len - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            free(data);
            return false;
        }

        if (n == 0) {
            break;
        }

        total += (size_t) n;
    }

    data[total] = 0;

    out->data = data;
    out->len = total;
    out->kind = FILE_BUFFER_HEAP;
    out->owned = data;
    out->owned_len = total + 1;
    out->fd = -1;

    return true;
}

static bool load_mmap(int fd, size_t len, file_buffer_t* out) {
    if (len == 0) {
        return load_heap(fd, len, out);
    }

    void* data = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        return false;
    }

    out->data = data;
    out->len = len;
    out->kind = FILE_BUFFER_MMAP;
    out->owned = data;
    out->owned_len = len;
    out->fd = fd;

    return true;
}

bool loader_load(
    const char* path,
    const size_t mmap_threshold,
    file_buffer_t* out
) {
    if (!path || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->fd = -1;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return false;
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);
        return false;
    }

    if (st.st_size < 0) {
        close(fd);
        return false;
    }

    size_t len = (size_t) st.st_size;

    bool ok;
    if (len >= mmap_threshold) {
        ok = load_mmap(fd, len, out);

        if (!ok) {
            lseek(fd, 0, SEEK_SET);
            ok = load_heap(fd, len, out);
            close(fd);
        }
    } else {
        ok = load_heap(fd, len, out);
        close(fd);
    }

    if (!ok) {
        close(fd);
        memset(out, 0, sizeof(*out));
        out->fd = -1;
        return false;
    }

    return true;
}

void loader_free(file_buffer_t* file) {
    if (!file) {
        return;
    }

    if (file->kind == FILE_BUFFER_MMAP && file->owned) {
        munmap(file->owned, file->owned_len);

        if (file->fd >= 0) {
            close(file->fd);
        }
    } else if (file->kind == FILE_BUFFER_HEAP && file->owned) {
        free(file->owned);
    }

    memset(file, 0, sizeof(*file));
    file->fd = -1;
}
