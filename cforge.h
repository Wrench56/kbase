#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200809L

#ifndef CFORGE_H
#define CFORGE_H

#if 0
{ [ "cforge.c" -nt ".b" ] || [ "cforge.h" -nt ".b" ]; } && { cc -O2 -Wall -Wextra -Wshadow -Wpedantic -Wconversion -Wstrict-prototypes -Wformat=2 -Wmissing-prototypes -Wold-style-definition -Wdouble-promotion -Wno-unused-parameter -std=c11 "cforge.c" -o "./.b" || exit 4; }
exec "./.b" "$@"
exit 0
#endif

/* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */
/*                                                     */
/*  CForge Build Tool                                  */
/*                                             v1.0.1  */
/*                                                     */
/* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */
/*                                                     */
/*  Author : Mark Devenyi                              */
/*  Source : https://github.com/Wrench56/cforge        */
/*  License: MIT License                               */
/*                                                     */
/* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= */

#include <glob.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* TODO: Port this to Windows someday */
#include <ftw.h>
#include <sys/stat.h>

/* TODO: Add other threading implementations (pthreads, WinAPI) */
#ifdef __STDC_NO_THREADS__
#error C11 threads library (threads.h) is needed for CForge!
#else
#include <threads.h>
#endif

#define CF_VERSION_MAJOR 1
#define CF_VERSION_MINOR 0
#define CF_VERSION_PATCH 1

#define CF_MAX_TARGETS 64
#define CF_MAX_CONFIGS 64
#define CF_MAX_GLOBS 64
#define CF_MAX_THRDS 16
#define CF_MAX_JOBS 64
#define CF_MAX_ENVS 256
#define CF_MAX_JOIN_STRINGS 256
#define CF_MAX_FILE_STRINGS 256
#define CF_MAX_SPLITS 256
#define CF_MAX_MAP_ATTRS 8
#define CF_MAX_MAPS 64
#define CF_MAX_DEFERRED_UTD 512
#define CF_INIT_PENDING_ENTRIES 64
#define CF_INIT_PENDING_STRING_SZ (4 * 1024)

#define CF_MAGIC_HEADER_VALUE 0xDBCF
#define CF_DB_CVERSION 0x5

#define CF_MAX_NAME_LENGTH 127
#define CF_MAX_OUTSTR_LENGTH 511
#define CF_MAX_COMMAND_LENGTH (1 * 1024)
#define CF_MAX_JOIN_STRING_LEN 8192

#define CF_ERR_LOG(...) fprintf(stderr, __VA_ARGS__)
#define CF_WRN_LOG(...) fprintf(stdout, __VA_ARGS__)

#define CF_SUCCESS_EC 0
#define CF_MAX_REACHED_EC 1
#define CF_NOT_FOUND_EC 2
#define CF_INVALID_STATE_EC 3
#define CF_CLIB_FAIL_EC 4
#define CF_TARGET_DEP_CYCLE_EC 5
#define CF_UNKNOWN_ATTR_EC 6
#define CF_DB_FAIL_EC 7
#define CF_IMPOSSIBLE_EC 8

/* TODO: Port this environment variable system to Windows */
extern char** environ;
static uint64_t denv_hash = 0;
static uint64_t cenv_hash = 0;

typedef void (*cf_target_fn)(void);
typedef void (*cf_config_fn)(void);


typedef enum {
    UNKNOWN = 0,
    DEPENDENCY,
    CONFIG_SET,
    HELP_STRING,
    HIDDEN,
    VERBOSE,
} cf_attr_type_t;

typedef struct {
    cf_attr_type_t type;
    union {
        struct {
            const char* target_name;
        } depends;
        struct {
            const char* config_name;
        } configset;
        struct {
            const char* help_string;
        } helpstring;
    } arg;
} cf_attr_t;

typedef enum {
    UNVISITED = 0,
    VISITING,
    DONE
} cf_dfs_node_status_t;

typedef struct {
    const char* name;
    cf_target_fn fn;
    cf_attr_t* attribs;
    size_t attribs_size;
    cf_dfs_node_status_t node_status;
} cf_target_decl_t;

typedef struct {
    const char* name;
    cf_config_fn fn;
} cf_config_decl_t;

typedef struct {
    const char* envname;
    char* value;
    bool was_set;
} cf_env_restore_t;

typedef struct {
    size_t c;
    char** p;
} cf_glob_t;

typedef struct {
    /* Magic header should be CFDB */
    uint16_t magic_header;
    uint16_t version;
    uint32_t reserved;
    size_t entry_cnt;
    size_t string_sz;
} cf_db_hdr_t __attribute__((aligned(8)));

typedef struct {
    uint64_t path_hash;
    uint64_t env_hash;
    uint64_t content_hash;
    uint64_t mtime_sec;
    uint64_t mtime_nsec;
    uint64_t size;
    size_t path_offset;
} cf_db_entry_t __attribute__((aligned(8)));

/* Technically never used */
typedef struct {
    /* Maximum path on Linux is 4KiB by default */
    uint16_t len;
    char* string;
} cf_db_lstring_t __attribute__((aligned(2)));

typedef struct {
    cf_db_hdr_t* header;
    cf_db_entry_t* entries;
    cf_db_lstring_t* strings;
    cf_db_entry_t* pending_entries;
    cf_db_lstring_t* pending_strings;
    /* max index */
    size_t pentries_max;
    size_t pentries_idx;
    size_t pstrings_sz;
    size_t pstrings_off;
} cf_db_mem_t;

static cf_db_mem_t* global_db = NULL;

typedef enum {
    REGISTER_PHASE = 0,
    TARGET_EXECUTE_PHASE = 1,
} cf_state_t;

typedef enum {
    MAP_UNKNOWN = 0,
    MAP_EXT,
    MAP_PARENT,
    MAP_DIRS,
} cf_map_attr_type_t;

typedef struct {
    cf_map_attr_type_t type;
    union {
        struct {
            char* n_ext;
        };
        struct {
            char* n_parent;
        };
        struct {
            char* n_dirs;
        };
    };
} cf_map_attr_t;

typedef struct {
    char** oarray;
    size_t size;
} cf_map_entry_t;

typedef struct {
    size_t c;
    char** p;
    char* buf;
} cf_split_t;

typedef struct {
    char* command;
} cf_thrd_job;

typedef struct {
    cf_thrd_job jobs[CF_MAX_JOBS];
    uint16_t active_jobs;
    int32_t front;
    int32_t back;
    mtx_t lock;
    cnd_t free_slot;
    cnd_t new_job;
    cnd_t no_job;
} cf_work_queue;

static cf_work_queue* global_workq = NULL;

static cf_target_decl_t cf_targets[CF_MAX_TARGETS] = { 0 };
static size_t cf_num_targets = 0;

static cf_config_decl_t cf_configs[CF_MAX_CONFIGS] = { 0 };
static size_t cf_num_configs = 0;

static glob_t cf_globs[CF_MAX_GLOBS] = { 0 };
static size_t cf_num_globs = 0;

static thrd_t cf_thrd_pool[CF_MAX_THRDS] = { 0 };
static size_t cf_num_thrds = 0;

static cf_env_restore_t cf_envs[CF_MAX_ENVS] = { 0 };
static size_t cf_num_envs = 0;

static char* cf_jstrings[CF_MAX_JOIN_STRINGS] = { 0 };
static size_t cf_num_jstrings = 0;

static cf_split_t cf_splits[CF_MAX_SPLITS] = { 0 };
static size_t cf_num_splits = 0;

