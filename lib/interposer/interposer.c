#include "config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <dlfcn.h>
#include <stdlib.h>

#include "config.h"
#include "sudo_compat.h"
#include "sudo_debug.h"

#include "interposer_ipc.h"


__attribute__((constructor)) void
interposer_init(void)
{
    const char *ipc_path = getenv("SUDO_IPC_PATH");
    if (ipc_path == NULL || *ipc_path == '\0') {
        fprintf(stderr, "Failed to get SUDO_IPC_PATH\n");
        exit(129); // TODO
    }
    interposer_set_ipc_path(ipc_path);
}

static int
original_execve(const char *command, char * const argv[], char * const envp[])
{
    int (*execve_fn)(const char *, char *const *, char *const *);

    /* Find the "next" execve function in the dynamic chain. */
    execve_fn = dlsym(RTLD_NEXT, "execve");
    if (execve_fn == NULL) {
        return -1; /* should not happen */
    }

    // fprintf(stderr, "[INTERPOSER] -> running command: %s\n", command);

    /* Execute the command using the "real" execve function. */
    return (*execve_fn)(command, argv, envp);
}

sudo_dso_public int
execve(const char *command, char * const argv[], char * const envp[])
{
    int fd = interposer_client_connect();
    if (fd < 0)
        return -1;

    if (interposer_send_exec(fd, command, argv, envp) != 0) {
        close(fd);
        return -1;
    }

    close(fd);

    return original_execve(command, argv, envp);
}
