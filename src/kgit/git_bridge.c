#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <git2.h>

#include "kgit/git_bridge.h"

#define MAX_UNIX_PATH_SIZE 4096
#define KBASE_SSH_FILE_ENVVAR_NAME "KBASE_SSH_PRIV_FILE"

static __attribute__((noreturn)) void git_die(const char* msg, int32_t error) {
    const git_error* e = git_error_last();
    fprintf(
        stderr,
        "%s: %s (%d)\n",
        msg,
        e ? e->message : "unknown libgit2 error",
        error
    );
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

int32_t ssh_agent_cred_cb(
    git_credential** out,
    const char* url,
    const char* username_from_url,
    uint32_t allowed_types,
    void* payload
) {
    int32_t error = 1;
    char username[256] = { 0 };
    char* password = NULL;

-    printf("Authenticating for \"%s\"...\n", url);
    if (username_from_url != NULL) {
        strcpy(username, username_from_url);
    } else {
        printf("Username: ");
        if (fgets(username, sizeof(username), stdin) == NULL) {
            goto out;
        }

        username[strcspn(username, "\r\n")] = '\0';
    }

    if (allowed_types & GIT_CREDENTIAL_SSH_KEY) {
        char* privpath = getenv(KBASE_SSH_FILE_ENVVAR_NAME);
        if (privpath == NULL || privpath[0] == '\0') {
            fprintf(
                stderr,
                "Error: KBASE_SSH_PRIV_FILE environment variable not set!\n"
            );
            goto out;
        }

        size_t privlen = strlen(privpath);
        char pubpath[privlen + 5];
        memcpy(pubpath, privpath, privlen);
        pubpath[privlen] = '.';
        pubpath[privlen + 1] = 'p';
        pubpath[privlen + 2] = 'u';
        pubpath[privlen + 3] = 'b';
        pubpath[privlen + 4] = '\0';

        password = getpass("Password: ");
        if (password == NULL) {
            goto out;
        }

        error = git_credential_ssh_key_new(
            out,
            username,
            pubpath,
            privpath,
            password
        );
    } else if (allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) {
        password = getpass("Password: ");
        if (password == NULL) {
            goto out;
        }

        error = git_credential_userpass_plaintext_new(out, username, password);
    } else if (allowed_types & GIT_CREDENTIAL_USERNAME) {
        error = git_credential_username_new(out, username);
    }

out:
    if (password != NULL) {
        memset(password, 0, strlen(password));
        password = NULL;
    }
    return error;
}

static bool git_fetch_origin(
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
    opts.callbacks.credentials = ssh_agent_cred_cb;
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
        git_die(errmsg, error);
    }
    return ret;
}

static git_reference* kgit_new_branch_from_remote(
    git_repository* repo,
    const char* branch_name
) {
    git_reference* remote = NULL;
    git_reference* local = NULL;
    git_object* target = NULL;

    char* errmsg = NULL;

    char remote_branch[1024];
    snprintf(remote_branch, sizeof(remote_branch), "origin/%s", branch_name);

    int32_t error = git_branch_lookup(
        &remote,
        repo,
        remote_branch,
        GIT_BRANCH_REMOTE
    );
    if (error < 0) {
        errmsg = "Failed to find remote branch";
        goto cleanup;
    }

    error = git_reference_peel(&target, remote, GIT_OBJECT_COMMIT);
    if (error < 0) {
        errmsg = "Failed to peel reference";
        goto cleanup;
    }

    error = git_branch_create(
        &local,
        repo,
        branch_name,
        (git_commit*) target,
        0
    );
    if (error < 0) {
        errmsg = "Failed to create branch";
        goto cleanup;
    }

    error = git_branch_set_upstream(local, remote_branch);
    if (error < 0) {
        errmsg = "Failed to set branch upstream";
        goto cleanup;
    }

cleanup:
    if (remote) {
        git_reference_free(remote);
    }
    if (target) {
        git_object_free(target);
    }

    return local;
}

void kgit_init(void) {
    git_libgit2_init();
}

void kgit_free(void) {
    git_libgit2_shutdown();
}

git_repository* kgit_find_repo(char* cwd) {
    git_buf repo_name_buf = { 0 };
    int32_t error = git_repository_discover(&repo_name_buf, cwd, 1, NULL);
    if (error != 0) {
        git_buf_dispose(&repo_name_buf);
        git_die(
            "Error: could not discover current repository. Please enter a "
            "knowledge base before sync",
            error
        );
    }

    git_repository* repo = NULL;
    error = git_repository_open(&repo, repo_name_buf.ptr);
    git_buf_dispose(&repo_name_buf);
    if (error != 0) {
        git_die("Error: could not open knowledge base", error);
    }

    return repo;
}

