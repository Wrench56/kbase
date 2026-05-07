#ifndef KBASE_GIT_BRIDGE_H
#define KBASE_GIT_BRIDGE_H

#include <git2/repository.h>
#include <git2/checkout.h>

void git_init(void);
void git_free(void);

git_repository* git_find_repo(char* cwd);
void git_sync_repo(git_repository* repo, git_indexer_progress_cb transfer_progress_cb);
git_repository* git_clone_repo(char* url, const char* cwd,  git_indexer_progress_cb transfer_progress_cb, git_checkout_progress_cb checkout_progress_cb);

#endif // KBASE_GIT_BRIDGE_H
