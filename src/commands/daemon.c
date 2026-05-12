#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "commands/daemon.h"

#include "git/git_bridge.h"

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

    git_init();
    git_repository* repo = git_find_repo(cwd);
    printf("Syncing workspace branch \"%s\"\n", branch);
    git_sync_workspace_branch(repo, branch, NULL, NULL, NULL);

    time_t curr_time;
    for (;;) {
        sleep(1);
        printf("ref\n");
        fflush(stdout);
        if (git_has_new_changes(repo)) {
            time(&curr_time);
            printf("Autosaving...\n");
            fflush(stdout);

            snprintf(commitmsg, sizeof(commitmsg), "Autosave on %s", ctime(&curr_time));
            git_commit_all(repo, commitmsg);
            git_push_branch(repo, branch);
        }
    }
}