static cf_map_entry_t cf_maps[CF_MAX_MAPS] = { 0 };
static size_t cf_num_maps = 0;

static char* cf_deferred_utd[CF_MAX_DEFERRED_UTD] = { 0 };
static size_t cf_num_deferred_utd = 0;

static char* cf_fstrings[CF_MAX_FILE_STRINGS] = { 0 };
static size_t cf_num_fstrings = 0;

static cf_state_t cf_state = REGISTER_PHASE;

static bool is_verbose_target = false;

static inline bool cf_empty_job(void) {
    return global_workq->front == global_workq->back;
}

static inline bool cf_full_job(void) {
    return ((global_workq->back + 1) % CF_MAX_JOBS) == global_workq->front;
}

static bool cf_enqueue_job(cf_thrd_job job) {
    if (cf_full_job()) {
        return false;
    }

    global_workq->jobs[global_workq->back] = job;
    global_workq->back = (global_workq->back + 1) % CF_MAX_JOBS;
    return true;
}

static bool cf_dequeue_job(cf_thrd_job* job) {
    if (cf_empty_job()) {
        return false;
    }

    *job = global_workq->jobs[global_workq->front];
    global_workq->jobs[global_workq->front] = (cf_thrd_job) { 0 };
    global_workq->front = (global_workq->front + 1) % CF_MAX_JOBS;
    return true;
}

static size_t cf_find_target_index(const char* target_name) {
    for (size_t i = cf_num_targets; i-- > 0;) {
        if (strncmp(target_name, cf_targets[i].name, CF_MAX_NAME_LENGTH) == 0) {
            return i;
        }
    }

    return CF_MAX_TARGETS + 1;
}

static void cf_register_target(const char* name, cf_target_fn fn, const cf_attr_t* attribs, size_t attribs_size) {
    if (cf_state != REGISTER_PHASE) {
        CF_ERR_LOG("Error: Invalid cf_state (%d) when registering target!\n", cf_state);
        exit(CF_INVALID_STATE_EC);
    }

    if (strlen(name) > CF_MAX_NAME_LENGTH) {
        CF_ERR_LOG("Error: The name \"%s\" when registering target is too long (max name length: %d)\n", name, CF_MAX_NAME_LENGTH);
        exit(CF_MAX_REACHED_EC);
    }

    if (cf_num_targets >= CF_MAX_TARGETS) {
        CF_ERR_LOG("Error: Maximum targets of %d was reached!\n", CF_MAX_TARGETS);
        exit(CF_MAX_REACHED_EC);
    }

    void* attribs_block;
    if (attribs_size > 0) {
        attribs_block = malloc(attribs_size * sizeof(cf_attr_t));
        if (attribs_block == NULL) {
            CF_ERR_LOG("Error: malloc() failed in cf_register_target()\n");
            exit(CF_CLIB_FAIL_EC);
        }

        memcpy(attribs_block, attribs, attribs_size * sizeof(cf_attr_t));
    } else {
        attribs_block = NULL;
    }

    cf_targets[cf_num_targets++] = (cf_target_decl_t) {
        .name = name,
        .fn = fn,
        .attribs = (cf_attr_t*) attribs_block,
        .attribs_size = attribs_size,
        .node_status = UNVISITED
    };
}

static void cf_register_config(const char* name, cf_config_fn fn) {
    if (cf_state != REGISTER_PHASE) {
        CF_ERR_LOG("Error: Invalid cf_state (%d) when registering config!\n", cf_state);
        exit(CF_INVALID_STATE_EC);
    }

    if (strlen(name) > CF_MAX_NAME_LENGTH) {
        CF_ERR_LOG("Error: The name \"%s\" when registering config is too long (max name length: %d)\n", name, CF_MAX_NAME_LENGTH);
        exit(CF_MAX_REACHED_EC);
    }

    if (cf_num_configs >= CF_MAX_CONFIGS) {
        CF_ERR_LOG("Error: Maximum configs of %d was reached!\n", CF_MAX_CONFIGS);
        exit(CF_MAX_REACHED_EC);
    }

    cf_configs[cf_num_configs++] = (cf_config_decl_t) {
        .name = name,
        .fn = fn
    };
}

__attribute__((unused)) static void cf_setenv_wrapper(const char* ident, char* value) {
    if (cf_num_envs >= CF_MAX_ENVS) {
        CF_ERR_LOG("Error: Maximum environment variables of %d was reached!\n", CF_MAX_ENVS);
        exit(CF_MAX_REACHED_EC);
    }

    char* envvar = getenv(ident);
    if (envvar == NULL) {
        cf_envs[cf_num_envs++] = (cf_env_restore_t) {
            .envname = ident,
            .value = NULL,
            .was_set = false
        };
    } else {
        char* value_block = (char*) malloc(strlen(envvar) + 1);
        if (value_block == NULL) {
            CF_ERR_LOG("Error: malloc() failed in cf_setenv_wrapper()\n");
            exit(CF_CLIB_FAIL_EC);
        }

        strcpy(value_block, envvar);
        cf_envs[cf_num_envs++] = (cf_env_restore_t) {
            .envname = ident,
            .value = value_block,
            .was_set = true
        };
    }

    if (setenv(ident, value, 1) != 0) {
        CF_ERR_LOG("Error: setenv() failed in cf_setenv_wrapper()\n");
        exit(CF_CLIB_FAIL_EC);
    }
}

static void cf_restore_env(size_t env_checkpoint) {
    cf_env_restore_t envres;
    while (cf_num_envs > env_checkpoint) {
        envres = cf_envs[--cf_num_envs];
        if (!envres.was_set) {
            if (unsetenv(envres.envname) != 0) {
                CF_ERR_LOG("Error: unsetenv() failed in cf_restore_env()\n");
                exit(CF_CLIB_FAIL_EC);
            }
        } else {
            if (setenv(envres.envname, envres.value, 1) != 0) {
                CF_ERR_LOG("Error: setenv() failed in cf_restore_env()\n");
                exit(CF_CLIB_FAIL_EC);
            }
        }

        free(envres.value);
    }
}

__attribute__((unused)) static cf_glob_t cf_glob(const char* expr) {
    glob_t glob_res = { 0 };
    int32_t rc = glob(expr, GLOB_NOSORT | GLOB_MARK | GLOB_NOESCAPE, NULL, &glob_res);
    
    if (rc == GLOB_NOMATCH) {
        globfree(&glob_res);
        return (cf_glob_t) {
            .c = 0,
            .p = NULL,
        };
    } else if (rc == GLOB_NOSPACE) {
        CF_ERR_LOG("Error: glob() ran out of memory during cf_glob() call!\n");
        exit(CF_CLIB_FAIL_EC);
    } else if (rc == GLOB_ABORTED) {
        CF_ERR_LOG("Error: glob() aborted due to a read error during cf_glob() call!\n");
        exit(CF_CLIB_FAIL_EC);
    }

    if (cf_num_globs >= CF_MAX_GLOBS) {
        CF_ERR_LOG("Error: Maximum globs of %d was reached!\n", CF_MAX_GLOBS);
        exit(CF_MAX_REACHED_EC);
    }

    cf_globs[cf_num_globs++] = glob_res;
    return (cf_glob_t) {
        .c = glob_res.gl_pathc,
        .p = glob_res.gl_pathv,
    };
}

static void cf_free_glob(size_t checkpoint) {
    while (cf_num_globs > checkpoint) {
        globfree(&cf_globs[--cf_num_globs]);
        cf_globs[cf_num_globs] = (glob_t) { 0 };
    }
}

