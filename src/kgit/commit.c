#include <git2.h>

#include "kgit/common.h"

#include "kgit/commit.h"

void kgit_commit_all(git_repository* repo, char* msg) {
    git_index* index = NULL;
    git_reference* head = NULL;
    git_tree* tree = NULL;
    git_commit* parent = NULL;

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

    git_signature* me = NULL;
    kgit_signature(&me);

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
    if (me) {
        git_signature_free(me);
    }
    if (errmsg) {
        kgit_die(errmsg, error);
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
