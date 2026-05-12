#ifndef KBASE_GIT_BRIDGE_H
#define KBASE_GIT_BRIDGE_H

#include <stdbool.h>

#include <git2/repository.h>
#include <git2/checkout.h>

void kgit_init(void);
void kgit_free(void);

git_repository* kgit_find_repo(char* cwd);
void kgit_sync_repo(git_repository* repo, git_indexer_progress_cb transfer_progress_cb, git_checkout_progress_cb checkout_progress_cb, git_checkout_notify_cb notify_cb);
git_repository* kgit_clone_repo(char* url, const char* cwd,  git_indexer_progress_cb transfer_progress_cb, git_checkout_progress_cb checkout_progress_cb);

git_reference* kgit_new_branch(git_repository* repo, char* name);
bool kgit_is_branch(git_repository* repo, char* branch_name);
bool kgit_switch_branch(git_repository* repo, const char* name);
void kgit_push_branch(git_repository* repo, char* branch_name);

bool kgit_has_new_changes(git_repository* repo);
void kgit_commit_all(git_repository* repo, char* msg);

void kgit_sync_workspace_branch(git_repository* repo, const char* branch_name, git_indexer_progress_cb transfer_progress_cb, git_checkout_progress_cb checkout_progress_cb, git_checkout_notify_cb notify_cb);

#endif // KBASE_GIT_BRIDGE_H
