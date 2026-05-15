#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "commands/sync.h"

#include "kgit/git_bridge.h"
#include "ui/pbar.h"

#define MAX_PATH_NAME 4096

static char cwd[MAX_PATH_NAME] = { 0 };
static uint8_t checkout_done = 0;
static transfer_state_t transfer_state = RECEIVE;

static int transfer_progress(const git_indexer_progress* progress, void* data) {
    switch (transfer_state) {
        case RECEIVE:
            progress_bar(
                "Receiving objects",
                progress->received_objects,
                progress->total_objects
            );
            if (progress->received_objects >= progress->total_objects &&
                progress->received_objects > 0) {
                ++transfer_state;
            }
            break;

        case INDEX:
            progress_bar(
                "Indexing objects",
                progress->indexed_objects,
                progress->total_objects
            );
            if (progress->indexed_objects >= progress->total_objects &&
                progress->total_objects > 0) {
                ++transfer_state;
            }
            break;

        case RESOLVE:
            progress_bar(
                "Resolving deltas",
                progress->indexed_deltas,
                progress->total_deltas
            );
            if (progress->indexed_deltas >= progress->total_deltas &&
                progress->total_deltas > 0) {
                ++transfer_state;
            }
            break;

        case DONE:
            return 0;
    }
    return 0;
}

static void checkout_progress(
    const char* path,
    size_t cur,
    size_t tot,
    void* payload
) {
    if (checkout_done) {
        return;
    }

    progress_bar("Checking out files", cur, tot);
    if (cur >= tot) {
        checkout_done = 1;
    }
}

static int notify_cb(
    git_checkout_notify_t reason,
    const char* path,
    const git_diff_file* baseline,
    const git_diff_file* target,
    const git_diff_file* workdir,
    void* payload
) {
    fprintf(stderr, "checkout notify: reason=%d path=%s\n", reason, path);
    fflush(stderr);
    return 0;
}

void cmd_sync(int32_t argc, char** argv) {
    kgit_init();
    git_repository* repo = NULL;

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd() error");
        goto error;
    }

    checkout_done = 0;
    transfer_state = RECEIVE;
    int32_t opt = getopt(argc, argv, "n:");
    if (opt != -1) {
        printf("Creating new knowledge base...\n");
        repo = kgit_clone_repo(
            optarg,
            cwd,
            transfer_progress,
            checkout_progress
        );
        goto cleanup;
    }

    printf("Syncing knowledge base...\n");
    repo = kgit_find_repo(cwd);
    kgit_sync_repo(repo, transfer_progress, checkout_progress, notify_cb);

cleanup:
    git_repository_free(repo);
    kgit_free();
    return;

error:
    kgit_free();
    exit(1);
}
