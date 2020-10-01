#include "config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

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

    int ack = 0;
    if (interposer_receive_ack(fd, &ack) != 0) {
        close(fd);
        return -1;
    }

    close(fd);

    if (ack != 1) {
        return -1;
    }

    return original_execve(command, argv, envp);
}

sudo_dso_public int
open(const char *path, int oflag, ...)
{
    int fd = interposer_client_connect();
    if (fd < 0)
        return -1;

    if (interposer_send_open(fd, path, oflag) != 0) {
        close(fd);
        return -1;
    }

    int ack = 0;
    if (interposer_receive_ack(fd, &ack) != 0) {
        close(fd);
        return -1;
    }

    close(fd);

    if (ack != 1) {
        errno = EPERM;
        return -1;
    }

    int (*open_fn)(const char *path, int oflag, ...);

    /* Find the "next" open function in the dynamic chain. */
    open_fn = dlsym(RTLD_NEXT, "open");
    if (open_fn == NULL) {
        return -1; /* should not happen */
    }

    /* Execute the command using the "real" open function. */
    if ((oflag & O_CREAT) == 0) {
        va_list extra_args;
        va_start(extra_args, oflag);
        mode_t mode = va_arg(extra_args, mode_t);
        va_end(extra_args);
        return (*open_fn)(path, oflag, mode);
    }

    return (*open_fn)(path, oflag);
}
