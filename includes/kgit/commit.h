#ifndef KBASE_KGIT_COMMIT_H
#define KBASE_KGIT_COMMIT_H

#include <git2/repository.h>
#include <stdbool.h>

bool kgit_has_new_changes(git_repository* repo);
void kgit_commit_all(git_repository* repo, char* msg);

#endif // KBASE_KGIT_COMMIT_H
