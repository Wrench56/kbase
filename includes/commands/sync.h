#ifndef KBASE_SYNC_H
#define KBASE_SYNC_H

#include <stdint.h>

typedef enum {
    RECEIVE = 0,
    INDEX,
    RESOLVE,
    DONE,
} transfer_state_t;

void cmd_sync(int32_t argc, char** argv);

#endif // KBASE_SYNC_H
