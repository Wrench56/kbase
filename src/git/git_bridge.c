#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <git2.h>

#include "git/git_bridge.h"

#define MAX_UNIX_PATH_SIZE 4096

static void git_die(const char* msg, int32_t error) {
    const git_error* e = git_error_last();
    fprintf(stderr, "%s: %s (%d)\n", msg, e ? e->message : "unknown libgit2 error", error);
    exit(1);
}

static char* repo_name_from_url(const char* url) {
    const char* slash = strrchr(url, '/');
    const char* start = slash ? slash + 1 : url;
    const char* end = start + strlen(start);

    if ((size_t) (end - start) > 4 && strcmp(end - 4, ".git") == 0) {
        end -= 4;
    }

    size_t len = (size_t) (end - start);
    char* name = malloc(len + 1);
    if (name == NULL) {
        perror("malloc() failed");
        exit(1);
    }

    memcpy(name, start, len);
    name[len] = '\0';

    return name;
}

void git_init(void) {
    git_libgit2_init();
}

void git_free(void) {
    git_libgit2_shutdown();
}

git_repository* git_find_repo(char* cwd) {
    git_buf repo_name_buf = { 0 };
    int32_t error = git_repository_discover(&repo_name_buf, cwd, 1, NULL);
    if (error != 0) {
        git_buf_dispose(&repo_name_buf);
        git_die("Error: could not discover current repository. Please enter a knowledge base before sync", error);
    }

    git_repository* repo = NULL;
    error = git_repository_open_bare(&repo, repo_name_buf.ptr);
    git_buf_dispose(&repo_name_buf);
    if (error != 0) {
        git_die("Error: could not open knowledge base", error);
    }

    return repo;
}


void git_sync_repo(git_repository* repo, git_indexer_progress_cb transfer_progress_cb) {
    git_remote* remote;
    int32_t error = git_remote_lookup(&remote, repo, "origin");
    if (error != 0) {
        git_die("Error: could not lookup origin", error);
    }

    git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
    fetch_opts.callbacks.transfer_progress = transfer_progress_cb;

    error = git_remote_fetch(remote, NULL, &fetch_opts, NULL);
    if (error != 0) {
        git_die("Error: could not fetch from remote", error);
    }

    /* TODO: git_remote_stats() here */
    git_remote_free(remote);
}

git_repository* git_clone_repo(char* url, char* cwd,  git_indexer_progress_cb transfer_progress_cb, git_checkout_progress_cb checkout_progress_cb) {
    char* dir = repo_name_from_url(url);
    if (mkdir(dir, 0777) != 0) {
        perror("mkdir() error");
        exit(1);
    }

    char repo_dir[MAX_UNIX_PATH_SIZE];
    snprintf(repo_dir, sizeof(repo_dir), "%s/%s", cwd, dir);

    git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
    clone_opts.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    clone_opts.checkout_opts.progress_cb = checkout_progress_cb;
    clone_opts.fetch_opts.callbacks.transfer_progress = transfer_progress_cb;

    git_repository* repo = NULL;
    int32_t error = git_clone(&repo, url, repo_dir, &clone_opts);
    if (error != 0) {
        char errmsg[4096];
        snprintf(errmsg, sizeof(errmsg), "Error: could not clone knowledge base at \"%s\"", url);
        git_die(errmsg, error);
    }

    return repo;
}
