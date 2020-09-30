#include "interposer_ipc.h"

#include "config.h"
#include "sudo_compat.h"
#include "sudo_debug.h"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

static char *interposer_ipc_path = NULL;

int
interposer_init_socket(void)
{
    debug_decl(interposer_init_socket, SUDO_DEBUG_UTIL);

    int ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc_fd < 0) {
        fprintf(stderr, "Failed to create socket: %d %s\n", errno, strerror(errno));
        debug_return_int(-1);
    }

    char *socket_path = strdup("/tmp/sudo_XXXXXX");
    if ((socket_path == NULL) || (mkdtemp(socket_path) == NULL)) {
        fprintf(stderr, "Failed to create temp file: %d %s\n", errno, strerror(errno));
        free(socket_path);
        debug_return_int(-1);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strlcpy(addr.sun_path, socket_path, sizeof(addr.sun_path));
    strlcat(addr.sun_path, "/socket", sizeof(addr.sun_path));

    if (bind(ipc_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to bind socket: %d %s\n", errno, strerror(errno));
        free(socket_path);
        debug_return_int(-1);
    }

    if (listen(ipc_fd, 0) < 0) {
        fprintf(stderr, "Failed to listen on socket: %d %s\n", errno, strerror(errno));
        free(socket_path);
        debug_return_int(-1);
    }

    free(socket_path);
    free(interposer_ipc_path);
    interposer_ipc_path = strdup(addr.sun_path);
    debug_return_int(ipc_fd);
}

const char *
interposer_get_ipc_path()
{
    return interposer_ipc_path;
}
