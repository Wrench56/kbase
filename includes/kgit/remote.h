#ifndef KBASE_KGIT_REMOTE_H
#define KBASE_KGIT_REMOTE_H

#include <git2/checkout.h>
#include <git2/repository.h>
#include <stdbool.h>

void kgit_sync_repo(
    git_repository* repo,
    git_indexer_progress_cb transfer_progress_cb,
    git_checkout_progress_cb checkout_progress_cb,
    git_checkout_notify_cb notify_cb
);

git_repository* kgit_clone_repo(
    char* url,
    const char* cwd,
    git_indexer_progress_cb transfer_progress_cb,
    git_checkout_progress_cb checkout_progress_cb
);

bool git_fetch_origin(
    git_repository* repo,
    git_indexer_progress_cb transfer_progress_cb
);

#endif // KBASE_KGIT_REMOTE_H
