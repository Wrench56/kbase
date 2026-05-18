#ifndef KBASE_KGIT_COMMON_H
#define KBASE_KGIT_COMMON_H

#include <git2/credential.h>
#include <git2/repository.h>
#include <stdint.h>

void kgit_init(void);
void kgit_free(void);

git_repository* kgit_find_repo(char* cwd);
__attribute__((noreturn)) void kgit_die(const char* msg, int32_t error);
int32_t kgit_ssh_agent_cred_cb(
    git_credential** out,
    const char* url,
    const char* username_from_url,
    uint32_t allowed_types,
    void* payload
);

#endif // KBASE_KGIT_COMMON_H
