#include <stdio.h>

#include "help.h"

void cmd_help(int32_t argc, char** argv) {
    printf("%s\n\nCommands:\n", argv[0]);
    printf(" * help - print this message\n");
}
