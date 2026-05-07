#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <git2.h>

#include "git/git_bridge.h"

#define MAX_UNIX_PATH_SIZE 4096

static __attribute__((noreturn)) void git_die(const char* msg, int32_t error) {
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
    error = git_repository_open(&repo, repo_name_buf.ptr);
    git_buf_dispose(&repo_name_buf);
    if (error != 0) {
        git_die("Error: could not open knowledge base", error);
    }

    return repo;
}

static void git_fast_forward_current_branch(git_repository* repo, git_checkout_progress_cb checkout_progress_cb, git_checkout_notify_cb checkout_notify_cb) {
    git_reference* head = NULL;
    git_reference* upstream = NULL;
    git_reference* updated_head = NULL;
    git_annotated_commit* upstream_commit = NULL;
    git_object* upstream_target = NULL;

    const char* errmsg = NULL;
    int32_t error = 0;

    error = git_repository_head(&head, repo);
    if (error != 0) {
        errmsg = "Error: could not read HEAD";
        goto cleanup;
    }

    error = git_branch_upstream(&upstream, head);
    if (error != 0) {
        errmsg = "Error: current branch has no upstream";
        goto cleanup;
    }

    const git_oid* upstream_oid = git_reference_target(upstream);
    if (upstream_oid == NULL) {
        error = -1;
        errmsg = "Error: upstream reference has no target";
        goto cleanup;
    }

    error = git_annotated_commit_lookup(&upstream_commit, repo, upstream_oid);
    if (error != 0) {
        errmsg = "Error: could not lookup upstream commit";
        goto cleanup;
    }

    const git_annotated_commit* heads[] = { upstream_commit };
    git_merge_analysis_t analysis = 0;
    git_merge_preference_t preference = 0;

    error = git_merge_analysis(&analysis, &preference, repo, heads, 1);
    if (error != 0) {
        errmsg = "Error: could not analyze merge";
        goto cleanup;
    }

    if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
        goto cleanup;
    }

    if (!(analysis & GIT_MERGE_ANALYSIS_FASTFORWARD)) {
        error = -1;
        errmsg = "Error: branch cannot be fast-forwarded";
        goto cleanup;
    }

    error = git_object_lookup(&upstream_target, repo, upstream_oid, GIT_OBJECT_COMMIT);
    if (error != 0) {
        errmsg = "Error: could not lookup upstream target";
        goto cleanup;
    }

    git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
    checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    checkout_opts.progress_cb = checkout_progress_cb;
    checkout_opts.notify_cb = checkout_notify_cb;
    checkout_opts.notify_flags = GIT_CHECKOUT_NOTIFY_ALL;

    error = git_checkout_tree(repo, upstream_target, &checkout_opts);
    if (error != 0) {
        errmsg = "Error: could not checkout upstream tree";
        goto cleanup;
    }

    error = git_reference_set_target(&updated_head, head, upstream_oid, "kbase sync: fast-forward");
    if (error != 0) {
        errmsg = "Error: could not update branch ref";
        goto cleanup;
    }

cleanup:
    if (updated_head) git_reference_free(updated_head);
    if (upstream_target) git_object_free(upstream_target);
    if (upstream_commit) git_annotated_commit_free(upstream_commit);
    if (upstream) git_reference_free(upstream);
    if (head) git_reference_free(head);
    if (errmsg) git_die(errmsg, error);
}

void git_sync_repo(git_repository* repo, git_indexer_progress_cb transfer_progress_cb, git_checkout_progress_cb checkout_progress_cb, git_checkout_notify_cb notify_cb) {
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

    git_fast_forward_current_branch(repo, checkout_progress_cb, notify_cb);

    /* TODO: git_remote_stats() here */
    git_remote_free(remote);
}

git_repository* git_clone_repo(char* url, const char* cwd,  git_indexer_progress_cb transfer_progress_cb, git_checkout_progress_cb checkout_progress_cb) {
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
        free(dir);
        git_die(errmsg, error);
    }

    free(dir);
    return repo;
}
