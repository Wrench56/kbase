#ifndef KBASE_CMD_H
#define KBASE_CMD_H

#include <stdint.h>

#define CMD_SIZE(arr) (sizeof(arr) / sizeof((arr[0])))

typedef void (*cmd_fn_t)(int32_t argc, char** argv);

typedef struct {
    const char* name;
    cmd_fn_t fptr;
} cmd_t;

void call_cmd(int32_t argc, char** argv);

#endif // KBASE_CMD_H