__attribute__((unused)) static char* cf_join(char* strings[], char* separator, size_t length) {
    if (length < 1) {
        return (char*) "";
    }

    if (cf_num_jstrings >= CF_MAX_JOIN_STRINGS) {
        CF_ERR_LOG("Error: Maximum joined strings of %d was reached!\n", CF_MAX_JOIN_STRINGS);
        exit(CF_MAX_REACHED_EC);
    }

    char* jstring = (char*) malloc(CF_MAX_JOIN_STRING_LEN);
    if (jstring == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_join()\n");
        exit(CF_CLIB_FAIL_EC);
    }

    cf_jstrings[cf_num_jstrings++] = jstring;

    const char* endptr = jstring + CF_MAX_JOIN_STRING_LEN - 1;
    char* cptr = stpncpy(jstring, strings[0], (size_t) (endptr - jstring));
    for (size_t i = 1; i < length; i++) {
        cptr = stpncpy(cptr, separator, (size_t) (endptr - cptr));
        cptr = stpncpy(cptr, strings[i], (size_t) (endptr - cptr));
    }

    *cptr = '\0';
    return jstring;
}

static void cf_free_jstrings(size_t checkpoint) {
    while (cf_num_jstrings > checkpoint) {
        free(cf_jstrings[--cf_num_jstrings]);
        cf_jstrings[cf_num_jstrings] = NULL;
    }
}

__attribute__((unused)) static char** cf_map(char** sources, size_t src_length, cf_map_attr_t* attrs, size_t attr_length) {
    if (cf_num_maps >= CF_MAX_MAPS) {
        CF_ERR_LOG("Error: Maximum maps of %d was reached!\n", CF_MAX_MAPS);
        exit(CF_MAX_REACHED_EC);
    }
    
    
    char** oarray = (char**) malloc(src_length * sizeof(char*));
    if (oarray == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_map()\n");
        exit(CF_CLIB_FAIL_EC);
    }

    cf_maps[cf_num_maps++] = (cf_map_entry_t) {
        .oarray = oarray,
        .size = src_length
    };

    size_t oarray_idx = 0;
    for (size_t i = 0; i < src_length; i++) {
        char* outstr = (char*) malloc(CF_MAX_OUTSTR_LENGTH);
        if (outstr == NULL) {
            CF_ERR_LOG("Error: malloc() failed for outstr in cf_map()\n");
            exit(CF_CLIB_FAIL_EC);
        }

        strcpy(outstr, sources[i]);
        if (oarray_idx >= src_length) {
            CF_ERR_LOG("Error: Impossible error in cf_map()\n");
            exit(CF_IMPOSSIBLE_EC);
        }

        for (size_t j = 0; j < attr_length; j++) {
            cf_map_attr_t attr = attrs[j];
            switch (attr.type) {
                case MAP_EXT: {
                    /* TODO: Handle directories with dots in their path */
                    const char* dot = strrchr(outstr, '.');

                    size_t length = 0;
                    if (dot == NULL) {
                        length = strlen(outstr);
                        outstr[length++] = '.';
                    } else {
                        length = (size_t) (dot - outstr + 1);
                    }

                    size_t ext_len = strlen(attr.n_ext);
                    memcpy(outstr + length, attr.n_ext, ext_len);
                    outstr[length + ext_len] = '\0';
                    break;
                }
                case MAP_PARENT: {
                    /* TODO: Sanitize Windows path... */
                    const char* slash = strchr(outstr, '/');
                    if (slash == NULL) {
                        CF_WRN_LOG("Warning: No parent directory to replace in cf_map()\n");
                        break;
                    }

                    size_t length = strlen(attr.n_parent);
                    memmove(outstr + length, slash, strlen(slash) + 1);
                    memcpy(outstr, attr.n_parent, length);
                    break;
                }
                case MAP_DIRS: {
                    char* slash = strrchr(outstr, '/');
                    if (slash == NULL) {
                        CF_WRN_LOG("Warning: No directory found to replace in cf_map()\n");
                        break;
                    }

                    size_t length = strlen(attr.n_dirs);
                    memmove(outstr + length, slash + 1, strlen(slash + 1) + 1);
                    memcpy(outstr, attr.n_dirs, length);
                    break;
                }
                case MAP_UNKNOWN: {
                    CF_ERR_LOG("Error: MAP_UNKNOWN attribute detected!");
                    exit(CF_UNKNOWN_ATTR_EC);
                }
            }
         }

        oarray[oarray_idx++] = outstr;
    }

    return oarray;
}

static void cf_free_maps(size_t checkpoint) {
    while (cf_num_maps > checkpoint) {
        cf_map_entry_t entry = cf_maps[--cf_num_maps];
        size_t rem_elems = entry.size;
        while (rem_elems > 0) {
            free(entry.oarray[--rem_elems]);
        }

        free(entry.oarray);
        cf_maps[cf_num_maps] = (cf_map_entry_t) { 0 };
    }

}

__attribute__((unused)) static cf_split_t* cf_split(char* str, char delim) {
    if (cf_num_splits >= CF_MAX_SPLITS) {
        CF_ERR_LOG("Error: Maximum number of %d splits reached!", CF_MAX_SPLITS);
        exit(CF_MAX_REACHED_EC);
    }

    char* buf = strdup(str);
    if (buf == NULL) {
        CF_ERR_LOG("Error: strdup() failed in cf_split()!\n");
        exit(CF_CLIB_FAIL_EC);
    }

    size_t count = 1;
    for (char* s = buf; *s; s++) {
        if (*s == delim) {
            ++count;
        }
    }

    char** parts = (char**) malloc(count * sizeof(char*));
    if (parts == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_split()!\n");
        free(buf);
        exit(CF_CLIB_FAIL_EC);
    }

    size_t idx = 0;
    parts[idx++] = buf;
    for (char* s = buf; *s; s++) {
        if (*s == delim) {
            *s = '\0';
            parts[idx++] = s + 1;
        }
    }

    cf_splits[cf_num_splits] = (cf_split_t) {
        .c = idx,
        .p = parts,
        .buf = buf
    };

    return &cf_splits[cf_num_splits++];
}
static void cf_free_splits(size_t checkpoint) {
    while (cf_num_splits > checkpoint) {
        cf_split_t entry = cf_splits[--cf_num_splits];
        free(entry.p);
        free(entry.buf);
        cf_splits[cf_num_splits] = (cf_split_t) { 0 };
    }

}

__attribute__((unused)) static inline bool cf_file_exists(char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}


__attribute__((unused)) static void cf_mkdirp(const char* path) {
    if (cf_file_exists((char*) path)) {
        return;
    }

    char temp[4096];
    size_t len = strlen(path);
    if (len >= sizeof(temp)) {
        CF_ERR_LOG("Error: Path too long in cf_mkdirp()!\n");
        exit(CF_IMPOSSIBLE_EC);
    }

    memcpy(temp, path, len + 1);
    for (char* chr = temp + 1; *chr != '\0'; chr++) {
        if (*chr == '/') {
            *chr = '\0';
            mkdir(temp, 0755);
            *chr = '/';
        }
    }

    mkdir(temp, 0755);
}


static int32_t cf_remove_helper(const char* fpath, const struct stat* sb, int32_t typeflag, struct FTW* ftwbuf) {
    if (remove(fpath) != 0) {
        CF_WRN_LOG("Warning: Could not remove \"%s\" in cf_remove_helper()!\n", fpath);
    }

    return 0;
}

