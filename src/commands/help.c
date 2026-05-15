#include <stdio.h>

#include "commands/help.h"

void cmd_help(int32_t argc, char** argv) {
    printf("%s\n\nCommands:\n", argv[0]);
    printf(" * help - print this message\n");
    printf(" * daemon - start autocommit daemon\n");
    printf(" * sync - sync the knowledge base\n");
    printf(" * grep - search the knowledge base\n");
    printf(" * todo - show due dates and TODO-s\n");
}
