#include <stddef.h>
#include <string.h>

#include "cmd.h"

#include "commands/daemon.h"
#include "commands/grep.h"
#include "commands/help.h"
#include "commands/sync.h"

cmd_t cmds[] = {
    { "grep", cmd_grep },
    { "sync", cmd_sync },
    { "daemon", cmd_daemon },
    { "help", cmd_help },
};

void call_cmd(int32_t argc, char** argv) {
    if (argc < 2) {
        cmd_help(argc, argv);
        return;
    }

    for (size_t i = 0; i < CMD_SIZE(cmds); i++) {
        if (strcmp(cmds[i].name, argv[1]) == 0) {
            cmds[i].fptr(argc, argv);
            return;
        }
    }
}
