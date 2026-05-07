#include <stddef.h>
#include <string.h>

#include "cmd.h"

#include "commands/help.h"
#include "commands/sync.h"

cmd_t cmds[] = {
    {"help", cmd_help},
    {"sync", cmd_sync},
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