static void kgit_fast_forward_current_branch(
    git_repository* repo,
    git_checkout_progress_cb checkout_progress_cb,
    git_checkout_notify_cb checkout_notify_cb
) {
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

    error = git_object_lookup(
        &upstream_target,
        repo,
        upstream_oid,
        GIT_OBJECT_COMMIT
    );
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

    error = git_reference_set_target(
        &updated_head,
        head,
        upstream_oid,
        "kbase sync: fast-forward"
    );
    if (error != 0) {
        errmsg = "Error: could not update branch ref";
        goto cleanup;
    }

cleanup:
    if (updated_head) {
        git_reference_free(updated_head);
    }
    if (upstream_target) {
        git_object_free(upstream_target);
    }
    if (upstream_commit) {
        git_annotated_commit_free(upstream_commit);
    }
    if (upstream) {
        git_reference_free(upstream);
    }
    if (head) {
        git_reference_free(head);
    }
    if (errmsg) {
        git_die(errmsg, error);
    }
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
    clone_opts.fetch_opts.callbacks.credentials = ssh_agent_cred_cb;

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
        git_die(errmsg, error);
    }

    free(dir);
    return repo;
}

void kgit_sync_workspace_branch(
    git_repository* repo,
    const char* branch_name,
    git_indexer_progress_cb transfer_progress_cb,
    git_checkout_progress_cb checkout_progress_cb,
    git_checkout_notify_cb notify_cb
) {
    git_reference* branch = NULL;
    bool has_upstream = true;

    git_fetch_origin(repo, transfer_progress_cb);
    if (!kgit_switch_branch(repo, branch_name)) {
        branch = kgit_new_branch_from_remote(repo, branch_name);
        if (branch == NULL) {
            has_upstream = false;
            branch = kgit_new_branch(repo, (char*) branch_name);
        }

        if (branch == NULL) {
            git_die("Could not create workspace branch", -1);
        }

        git_reference_free(branch);
        if (!kgit_switch_branch(repo, branch_name)) {
            git_die("Could not switch to workspace branch", -1);
        }
    }

    if (has_upstream) {
        kgit_fast_forward_current_branch(repo, checkout_progress_cb, notify_cb);
    }
}

git_reference* kgit_new_branch(git_repository* repo, char* name) {
    git_reference* head_ref = NULL;
    git_object* head_obj = NULL;
    git_reference* branch = NULL;

    int32_t error = git_repository_head(&head_ref, repo);
    if (error < 0) {
        goto cleanup;
    }

    error = git_reference_peel(&head_obj, head_ref, GIT_OBJECT_COMMIT);
    if (error < 0) {
        goto cleanup;
    }

    error = git_branch_create(&branch, repo, name, (git_commit*) head_obj, 0);
    if (error < 0) {
        goto cleanup;
    }

cleanup:
    if (head_obj) {
        git_object_free(head_obj);
    }
    if (head_ref) {
        git_reference_free(head_ref);
    }

    return branch;
}

bool kgit_is_branch(git_repository* repo, char* branch_name) {
    git_reference* head = NULL;
    const char* name = NULL;
    bool ret = false;

    int32_t error = git_repository_head(&head, repo);
    if (error < 0) {
        goto cleanup;
    }

    error = git_branch_name(&name, head);
    if (error < 0) {
        goto cleanup;
    }

    if (strcmp(name, branch_name) == 0) {
        ret = true;
    }

cleanup:
    if (head) {
        git_reference_free(head);
    }
    return ret;
}

bool kgit_switch_branch(git_repository* repo, const char* name) {
    git_reference* branch = NULL;
    git_object* target = NULL;
    bool ret = false;

    char* errmsg = NULL;

    int32_t error = git_branch_lookup(&branch, repo, name, GIT_BRANCH_LOCAL);
    if (error < 0) {
        errmsg = "Failed to find local branch";
        goto cleanup;
    }

    error = git_reference_peel(&target, branch, GIT_OBJECT_COMMIT);
    if (error < 0) {
        errmsg = "Failed to find commit on branch";
        goto cleanup;
    }

    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_SAFE | GIT_CHECKOUT_RECREATE_MISSING;

    error = git_checkout_tree(repo, target, &opts);
    if (error < 0) {
        errmsg = "Failed to checkout tree";
        goto cleanup;
    }

    char headstr[1024] = { 0 };
    snprintf(headstr, sizeof(headstr), "refs/heads/%s", name);

    error = git_repository_set_head(repo, headstr);
    if (error < 0) {
        errmsg = "Failed to set HEAD";
        goto cleanup;
    }

    ret = true;

cleanup:
    if (branch) {
        git_reference_free(branch);
    }
    if (target) {
        git_object_free(target);
    }
    if (errmsg) {
        git_die(errmsg, error);
    }
    return ret;
}

