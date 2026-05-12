#ifndef KBASE_GIT_BRIDGE_H
#define KBASE_GIT_BRIDGE_H

#include <stdbool.h>

#include <git2/repository.h>
#include <git2/checkout.h>

void git_init(void);
void git_free(void);

git_repository* git_find_repo(char* cwd);
void git_sync_repo(git_repository* repo, git_indexer_progress_cb transfer_progress_cb, git_checkout_progress_cb checkout_progress_cb, git_checkout_notify_cb notify_cb);
git_repository* git_clone_repo(char* url, const char* cwd,  git_indexer_progress_cb transfer_progress_cb, git_checkout_progress_cb checkout_progress_cb);

git_reference* git_new_branch(git_repository* repo, char* name);
bool git_is_branch(git_repository* repo, char* branch_name);
bool git_switch_branch(git_repository* repo, const char* name);

bool git_has_new_changes(git_repository* repo);
void git_commit_all(git_repository* repo, char* msg);

#endif // KBASE_GIT_BRIDGE_H