__attribute__((unused)) static inline void cf_remove(const char* path) {
    if (path == NULL) {
        CF_WRN_LOG("Warning: Path passed to cf_remove() was NULL!\n");
        return;
    }

    if (path[0] == '/') {
        const char* sptr = strchr(path + 1, '/');
        if (sptr == NULL) {
            CF_ERR_LOG("Error: Refusing to remove top-level path \"%s\"!\n", path);
            exit(CF_CLIB_FAIL_EC);
        }

        while (*(++sptr) != '\0') {
            if (*sptr != '/') {
                goto remove_entry;
            }
        }

        CF_ERR_LOG("Error: Refusing to remove top-level path \"%s\"!\n", path);
        exit(CF_CLIB_FAIL_EC);
    }

remove_entry:
    nftw(path, cf_remove_helper, 64, FTW_DEPTH | FTW_PHYS);
}

__attribute__((unused)) static void cf_write_file(const char* path, const char* mode, ...) {
    va_list args;
    va_start(args, mode);
    char* fmt = va_arg(args, char*);

    FILE* fp = fopen(path, mode);
    if (fp == NULL) {
        CF_WRN_LOG("Warning: Could not write to \"%s\"!\n", path);
        va_end(args);
        return;
    }

    vfprintf(fp, fmt, args);
    va_end(args);
    fclose(fp);
}