void kgit_commit_all(git_repository* repo, char* msg) {
    git_index* index = NULL;
    git_reference* head = NULL;
    git_tree* tree = NULL;
    git_commit* parent = NULL;
    git_config* cfg = NULL;
    git_config_entry* mentry = NULL;
    git_config_entry* nentry = NULL;

    char* errmsg = NULL;

    git_oid tree_oid;

    int32_t error = git_repository_index(&index, repo);
    if (error < 0) {
        errmsg = "Failed to fetch index";
        goto cleanup;
    }

    git_strarray paths_arr = { 0 };
    error = git_index_add_all(
        index,
        &paths_arr,
        GIT_INDEX_ADD_DEFAULT,
        NULL,
        NULL
    );
    if (error < 0) {
        errmsg = "Failed to stage all files (add)";
        goto cleanup;
    }

    error = git_index_update_all(index, &paths_arr, NULL, NULL);
    if (error < 0) {
        errmsg = "Failed to stage all files (update)";
        goto cleanup;
    }

    error = git_repository_head(&head, repo);
    if (error < 0) {
        errmsg = "Failed to fetch refhead";
        goto cleanup;
    }

    const git_oid* parent_oid = git_reference_target(head);
    if (!parent_oid) {
        errmsg = "Failed to resolve reftarget";
        goto cleanup;
    }

    error = git_commit_lookup(&parent, repo, parent_oid);
    if (error < 0) {
        errmsg = "Failed to fetch parent commit";
        goto cleanup;
    }

    error = git_config_open_default(&cfg);
    if (error < 0) {
        errmsg = "Failed to open config";
        goto cleanup;
    }

    error = git_config_get_entry(&mentry, cfg, "user.email");
    if (error < 0) {
        errmsg = "Failed to fetch email from config";
        goto cleanup;
    }

    error = git_config_get_entry(&nentry, cfg, "user.name");
    if (error < 0) {
        errmsg = "Failed to fetch name from config";
        goto cleanup;
    }

    git_signature* me = NULL;
    error = git_signature_now(&me, nentry->value, mentry->value);
    if (error < 0) {
        errmsg = "Failed to create commit signature";
        goto cleanup;
    }

    error = git_index_write(index);
    if (error < 0) {
        errmsg = "Failed to write index";
        goto cleanup;
    }

    error = git_index_write_tree(&tree_oid, index);
    if (error < 0) {
        errmsg = "Failed to write index tree";
        goto cleanup;
    }

    error = git_tree_lookup(&tree, repo, &tree_oid);
    if (error < 0) {
        errmsg = "Failed to fetch tree";
        goto cleanup;
    }

    const git_commit* parents[] = { parent };
    git_oid new_commit;
    error = git_commit_create(
        &new_commit,
        repo,
        "HEAD",
        me,
        me,
        "UTF-8",
        msg,
        tree,
        1,
        parents
    );
    if (error < 0) {
        errmsg = "Failed to create new commit";
    }

cleanup:
    if (index) {
        git_index_free(index);
    }
    if (head) {
        git_reference_free(head);
    }
    if (tree) {
        git_tree_free(tree);
    }
    if (parent) {
        git_commit_free(parent);
    }
    if (cfg) {
        git_config_free(cfg);
    }
    if (mentry) {
        git_config_entry_free(mentry);
    }
    if (nentry) {
        git_config_entry_free(nentry);
    }
    if (errmsg) {
        git_die(errmsg, error);
    }
}

bool kgit_has_new_changes(git_repository* repo) {
    git_status_list* status = NULL;
    bool ret = false;

    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    int32_t error = git_status_list_new(&status, repo, &opts);
    if (error < 0) {
        goto cleanup;
    }

    if (git_status_list_entrycount(status) > 0) {
        ret = true;
    }

cleanup:
    if (status) {
        git_status_list_free(status);
    }
    return ret;
}

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
    opts.callbacks.credentials = ssh_agent_cred_cb;
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
        git_die(errmsg, error);
    }
}
