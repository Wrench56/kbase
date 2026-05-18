#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "kgit/branch.h"
#include "kgit/common.h"
#include "kgit/remote.h"
#include "ui/pbar.h"

#include "commands/add.h"

#define MAX_PATH_NAME 4096

static void get_username(char* username, size_t maxsize) {
    uid_t uid = getuid();
    struct passwd* pw = getpwuid(uid);

    strncpy(username, pw->pw_name, maxsize);
}

void cmd_add(int32_t argc, char** argv) {
    char cwd[MAX_PATH_NAME] = { 0 };
    char workspace_b[1024] = { 0 };
    char commitmsg[4096] = { 0 };
    char username[256] = { 0 };

    if (argc < 3) {
        fprintf(stderr, "Error: usage of command add: kbase add \"comment\"\n");
        exit(1);
    }

    kgit_init();
    git_repository* repo = NULL;

    get_username(username, sizeof(username));
    getcwd(cwd, sizeof(cwd));

    printf("Merging worktree into knowledge base...\n");
    repo = kgit_find_repo(cwd);

    git_fetch_origin(repo, NULL);
    if (!kgit_switch_branch(repo, "main")) {
        git_repository_free(repo);
        fprintf(stderr, "Error: could not switch to main\n");
        goto error;
    }

    snprintf(workspace_b, sizeof(workspace_b), "worktree/%s", username);
    kgit_fast_forward_current_branch(repo, NULL, NULL);
    kgit_squash_branch_into_current(repo, workspace_b, argv[2]);
    kgit_push_branch(repo, "main");

cleanup:
    git_repository_free(repo);
    kgit_free();
    return;

error:
    kgit_free();
    exit(1);
}
