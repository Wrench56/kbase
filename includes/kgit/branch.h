#ifndef KBASE_KGIT_BRANCH_H
#define KBASE_KGIT_BRANCH_H

#include <git2/checkout.h>
#include <git2/repository.h>
#include <stdbool.h>

git_reference* kgit_new_branch(git_repository* repo, char* name);
bool kgit_is_branch(git_repository* repo, char* branch_name);
bool kgit_switch_branch(git_repository* repo, const char* name);
void kgit_push_branch(git_repository* repo, char* branch_name);
void kgit_sync_workspace_branch(
    git_repository* repo,
    const char* branch_name,
    git_indexer_progress_cb transfer_progress_cb,
    git_checkout_progress_cb checkout_progress_cb,
    git_checkout_notify_cb notify_cb
);
void kgit_fast_forward_current_branch(
    git_repository* repo,
    git_checkout_progress_cb checkout_progress_cb,
    git_checkout_notify_cb checkout_notify_cb
);
void kgit_squash_branch_into_current(
    git_repository* repo,
    const char* source_branch,
    const char* message
);

#endif // KBASE_KGIT_BRANCH_H
