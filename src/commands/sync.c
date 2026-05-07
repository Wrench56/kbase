#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#include "commands/sync.h"

#include "git/git_bridge.h"

#define MAX_PATH_NAME 4096

char cwd[4096] = { 0 };

int transfer_progress(const git_indexer_progress* progress, void* data) {
    /* TODO: Make this look cool */
    return 0;
}

void checkout_progress(const char* path, size_t cur, size_t tot, void* payload) {
    /* TODO: Make this look cool */
}

void cmd_sync(int32_t argc, char** argv) {
    git_init();
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd() error");
        goto error;

    }

    int32_t opt = getopt(argc, argv, "n:");
    if (opt != -1) {
        printf("Creating new knowledge base...\n");
        git_clone_repo(optarg, cwd, transfer_progress, checkout_progress);
        return;
    }

    git_repository* repo = git_find_repo(cwd);
    git_sync_repo(repo, &transfer_progress);

    printf("Syncing knowledge base...\n");
    git_free();
    return;

error:
    git_free();
    exit(1);
}
