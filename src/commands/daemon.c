#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "kgit/branch.h"
#include "kgit/commit.h"
#include "kgit/common.h"

#include "commands/daemon.h"

static void get_username(char* username, size_t maxsize) {
    uid_t uid = getuid();
    struct passwd* pw = getpwuid(uid);

    strncpy(username, pw->pw_name, maxsize);
}

void cmd_daemon(int32_t argc, char** argv) {
    char username[256] = { 0 };
    char cwd[4096] = { 0 };
    char commitmsg[64] = { 0 };

    get_username(username, sizeof(username));
    getcwd(cwd, sizeof(cwd));

    char branch[512] = { 0 };
    snprintf(branch, sizeof(branch), "worktree/%s", username);

    kgit_init();
    git_repository* repo = kgit_find_repo(cwd);
    printf("Syncing workspace branch \"%s\"\n", branch);
    kgit_sync_workspace_branch(repo, branch, NULL, NULL, NULL);

    time_t curr_time;
    for (;;) {
        sleep(DAEMON_SLEEP_SECS);
        if (kgit_has_new_changes(repo)) {
            time(&curr_time);
            printf("Autosaving...\n");
            fflush(stdout);

            snprintf(
                commitmsg,
                sizeof(commitmsg),
                "Autosave on %s",
                ctime(&curr_time)
            );
            kgit_commit_all(repo, commitmsg);
            kgit_push_branch(repo, branch);
        }
    }

    kgit_free();
}
