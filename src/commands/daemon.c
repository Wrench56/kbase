#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include <stdio.h>

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
    get_username(username, sizeof(username));
    getcwd(cwd, sizeof(cwd));

    char branch[512] = { 0 };
    snprintf(branch, sizeof(branch), "worktree/%s", username);

    git_init();
    git_repository* repo = git_find_repo(cwd);
    if (!git_is_branch(repo, branch)) {
        printf("Switching to session branch \"%s\"...\n", branch);
        if (!git_switch_branch(repo, branch)) {
            printf("Creating non-existent session branch...\n");
            git_new_branch(repo, branch);
            git_switch_branch(repo, branch);
        }
    }

    for (;;) {
        sleep(1);
        if (git_has_new_changes(repo)) {
            printf("Autosaving...\n");
            fflush(stdout);
            git_commit_all(repo, "test");
        }
    }
}
