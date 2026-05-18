#include <git2.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "kgit/common.h"
#include "kgit/remote.h"

#include "kgit/branch.h"

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

static bool kgit_current_branch_has_upstream(git_repository* repo) {
    git_reference* head = NULL;
    git_reference* upstream = NULL;
    bool ret = false;

    if (git_repository_head(&head, repo) < 0) {
        goto cleanup;
    }

    if (git_branch_upstream(&upstream, head) == 0) {
        ret = true;
    }

cleanup:
    if (upstream) {
        git_reference_free(upstream);
    }
    if (head) {
        git_reference_free(head);
    }

    return ret;
}

void kgit_fast_forward_current_branch(
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
        kgit_die(errmsg, error);
    }
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
            kgit_die("Could not create workspace branch", -1);
        }

        git_reference_free(branch);
        if (!kgit_switch_branch(repo, branch_name)) {
            kgit_die("Could not switch to workspace branch", -1);
        }
    }

    if (has_upstream && kgit_current_branch_has_upstream(repo)) {
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
        goto cleanup;
    }

    error = git_reference_peel(&target, branch, GIT_OBJECT_COMMIT);
    if (error < 0) {
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
        kgit_die(errmsg, error);
    }
    return ret;
}

void kgit_squash_branch_into_current(
    git_repository* repo,
    const char* source_branch,
    const char* message
) {
    git_reference* head_ref = NULL;
    git_reference* source_ref = NULL;

    git_commit* head_commit = NULL;
    git_commit* source_commit = NULL;
    git_commit* base_commit = NULL;

    git_tree* head_tree = NULL;
    git_tree* source_tree = NULL;
    git_tree* base_tree = NULL;
    git_tree* result_tree = NULL;

    git_index* index = NULL;
    git_signature* sig = NULL;

    git_oid base_oid;
    git_oid tree_oid;
    git_oid commit_oid;

    char* errmsg = NULL;

    int32_t error = git_repository_head(&head_ref, repo);
    if (error < 0) {
        errmsg = "Failed to read HEAD";
        goto cleanup;
    }

    const git_oid* head_oid = git_reference_target(head_ref);
    if (!head_oid) {
        errmsg = "HEAD has no target";
        error = -1;
        goto cleanup;
    }

    error = git_commit_lookup(&head_commit, repo, head_oid);
    if (error < 0) {
        errmsg = "Failed to lookup HEAD commit";
        goto cleanup;
    }

    error = git_branch_lookup(
        &source_ref,
        repo,
        source_branch,
        GIT_BRANCH_LOCAL
    );
    if (error < 0) {
        errmsg = "Failed to find source branch";
        goto cleanup;
    }

    const git_oid* source_oid = git_reference_target(source_ref);
    if (!source_oid) {
        errmsg = "Source branch has no target";
        error = -1;
        goto cleanup;
    }

    error = git_commit_lookup(&source_commit, repo, source_oid);
    if (error < 0) {
        errmsg = "Failed to lookup source commit";
        goto cleanup;
    }

    error = git_merge_base(&base_oid, repo, head_oid, source_oid);
    if (error < 0) {
        errmsg = "Failed to find merge base";
        goto cleanup;
    }

    error = git_commit_lookup(&base_commit, repo, &base_oid);
    if (error < 0) {
        errmsg = "Failed to lookup merge base commit";
        goto cleanup;
    }

    error = git_commit_tree(&head_tree, head_commit);
    if (error < 0) {
        errmsg = "Failed to get HEAD tree";
        goto cleanup;
    }

    error = git_commit_tree(&source_tree, source_commit);
    if (error < 0) {
        errmsg = "Failed to get source tree";
        goto cleanup;
    }

    error = git_commit_tree(&base_tree, base_commit);
    if (error < 0) {
        errmsg = "Failed to get base tree";
        goto cleanup;
    }

    git_merge_options merge_opts = GIT_MERGE_OPTIONS_INIT;

    error = git_merge_trees(
        &index,
        repo,
        base_tree,
        head_tree,
        source_tree,
        &merge_opts
    );
    if (error < 0) {
        errmsg = "Failed to squash-merge trees";
        goto cleanup;
    }

    if (git_index_has_conflicts(index)) {
        errmsg = "Squash merge has conflicts; resolve manually";
        error = -1;
        goto cleanup;
    }

    git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
    checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE |
        GIT_CHECKOUT_RECREATE_MISSING;

    error = git_checkout_index(repo, index, &checkout_opts);
    if (error < 0) {
        errmsg = "Failed to checkout squash result";
        goto cleanup;
    }

    error = git_index_write_tree_to(&tree_oid, index, repo);
    if (error < 0) {
        errmsg = "Failed to write squash tree";
        goto cleanup;
    }

    error = git_tree_lookup(&result_tree, repo, &tree_oid);
    if (error < 0) {
        errmsg = "Failed to lookup squash tree";
        goto cleanup;
    }

    kgit_signature(&sig);

    const git_commit* parents[] = {
        head_commit,
    };

    error = git_commit_create(
        &commit_oid,
        repo,
        "HEAD",
        sig,
        sig,
        "UTF-8",
        message,
        result_tree,
        1,
        parents
    );
    if (error < 0) {
        errmsg = "Failed to create squash commit";
    }

cleanup:
    if (sig) {
        git_signature_free(sig);
    }
    if (result_tree) {
        git_tree_free(result_tree);
    }
    if (base_tree) {
        git_tree_free(base_tree);
    }
    if (source_tree) {
        git_tree_free(source_tree);
    }
    if (head_tree) {
        git_tree_free(head_tree);
    }
    if (index) {
        git_index_free(index);
    }
    if (base_commit) {
        git_commit_free(base_commit);
    }
    if (source_commit) {
        git_commit_free(source_commit);
    }
    if (head_commit) {
        git_commit_free(head_commit);
    }
    if (source_ref) {
        git_reference_free(source_ref);
    }
    if (head_ref) {
        git_reference_free(head_ref);
    }
    if (errmsg) {
        kgit_die(errmsg, error);
    }
}
