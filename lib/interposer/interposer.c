#include "config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <dlfcn.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "config.h"
#include "sudo_compat.h"
#include "sudo_debug.h"


static char *ipc_path = NULL;


__attribute__((constructor)) void
interposer_init(void)
{
    ipc_path = getenv("SUDO_IPC_PATH");
    if (ipc_path == NULL || *ipc_path == '\0') {
        fprintf(stderr, "Failed to get SUDO_IPC_PATH\n");
        exit(129); // TODO
    }
    ipc_path = strdup(ipc_path);
    if (ipc_path == NULL) {
        fprintf(stderr, "Failed to allocate memory\n");
        exit(129); // TODO
    }

    // fprintf(stderr, "[INTERPOSER] -> will communicate on unix socket %s\n", ipc_path);
}


sudo_dso_public int
execve(const char *command, char * const argv[], char * const envp[])
{
    if (ipc_path == NULL) {
        fprintf(stderr, "Failed to initialize sudo interposer\n");
        errno = EPERM;
        return -1;
    }

    // -- communication vvv
    int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd  < 0) {
        fprintf(stderr, "Failed to create socket: %d %s\n", errno, strerror(errno));
        errno = EPERM;
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strlcpy(addr.sun_path, ipc_path, sizeof(addr.sun_path));
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        fprintf(stderr, "Failed to connect: %d %s\n", errno, strerror(errno));
        errno = EPERM;
        return -1;
    }

    if (send(fd, command, strlen(command) + 1, 0) < 0)
    {
        fprintf(stderr, "Failed to send: %d %s\n", errno, strerror(errno));
        errno = EPERM;
        return -1;
    }
    close(fd);

    // -- communication ^^^

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
