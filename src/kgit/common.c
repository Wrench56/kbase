#include <git2.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define KBASE_SSH_FILE_ENVVAR_NAME "KBASE_SSH_PRIV_FILE"

__attribute__((noreturn)) void kgit_die(const char* msg, int32_t error) {
    const git_error* e = git_error_last();
    fprintf(
        stderr,
        "%s: %s (%d)\n",
        msg,
        e ? e->message : "unknown libgit2 error",
        error
    );
    exit(1);
}

int32_t kgit_ssh_agent_cred_cb(
    git_credential** out,
    const char* url,
    const char* username_from_url,
    uint32_t allowed_types,
    void* payload
) {
    int32_t error = 1;
    char username[256] = { 0 };
    char* password = NULL;

    printf("Authenticating for \"%s\"...\n", url);
    if (username_from_url != NULL) {
        strcpy(username, username_from_url);
    } else {
        printf("Username: ");
        if (fgets(username, sizeof(username), stdin) == NULL) {
            goto out;
        }

        username[strcspn(username, "\r\n")] = '\0';
    }

    if (allowed_types & GIT_CREDENTIAL_SSH_KEY) {
        error = git_credential_ssh_key_from_agent(out, username);
        if (error == 0) {
            return GIT_OK;
        }

        char* privpath = getenv(KBASE_SSH_FILE_ENVVAR_NAME);
        if (privpath == NULL || privpath[0] == '\0') {
            fprintf(
                stderr,
                "Error: KBASE_SSH_PRIV_FILE environment variable not set!\n"
            );
            goto out;
        }

        size_t privlen = strlen(privpath);
        char pubpath[privlen + 5];
        memcpy(pubpath, privpath, privlen);
        pubpath[privlen] = '.';
        pubpath[privlen + 1] = 'p';
        pubpath[privlen + 2] = 'u';
        pubpath[privlen + 3] = 'b';
        pubpath[privlen + 4] = '\0';

        password = getpass("Password: ");
        if (password == NULL) {
            goto out;
        }

        error = git_credential_ssh_key_new(
            out,
            username,
            pubpath,
            privpath,
            password
        );
    } else if (allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) {
        password = getpass("Password: ");
        if (password == NULL) {
            goto out;
        }

        error = git_credential_userpass_plaintext_new(out, username, password);
    } else if (allowed_types & GIT_CREDENTIAL_USERNAME) {
        error = git_credential_username_new(out, username);
    }

out:
    if (password != NULL) {
        memset(password, 0, strlen(password));
        password = NULL;
    }
    return error;
}

void kgit_init(void) {
    git_libgit2_init();
}

void kgit_free(void) {
    git_libgit2_shutdown();
}

git_repository* kgit_find_repo(char* cwd) {
    git_buf repo_name_buf = { 0 };
    int32_t error = git_repository_discover(&repo_name_buf, cwd, 1, NULL);
    if (error != 0) {
        git_buf_dispose(&repo_name_buf);
        kgit_die(
            "Error: could not discover current repository. Please enter a "
            "knowledge base before sync",
            error
        );
    }

    git_repository* repo = NULL;
    error = git_repository_open(&repo, repo_name_buf.ptr);
    git_buf_dispose(&repo_name_buf);
    if (error != 0) {
        kgit_die("Error: could not open knowledge base", error);
    }

    return repo;
}