__attribute__((unused)) static char* cf_read_file(const char* path) {
    FILE* fp = fopen(path, "r");
    if (fp == NULL) {
        CF_WRN_LOG("Warning: fopen() returned NULL in cf_read_file()!\n");
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    size_t sz = (size_t) ftell(fp);
    rewind(fp);

    char* buf = (char*) malloc(sz + 1);
    if (buf == NULL) {
        fclose(fp);
        CF_ERR_LOG("Error: malloc() failed in cf_read_file()!\n");
        exit(CF_CLIB_FAIL_EC);
    }

    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    if (cf_num_fstrings >= CF_MAX_FILE_STRINGS) {
        CF_ERR_LOG("Error: Maximum file strings of %d reached!\n", CF_MAX_FILE_STRINGS);
        exit(CF_MAX_REACHED_EC);
    }

    cf_fstrings[cf_num_fstrings++] = buf;
    return buf;
}

static void cf_free_fstrings(size_t checkpoint) {
    while (cf_num_fstrings > checkpoint) {
        free(cf_fstrings[--cf_num_fstrings]);
        cf_fstrings[cf_num_fstrings] = NULL;
    }
}

static int cf_thrd_helper(void* queue) {
    cf_work_queue* q = (cf_work_queue*) queue;
    cf_thrd_job job;
    mtx_t* lock = &q->lock;

    while (true) {
        mtx_lock(lock);
        while (cf_empty_job()) {
            if (q->active_jobs == 0) {
                cnd_signal(&q->no_job);
            }
            cnd_wait(&q->new_job, lock);
        }
        cf_dequeue_job(&job);
        cnd_signal(&q->free_slot);
        mtx_unlock(lock);

        if (job.command == NULL) {
            break;
        }

        if (system((char*) job.command) != 0) {
            CF_ERR_LOG("Error: Executing command \"%s\" failed\n", (char*) job.command);
            exit(CF_CLIB_FAIL_EC);
        }

        free(job.command);
        mtx_lock(lock);
        --q->active_jobs;
        mtx_unlock(lock);
    }
    
    return 0;
}

__attribute__((unused)) static void cf_execute_command(bool is_parallel, char* buffer) {
    if (is_verbose_target) {
        printf("%s\n", buffer);
    }

    if (is_parallel) {
        mtx_t* lock = &global_workq->lock;
        mtx_lock(lock);

        while (cf_full_job()) {
            while (cf_num_thrds < CF_MAX_THRDS) {    
                thrd_t worker_thread = 0;
                if (thrd_create(&worker_thread, &cf_thrd_helper, (void*) global_workq) != thrd_success) {
                    CF_ERR_LOG("Error: Thread failed during creation in cf_execute_command()\n");
                    exit(CF_CLIB_FAIL_EC);
                }

                cf_thrd_pool[cf_num_thrds++] = worker_thread;
            }

            cnd_wait(&global_workq->free_slot, lock);
        }

        cf_enqueue_job((cf_thrd_job) {
            .command = buffer,
        });
        ++global_workq->active_jobs;
        cnd_signal(&global_workq->new_job);

        if (global_workq->active_jobs > cf_num_thrds && cf_num_thrds < CF_MAX_THRDS) {
            thrd_t worker_thread;
            if (thrd_create(&worker_thread, &cf_thrd_helper, (void*) global_workq) != thrd_success) {
                CF_ERR_LOG("Error: Thread failed during creation in cf_execute_command()\n");
                exit(CF_CLIB_FAIL_EC);
            }

            cf_thrd_pool[cf_num_thrds++] = worker_thread;
        }

        mtx_unlock(lock);
        return;
    }

    if (system(buffer) != 0) {
        CF_ERR_LOG("Error: Executing command \"%s\" failed", (char*) buffer);
        exit(CF_CLIB_FAIL_EC);
    }

    free(buffer);
}

/* Compact XXH64 implementation */
static const uint64_t XXH64_P1 = 0x9E3779B185EBCA87;
static const uint64_t XXH64_P2 = 0xC2B2AE3D27D4EB4F;
static const uint64_t XXH64_P3 = 0x165667B19E3779F9;
static const uint64_t XXH64_P4 = 0x85EBCA77C2B2AE63;
static const uint64_t XXH64_P5 = 0x27D4EB2F165667C5;

static inline uint64_t xxh64_rotl(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

static inline uint64_t xxh64_read64(const void *p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

static inline uint64_t xxh64_read32(const void *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static inline uint64_t xxh64_round(uint64_t acc, uint64_t input) {
    return xxh64_rotl(acc + input * XXH64_P2, 31) * XXH64_P1;
}

static uint64_t xxh64(uint8_t* data, size_t len, uint64_t seed) {
    uint8_t* p = data;
    uint8_t *end = p + len;
    uint64_t h;

    if (len >= 32) {
        uint64_t v[] = {seed + XXH64_P1 + XXH64_P2, seed + XXH64_P2, seed, seed - XXH64_P1};
        while (p <= end - 32) {
            for (int i = 0; i < 4; i++) {
                v[i] = xxh64_round(v[i], xxh64_read64(p + i * 8));
            }

            p += 32;
        }

        h = xxh64_rotl(v[0],1) + xxh64_rotl(v[1],7) + xxh64_rotl(v[2],12) + xxh64_rotl(v[3],18);
        for (int i = 0; i < 4; i++) {
            h = (h ^ xxh64_round(0, v[i])) * XXH64_P1 + XXH64_P4;
        }
    } else {
        h = seed + XXH64_P5;
    }

    h += (uint64_t)len;

    while (p + 8 <= end) {
        h ^= xxh64_round(0, xxh64_read64(p));
        h = xxh64_rotl(h, 27) * XXH64_P1 + XXH64_P4;
        p += 8;
    }
    while (p + 4 <= end) {
        h ^= xxh64_read32(p) * XXH64_P1;
        h = xxh64_rotl(h, 23) * XXH64_P2 + XXH64_P3;
        p += 4;
    }
    while (p < end) {
        h ^= *p * XXH64_P5;
        h = xxh64_rotl(h, 11) * XXH64_P1;
        p++;
    }

    h ^= h >> 33;
    h *= XXH64_P2;
    h ^= h >> 29;
    h *= XXH64_P3;
    h ^= h >> 32;
    return h;
}

/* CForge DB implementation */
static void cf_db_free(cf_db_mem_t* db) {
    if (db == NULL) {
        return;
    }

    cf_db_hdr_t* hdr = db->header;
    if (hdr != NULL) {
        free(hdr);
        db->header = NULL;
    }

    cf_db_entry_t* entries = db->entries;
    if (entries != NULL) {
        free(entries);
        db->entries = NULL;
    }

    cf_db_lstring_t* strings = db->strings;
    if (strings != NULL) {
        free(strings);
        db->strings = NULL;
    }

    cf_db_entry_t* pentries = db->pending_entries;
    if (pentries != NULL) {
        free(pentries);
        db->pending_entries = NULL;
    }

    cf_db_lstring_t* pstrings = db->pending_strings;
    if (pstrings != NULL) {
        free(pstrings);
        db->pending_strings = NULL;
    }

    free(db);
}

static cf_db_mem_t* cf_db_load(const char* db_path) {
    FILE* fp = fopen(db_path, "rb");

    cf_db_mem_t* db = (cf_db_mem_t*) malloc(sizeof(cf_db_mem_t));
    cf_db_hdr_t* hdr = (cf_db_hdr_t*) malloc(sizeof(cf_db_hdr_t));
    cf_db_entry_t* pentries = (cf_db_entry_t*) malloc(CF_INIT_PENDING_ENTRIES * sizeof(cf_db_entry_t));
    cf_db_lstring_t* pstrings = (cf_db_lstring_t*) malloc(CF_INIT_PENDING_STRING_SZ);
    if (db == NULL || hdr == NULL || pentries == NULL || pstrings == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_load_db() for db_mem\n");
        if (fp != NULL) {
            fclose(fp);
        }
    
        free(hdr);
        free(pentries);
        free(pstrings);
        free(db);
        exit(CF_CLIB_FAIL_EC);
    }

    memset(db, 0, sizeof(cf_db_mem_t));
    db->header = hdr;
    db->pending_entries = pentries;
    db->pending_strings = pstrings;
    db->pentries_max = CF_INIT_PENDING_ENTRIES;
    db->pstrings_sz = CF_INIT_PENDING_STRING_SZ;
    db->pentries_idx = 0;
    db->pstrings_off = 0;

    /* Default when DB not found */
    if (fp == NULL) {
        CF_WRN_LOG("Warning: DB at path not found, using default\n");
        hdr->magic_header = CF_MAGIC_HEADER_VALUE;
        hdr->version = CF_DB_CVERSION;
        hdr->reserved = 0;
        hdr->entry_cnt = 0;
        hdr->string_sz = 0;
        db->entries = NULL;
        db->strings = NULL;
        return db;
    }

    if (fread(hdr, sizeof(cf_db_hdr_t), 1, fp) != 1) {
        CF_ERR_LOG("Error: Could not read database header\n");   
        fclose(fp);
        cf_db_free(db);
        exit(CF_DB_FAIL_EC);
    }

    if (hdr->magic_header != CF_MAGIC_HEADER_VALUE) {
        CF_ERR_LOG("Error: Magic database header code is invalid!\n");
        fclose(fp);
        cf_db_free(db);
        exit(CF_DB_FAIL_EC);
    }

    if (hdr->version != CF_DB_CVERSION) {
        CF_ERR_LOG("Error: CForge version (v%d) does not match database version (v%d)\n", CF_DB_CVERSION, hdr->version);
        fclose(fp);
        cf_db_free(db);
        exit(CF_DB_FAIL_EC);
    }

    cf_db_entry_t* entries = (cf_db_entry_t*) malloc(hdr->entry_cnt * sizeof(cf_db_entry_t));
    if (entries == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_load_db() for entries\n");
        fclose(fp);
        cf_db_free(db);
        exit(CF_CLIB_FAIL_EC);
    }

    if (fread(entries, sizeof(cf_db_entry_t), hdr->entry_cnt, fp) != hdr->entry_cnt) {  
        CF_ERR_LOG("Error: Could not read database entries\n");
        fclose(fp);
        cf_db_free(db);
        exit(CF_DB_FAIL_EC);
    }

    cf_db_lstring_t* strings = (cf_db_lstring_t*) malloc(hdr->string_sz);
    if (strings == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_load_db() for strings\n");
        fclose(fp);
        cf_db_free(db);
        exit(CF_CLIB_FAIL_EC);
    }

    if (fread(strings, 1, hdr->string_sz, fp) != hdr->string_sz) {  
        CF_ERR_LOG("Error: Could not read database entries\n");
        fclose(fp);
        cf_db_free(db);
        exit(CF_DB_FAIL_EC);
    }
    
    db->entries = entries;
    db->strings = strings;
    fclose(fp);
    return db;
}

static void cf_db_save(const char* db_path, cf_db_mem_t* db) {
    if (db == NULL) {
        CF_ERR_LOG("Error: db passed to cf_save_db() is NULL");
        return;
    }

    FILE* fp = fopen(db_path, "wb");
    if (fp == NULL) {
        CF_ERR_LOG("Error: Could not open database file\n");
        cf_db_free(db);
        exit(CF_DB_FAIL_EC);
    }

    cf_db_hdr_t* hdr = db->header;
    size_t entry_cnt = hdr->entry_cnt;
    size_t string_sz = hdr->string_sz;
    hdr->entry_cnt += db->pentries_idx;
    hdr->string_sz += db->pstrings_off;
    if(fwrite(hdr, sizeof(cf_db_hdr_t), 1, fp) != 1) {
        CF_ERR_LOG("Error: Could not write database header\n");
        fclose(fp);
        cf_db_free(db);
        exit(CF_DB_FAIL_EC);
    }

    if (db->entries != NULL) {
        if (fwrite(db->entries, sizeof(cf_db_entry_t), entry_cnt, fp) != entry_cnt) {
            CF_ERR_LOG("Error: Could not write database entries\n");
            fclose(fp);
            cf_db_free(db);
            exit(CF_DB_FAIL_EC);
        }
    }
    
    if (db->pentries_idx > 0) {
        if (fwrite(db->pending_entries, sizeof(cf_db_entry_t), db->pentries_idx, fp) != db->pentries_idx) {
            CF_ERR_LOG("Error: Could not write new database entries\n");
            fclose(fp);
            cf_db_free(db);
            exit(CF_DB_FAIL_EC);
        }
    }

    if (db->strings != NULL) {
        if (fwrite(db->strings, 1, string_sz, fp) != string_sz) {
            CF_ERR_LOG("Error: Could not write database strings\n");
            fclose(fp);
            cf_db_free(db);
            exit(CF_DB_FAIL_EC);
        }
    }

    if (db->pstrings_off > 0) {
        if (fwrite(db->pending_strings, 1, db->pstrings_off, fp) != db->pstrings_off) {
            CF_ERR_LOG("Error: Could not write new database strings\n");
            fclose(fp);
            cf_db_free(db);
            exit(CF_DB_FAIL_EC);
        }
    }

    fclose(fp);
    cf_db_free(db);
}

static cf_db_entry_t* cf_db_find(char* path, cf_db_mem_t* db) {
    size_t plen = strlen(path);
    uint64_t hash = xxh64((uint8_t*) path, plen, 0);
    for (size_t i = 0; i < db->pentries_idx; i++) {
        cf_db_entry_t* entry = &db->pending_entries[i];
        if (hash == entry->path_hash) {
            uint8_t* slab = (uint8_t*) db->pending_strings;
            uint16_t* len_slot = (uint16_t*) (slab + entry->path_offset);
            uint16_t strl = *len_slot;
            char* strptr = (char*) (len_slot + 1);
            if (strncmp(path, strptr, strl) == 0) {
                return entry;
            } else {
                CF_WRN_LOG("Warning: Path hash collision detected!\n");
            }
        }
    }

    if (db->entries == NULL || db->strings == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < db->header->entry_cnt; i++) {
        cf_db_entry_t* entry = &db->entries[i];
        if (hash == entry->path_hash) {
            uint8_t* slab = (uint8_t*) db->strings;
            uint16_t* len_slot = (uint16_t*) (slab + entry->path_offset);
            uint16_t strl = *len_slot;
            char* strptr = (char*) (len_slot + 1);
            if (strncmp(path, strptr, strl) == 0) {
                return &db->entries[i];
            } else {
                CF_WRN_LOG("Warning: Path hash collision detected!\n");
            }
        }
    }

    return NULL;
}

static bool cf_db_hash_file(char* path, uint64_t* hash) {
#ifdef CF_DISABLE_FILE_HASH
    (void) path;
    *hash = 0;
    return true;
#else
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }

    ssize_t sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return false;
    }

    rewind(fp);
    uint8_t* buf = (uint8_t*) malloc((size_t) sz + 1);
    if (buf == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_db_hash_file()\n");
        exit(CF_CLIB_FAIL_EC);
    }

    if (fread(buf, 1, (size_t) sz, fp) != (size_t) sz) {
        fclose(fp);
        free(buf);
        return false;
    }

    fclose(fp);
    buf[sz] = '\0';
    *hash = xxh64(buf, (size_t) sz, 0);
    free(buf);
    return true;
#endif // CF_DISABLE_FILE_HASH
}

__attribute__((unused)) static void cf_db_mark_utd(char* path, cf_db_mem_t* db) {
    cf_db_entry_t* entry = cf_db_find(path, global_db);
    bool is_new = (entry == NULL);
    size_t saved_pstrings_off = db->pstrings_off;
    size_t saved_pentries_idx = db->pentries_idx;

    if (is_new) {
        size_t str_offset = db->pstrings_off;
        size_t strl = strlen(path);
        if (strl > UINT16_MAX) {
            CF_ERR_LOG("Error: Path length exceeds UINT16 length\n");
            exit(CF_MAX_REACHED_EC);
        }
        size_t needed = sizeof(uint16_t) + strl + 1;
        if (str_offset + needed > db->pstrings_sz) {
            CF_ERR_LOG("Error: Pending strings buffer ran out of space!");
            exit(CF_MAX_REACHED_EC);
        }

        uint8_t* slab = (uint8_t*) db->pending_strings;
        uint16_t* len_slot = (uint16_t*) (slab + db->pstrings_off);
        *len_slot = (uint16_t) strl;
        memcpy(len_slot + 1, path, strl + 1);

        size_t idx = db->pentries_idx;
        if (idx >= db->pentries_max) {
            CF_ERR_LOG("Error: Pending entries buffer ran out of space!");
            exit(CF_MAX_REACHED_EC);
        }

        entry = &db->pending_entries[idx];
        entry->path_offset = str_offset;
        entry->path_hash = xxh64((uint8_t*) path, strl, 0);
        db->pentries_idx++;
        db->pstrings_off += needed;
    }

    struct stat st;
    if (stat(path, &st) == -1) {
        db->pstrings_off = saved_pstrings_off;
        db->pentries_idx = saved_pentries_idx;
        return;
    }

    uint64_t hash = 0;
    if (!cf_db_hash_file(path, &hash)) {
        db->pstrings_off = saved_pstrings_off;
        db->pentries_idx = saved_pentries_idx;
        return;
    }

    entry->mtime_sec = (uint64_t) st.st_mtim.tv_sec;
    entry->mtime_nsec = (uint64_t) st.st_mtim.tv_nsec;
    entry->size = (uint64_t) st.st_size;
    entry->env_hash = cenv_hash;
    entry->content_hash = hash;
}

__attribute__((unused)) static void cf_db_defer_mark_utd(char* path) {
    if (cf_num_deferred_utd >= CF_MAX_DEFERRED_UTD) {
        CF_ERR_LOG("Error: Maximum deferred UTD marks reached!");
        exit(CF_MAX_REACHED_EC);
    }

    char* ptr = strdup(path);
    if (ptr == NULL) {
        CF_ERR_LOG("Error: strdup() failed in cf_db_defer_mark_utd()!\n");
        exit(CF_CLIB_FAIL_EC);
    }

    cf_deferred_utd[cf_num_deferred_utd++] = ptr;
}

__attribute__((unused)) static bool cf_file_utd(char* path) {
    cf_db_entry_t* entry = cf_db_find(path, global_db);
    if (entry == NULL) {
        return false;
    }

    struct stat st;
    if (stat(path, &st) == -1) {
        return false;
    }

    if (entry->size != (uint64_t) st.st_size) {
        return false;
    }

    if (entry->mtime_nsec != (uint64_t) st.st_mtim.tv_nsec) {
        return false;
    }

    if (entry->mtime_sec != (uint64_t) st.st_mtim.tv_sec) {
        return false;
    }

    if (cenv_hash != entry->env_hash) {
        return false;
    }

    /* TODO: Optimize the above so that this never has to run */
    uint64_t hash = 0;
    if (cf_db_hash_file(path, &hash) == false) {
        return false;
    }

    if (hash != entry->content_hash) {
        return false;
    }

    return true;
}

static inline uint64_t cf_hash_env(char** env) {
    uint64_t hash = 0;
    for (char** entry = env; *entry != NULL; entry++) {
        size_t len = strlen(*entry);
        hash ^= xxh64((uint8_t*) *entry, len, 0);
    }

    return hash;
}

static void cf_dfs_execute(cf_target_decl_t* target, cf_config_decl_t* inherited_config) {
    if (target->node_status == DONE) {
        return;
    } else if (target->node_status == VISITING) {
        CF_ERR_LOG("Error: Dependency cycle detected for \"%s\"\n", target->name);
        exit(CF_TARGET_DEP_CYCLE_EC);
    }

    target->node_status = VISITING;
    cf_config_decl_t* config = NULL;
    bool dep_ran = false;

    for (size_t i = 0; i < target->attribs_size; i++) {
        cf_attr_t* attrib = &target->attribs[i];
        switch (attrib->type) {
            case DEPENDENCY: {
                const char* dep_target_name = attrib->arg.depends.target_name;
                size_t dep_idx = cf_find_target_index(dep_target_name);
                if (dep_idx >= cf_num_targets) {
                    CF_ERR_LOG("Error: Target \"%s\" not found!\n", dep_target_name);
                    exit(CF_NOT_FOUND_EC);
                }

                dep_ran = true;
                cf_dfs_execute(&cf_targets[dep_idx], (config == NULL) ? inherited_config : config);
                break;
            }
            case CONFIG_SET: {
                if (config != NULL) {
                    CF_WRN_LOG("Warning: Cannot set two or more configs per target. Ignoring...\n");
                    goto next_attr;
                }

                if (dep_ran == true) {
                    CF_ERR_LOG("Error: Config attribute(s) specified later than first dependency attribute in target\"%s\"!\n", target->name);
                    exit(CF_INVALID_STATE_EC);
                }
                
                const char* conf_name = attrib->arg.configset.config_name;
                for (size_t c_idx = 0; c_idx < cf_num_configs; c_idx++) {
                    if (strncmp(conf_name, cf_configs[c_idx].name, CF_MAX_NAME_LENGTH) == 0) {
                        config = &cf_configs[c_idx];
                        goto next_attr;
                    }
                }
                
                CF_ERR_LOG("Error: Config \"%s\" not found!\n", conf_name);
                exit(CF_NOT_FOUND_EC);
                break;
            }
            case VERBOSE: {
                if (is_verbose_target) {
                    CF_WRN_LOG("Warning: VERBOSE attribute passed to target \"%s\" multiple times!\n", target->name);
                    break;
                }

                is_verbose_target = true;
                break;
            }
            case HELP_STRING:
            case HIDDEN:
                goto next_attr;
            case UNKNOWN: {
                CF_ERR_LOG("Error: Unknown attribute given for target \"%s\"\n", target->name);
                exit(CF_UNKNOWN_ATTR_EC);
            }
        }

next_attr:
        continue;
    }

    size_t env_checkpoint = cf_num_envs;
    if (config != NULL) {
        config->fn();
        cenv_hash = cf_hash_env(environ);
    } else if (inherited_config != NULL) {
        inherited_config->fn();
        cenv_hash = cf_hash_env(environ);
    } else {
        /* Commands in system() can't change parent environment! */
        cenv_hash = denv_hash;
    }


    size_t glob_checkpoint = cf_num_globs;
    size_t jstrings_checkpoint = cf_num_jstrings;
    size_t splits_checkpoint = cf_num_splits;
    size_t maps_checkpoint = cf_num_maps;
    size_t fstrings_checkpoint = cf_num_fstrings;
    target->fn();

    mtx_t* lock = &global_workq->lock;
    mtx_lock(lock);
    while (global_workq->active_jobs > 0 || !cf_empty_job()) {
        cnd_wait(&global_workq->no_job, lock);
    }
    mtx_unlock(lock);

    for (size_t i = 0; i < cf_num_deferred_utd; i++) {
        cf_db_mark_utd(cf_deferred_utd[i], global_db);
        free(cf_deferred_utd[i]);
    }
    cf_num_deferred_utd = 0;

    cf_free_fstrings(fstrings_checkpoint);
    cf_free_maps(maps_checkpoint);
    cf_free_splits(splits_checkpoint);
    cf_free_jstrings(jstrings_checkpoint);
    cf_free_glob(glob_checkpoint);
    cf_restore_env(env_checkpoint);

    is_verbose_target = false;

    target->node_status = DONE;
}


__attribute__((weak)) int main(int argc, char** argv) {
    (void) cf_register_config;
    (void) cf_register_target;
    (void) cf_glob;
    (void) cf_join;

    if (argc == 1) {
        printf(
            "\ncforge.h - v%d.%d.%d\n\nUsage:\n ./cforge.h <target> [...]\n\nAvailable targets:\n",
            CF_VERSION_MAJOR,
            CF_VERSION_MINOR,
            CF_VERSION_PATCH
        );
        for (size_t i = 0; i < cf_num_targets; i++) {
            cf_target_decl_t target = cf_targets[i];
            const char* help_str = NULL;
            bool hidden = false;
            for (size_t j = 0; j < target.attribs_size; j++) {
                if (target.attribs[j].type == HELP_STRING) {
                    if (help_str != NULL) {
                        CF_WRN_LOG("Warning: Help string for target \"%s\" provided multiple times! Using first definition.", target.name);
                        continue;
                    }
                    help_str = target.attribs[j].arg.helpstring.help_string;
                } else if (target.attribs[j].type == HIDDEN) {
                    if (hidden) {
                        CF_WRN_LOG("Warning: Hidden attribute for target \"%s\" used multiple times!",  target.name);
                        continue;
                    }
                    hidden = true;
                }
            }

            if (hidden) {
                goto next_target;
            }

            if (help_str == NULL) {
                printf(" > %s\n", cf_targets[i].name);
            } else {
                printf(" > %s - %s\n", cf_targets[i].name, help_str);
            }
next_target:
            continue;
        }

        goto cleanup;
    }

    global_db = cf_db_load(".cforge.db");
    denv_hash = cf_hash_env(environ);

    global_workq = (cf_work_queue*) malloc(sizeof(cf_work_queue));
    if (global_workq == NULL) {
        CF_ERR_LOG("Error: malloc() failed in main()\n");
        exit(CF_CLIB_FAIL_EC);
    }
    global_workq->front = 0;
    global_workq->back = 0;
    global_workq->active_jobs = 0;
    mtx_init(&global_workq->lock, mtx_plain);
    cnd_init(&global_workq->free_slot);
    cnd_init(&global_workq->new_job);
    cnd_init(&global_workq->no_job);

    cf_state = TARGET_EXECUTE_PHASE;
    for (int32_t i = 1; i < argc; i++) {
        for (size_t j = 0; j < cf_num_targets; j++) {
            cf_target_decl_t* target = &cf_targets[j];
            if (strcmp(target->name, argv[i]) == 0) {
                if (target->node_status == DONE) {
                    CF_WRN_LOG("Warning: Target \"%s\" was executed already! Skipping target...\n", argv[i]);
                    goto next_iter;
                }
                cf_dfs_execute(target, NULL);
                goto next_iter;
            }
        }

        CF_ERR_LOG("Error: Target \"%s\" not found!\n", argv[i]);
        return CF_NOT_FOUND_EC;

        next_iter:
            continue;
    }

    cf_db_save(".cforge.db", global_db);

    mtx_lock(&global_workq->lock);
    while (cf_full_job()) {
        cnd_wait(&global_workq->no_job, &global_workq->lock);
    }
    for (size_t t = 0; t < cf_num_thrds; t++) {

        cf_enqueue_job((cf_thrd_job) {
            .command = NULL
        });
        cnd_signal(&global_workq->new_job);
    }
    mtx_unlock(&global_workq->lock);

    for (size_t t = cf_num_thrds; t > 0; t--) {
        thrd_join(cf_thrd_pool[t - 1], NULL);
        cf_thrd_pool[t - 1] = (thrd_t) { 0 };
    }

    mtx_destroy(&global_workq->lock);
    cnd_destroy(&global_workq->free_slot);
    cnd_destroy(&global_workq->new_job);
    cnd_destroy(&global_workq->no_job);
    free(global_workq);

cleanup:
    for (size_t t_idx = 0; t_idx < cf_num_targets; t_idx++) {
        free(cf_targets[t_idx].attribs);
    }

    return CF_SUCCESS_EC;
}


#define CF_TARGET(name_ident, ...) \
    static void cf_target_##name_ident(void); \
    __attribute__((constructor)) static void cf_target_reg_##name_ident(void) { \
        const cf_attr_t attribs[] = { __VA_ARGS__ }; \
        cf_register_target(#name_ident, cf_target_##name_ident, attribs, sizeof(attribs)/sizeof(cf_attr_t)); \
    } \
    static void cf_target_##name_ident(void)


#define CF_CONFIG(name_ident) \
    static void cf_config_##name_ident(void); \
    __attribute__((constructor)) static void cf_config_reg_##name_ident(void) { \
        cf_register_config(#name_ident, cf_config_##name_ident); \
    } \
    static void cf_config_##name_ident(void)

#define CF_GLOB(expr) \
    cf_glob(expr)


/* Hack to make the `for` syntax possible for `CF_GLOBS_EACH()` */
typedef struct {
    cf_glob_t glob;
    size_t    checkpoint;
} cf_glob_iter_hack_t;

static inline cf_glob_iter_hack_t cf_glob_begin_hack(const char *expr) {
    /* Inlined so this is fine... */
    size_t local_num_globs = cf_num_globs;
    return (cf_glob_iter_hack_t){
        .glob = cf_glob(expr),
        .checkpoint = local_num_globs,
    };
}

#define CF_GLOBS_EACH(expr, filename) \
    (cf_glob_iter_hack_t cf_cgh_##filename = cf_glob_begin_hack(expr); \
    cf_cgh_##filename.glob.p != NULL; \
    cf_free_glob(cf_cgh_##filename.checkpoint), (void)(cf_cgh_##filename.glob.p = NULL)) \
    for (char **cf_ci_##filename = cf_cgh_##filename.glob.p, \
        *filename = *cf_ci_##filename; \
        cf_ci_##filename < cf_cgh_##filename.glob.p + cf_cgh_##filename.glob.c; \
        filename = *++cf_ci_##filename)

#define CF_INTERNAL_RUNNER(parallel, format_str, ...) \
    do { \
        char* buffer = malloc(CF_MAX_COMMAND_LENGTH); \
        if (buffer == NULL) { \
            CF_ERR_LOG("Error: malloc() failed in CF_INTERNAL_RUNNER\n"); \
            exit(CF_CLIB_FAIL_EC); \
        } \
        int n = snprintf(buffer, CF_MAX_COMMAND_LENGTH, format_str, ##__VA_ARGS__); \
        if (n < 0) { \
            CF_ERR_LOG("Error: snprintf() failed in CF_INTERNAL_RUNNER\n"); \
            exit(CF_CLIB_FAIL_EC); \
        }else if (n >= CF_MAX_COMMAND_LENGTH) { \
            CF_ERR_LOG("Error: Maximum command length of %d was reached!\n", CF_MAX_COMMAND_LENGTH); \
            exit(CF_MAX_REACHED_EC); \
        } \
        cf_execute_command(parallel, buffer); \
    } while (0);

#define CF_RUNP(format_str, ...) CF_INTERNAL_RUNNER(true, format_str, ##__VA_ARGS__)
#define CF_RUN(format_str, ...) CF_INTERNAL_RUNNER(false, format_str, ##__VA_ARGS__)

#define CF_DEPENDS(target_ident) \
    (cf_attr_t) { \
        .type = DEPENDENCY, \
        .arg.depends = { \
            .target_name = #target_ident \
        } \
    }

#define CF_WITH_CONFIG(config_ident) \
    (cf_attr_t) { \
        .type = CONFIG_SET, \
        .arg.configset = { \
            .config_name = #config_ident \
        } \
    }

#define CF_HELP_STRING(str) \
    (cf_attr_t) { \
        .type = HELP_STRING, \
        .arg.helpstring = { \
            .help_string = str \
        } \
    }

#define CF_HIDDEN \
    (cf_attr_t) { \
        .type = HIDDEN, \
        .arg.depends = { 0 } \
    }

#define CF_VERBOSE \
    (cf_attr_t) { \
        .type = VERBOSE, \
        .arg.depends = { 0 } \
    }

#define CF_SET_ENV(ident, value) cf_setenv_wrapper(#ident, value)
#define CF_ENV(ident) getenv(#ident)

#define CF_MAPA(sources, len, ...) \
    cf_map(sources, len, (cf_map_attr_t[]) { __VA_ARGS__ }, (sizeof((cf_map_attr_t[]) { __VA_ARGS__ })/sizeof(cf_map_attr_t)))

#define CF_MAP(source, ...) \
    CF_MAPA((char*[]) { source }, 1, __VA_ARGS__)[0]

#define CF_MAP_EXT(new_ext) \
    (cf_map_attr_t) { \
        .type = MAP_EXT, \
        .n_ext = new_ext \
    }

#define CF_MAP_PARENT(new_parent) \
    (cf_map_attr_t) { \
        .type = MAP_PARENT, \
        .n_parent = new_parent \
    }

#define CF_MAP_DIRS(new_dirs) \
    (cf_map_attr_t) { \
        .type = MAP_DIRS, \
        .n_dirs = new_dirs \
    }

#define CF_JOIN(arr, sep, len) \
    cf_join(arr, sep, len)

#define CF_JOIN_GLOB(glob, sep) \
    cf_join(glob.p, sep, glob.c)

#define CF_SPLIT(str, delim) \
    cf_split(str, delim)

#define CF_FILE_UTD(filepath) \
    (cf_file_utd((char*) filepath))

#define CF_FILE_NOT_UTD(filepath) \
    (!cf_file_utd((char*) filepath))

#define CF_FILE_MARK_UTD(filepath) \
    cf_db_mark_utd(filepath, global_db)

#define CF_FILE_MARK_UTDP(filepath) \
    cf_db_defer_mark_utd((char*) filepath)

#define CF_FILE_EXISTS(filepath) \
    (cf_file_exists((char*) filepath))

#define CF_MKDIR(path) \
    cf_mkdirp((char*) path)

#define CF_REMOVE(path) \
    cf_remove((char*) path)

#define CF_WRITE(path, ...) \
    cf_write_file((char*) path, "w", __VA_ARGS__)

#define CF_APPEND(path, ...) \
    cf_write_file((char*) path, "a", __VA_ARGS__)

#define CF_READ(path) \
    cf_read_file((char*) path)

#define CF_CONCAT_(a, b) a##b
#define CF_CONCAT(a, b) CF_CONCAT_(a, b)

#define CF_BANNER_IMPL(id, ...) \
    do { \
        static bool CF_CONCAT(cf_banner_shown_, id) = false; \
        if (!CF_CONCAT(cf_banner_shown_, id)) { \
            CF_CONCAT(cf_banner_shown_, id) = true; \
            printf(__VA_ARGS__); \
            putchar('\n'); \
        } \
    } while (0)

#define CF_BANNER(...) \
    CF_BANNER_IMPL(__COUNTER__, __VA_ARGS__)

#define CF_NOP() \
    do {} while (0)

#define CF_BOLD "\x1b[1m"
#define CF_UNDERLINE "\x1b[4m"
#define CF_INVERSE "\x1b[7m"
#define CF_BLACK "\x1b[30m"
#define CF_RED "\x1b[31m"
#define CF_GREEN "\x1b[32m"
#define CF_YELLOW "\x1b[33m"
#define CF_BLUE "\x1b[34m"
#define CF_MAGENTA "\x1b[35m"
#define CF_CYAN "\x1b[36m"
#define CF_WHITE "\x1b[37m"
#define CF_BG_BLACK "\x1b[40m"
#define CF_BG_RED "\x1b[41m"
#define CF_BG_GREEN "\x1b[42m"
#define CF_BG_YELLOW "\x1b[43m"
#define CF_BG_BLUE "\x1b[44m"
#define CF_BG_MAGENTA "\x1b[45m"
#define CF_BG_CYAN "\x1b[46m"
#define CF_BG_WHITE "\x1b[47m"
#define CF_RESET "\x1b[0m"

#define CF_VERSION_AT_LEAST(major, minor, patch) \
    ( \
        (CF_VERSION_MAJOR > (major)) \
        || (CF_VERSION_MAJOR == (major) && CF_VERSION_MINOR > (minor)) \
        || (CF_VERSION_MAJOR == (major) && CF_VERSION_MINOR == (minor) \
            && CF_VERSION_PATCH >= (patch)) \
    )

#define CF_VERSION_BELOW(major, minor, patch) \
    (!CF_VERSION_AT_LEAST(major, minor, patch))

#define CF_VERSION_EXACT(major, minor, patch) \
    (CF_VERSION_MAJOR == (major) && CF_VERSION_MINOR == (minor) \
    && CF_VERSION_PATCH == (patch))

#endif // CFORGE_H
