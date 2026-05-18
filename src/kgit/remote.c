#include <git2.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "kgit/branch.h"
#include "kgit/common.h"

#define MAX_UNIX_PATH_SIZE 4096

void kgit_push_branch(git_repository* repo, char* branch_name) {
    git_remote* remote = NULL;
    git_strarray refspecs = { 0 };
    char refspec[2048];
    char* errmsg = NULL;

    int32_t error = git_remote_lookup(&remote, repo, "origin");
    if (error < 0) {
        errmsg = "Failed to look up Git remote";
        goto cleanup;
    }

    snprintf(
        refspec,
        sizeof(refspec),
        "refs/heads/%s:refs/heads/%s",
        branch_name,
        branch_name
    );
    char* specs[] = { refspec };
    refspecs.strings = specs;
    refspecs.count = 1;

    git_push_options opts = GIT_PUSH_OPTIONS_INIT;
    opts.callbacks.credentials = kgit_ssh_agent_cred_cb;
    error = git_remote_push(remote, &refspecs, &opts);
    if (error < 0) {
        errmsg = "Failed to push to remote";
        goto cleanup;
    }

cleanup:
    if (remote) {
        git_remote_free(remote);
    }
    if (errmsg) {
        kgit_die(errmsg, error);
    }
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

bool git_fetch_origin(
    git_repository* repo,
    git_indexer_progress_cb transfer_progress_cb
) {
    git_remote* remote = NULL;

    char* errmsg = NULL;
    bool ret = false;

    int32_t error = git_remote_lookup(&remote, repo, "origin");
    if (error < 0) {
        errmsg = "Failed to look up remote";
        goto cleanup;
    }

    git_fetch_options opts = GIT_FETCH_OPTIONS_INIT;
    opts.callbacks.credentials = kgit_ssh_agent_cred_cb;
    opts.callbacks.transfer_progress = transfer_progress_cb;

    error = git_remote_fetch(remote, NULL, &opts, NULL);
    if (error < 0) {
        errmsg = "Failed to fetch remote";
        goto cleanup;
    }

    ret = true;

cleanup:
    if (remote) {
        git_remote_free(remote);
    }
    if (errmsg) {
        kgit_die(errmsg, error);
    }
    return ret;
}

void kgit_sync_repo(
    git_repository* repo,
    git_indexer_progress_cb transfer_progress_cb,
    git_checkout_progress_cb checkout_progress_cb,
    git_checkout_notify_cb notify_cb
) {
    git_fetch_origin(repo, transfer_progress_cb);
    kgit_fast_forward_current_branch(repo, checkout_progress_cb, notify_cb);

    /* TODO: git_remote_stats() here */
}

git_repository* kgit_clone_repo(
    char* url,
    const char* cwd,
    git_indexer_progress_cb transfer_progress_cb,
    git_checkout_progress_cb checkout_progress_cb
) {
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
    clone_opts.fetch_opts.callbacks.credentials = kgit_ssh_agent_cred_cb;

    git_repository* repo = NULL;
    int32_t error = git_clone(&repo, url, repo_dir, &clone_opts);
    if (error != 0) {
        char errmsg[4096];
        snprintf(
            errmsg,
            sizeof(errmsg),
            "Error: could not clone knowledge base at \"%s\"",
            url
        );
        free(dir);
        kgit_die(errmsg, error);
    }

    free(dir);
    return repo;
}
