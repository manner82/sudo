#include "config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <dlfcn.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>


static int ipc_fd = -1;


__attribute__((constructor)) void
interposer_init(void)
{
    const char *ipc_rc_str = getenv("SUDO_IPC_FD");
    if (ipc_rc_str == NULL) {
        fprintf(stderr, "Failed to get SUDO_IPC_FD\n");
        exit(129); // TODO
    }

    char *end = NULL;
    ipc_fd = strtol(ipc_rc_str, &end, 10);
    if (end == NULL || *end != '\0') {
        fprintf(stderr, "Failed to init: SUDO_IPC_FD contained invalid value '%s'\n", ipc_rc_str);
        exit(129); // TODO
    }

    fprintf(stderr, "[INTERPOSER] -> will communicate on descriptor %d\n", ipc_fd);
}


sudo_dso_public int
execve(const char *command, char * const argv[], char * const envp[])
{
    if (ipc_fd < 0) {
        fprintf(stderr, "Failed to initialize sudo interposer\n"); //
        errno = EPERM;
        return -1;
    }

    int (*execve_fn)(const char *, char *const *, char *const *);

    /* Find the "next" execve function in the dynamic chain. */
    execve_fn = dlsym(RTLD_NEXT, "execve");
    if (execve_fn == NULL) {
        return -1;              /* should not happen */
    }

    fprintf(stderr, "[INTERPOSER] -> running command: %s\n", command);

    /* Execute the command using the "real" execve function. */
    return (*execve_fn)(command, argv, envp);
}
