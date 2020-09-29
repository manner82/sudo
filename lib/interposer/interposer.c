#include "config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <dlfcn.h>
#include <stdio.h>

sudo_dso_public int
execve(const char *command, char * const argv[], char * const envp[])
{
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
