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
    interposer_set_ipc_path(addr.sun_path);
    debug_return_int(ipc_fd);
}

const char *
interposer_get_ipc_path()
{
    return interposer_ipc_path;
}

void
interposer_set_ipc_path(const char *path)
{
    free(interposer_ipc_path);
    interposer_ipc_path = strdup(path);
}


int
interposer_client_connect(void)
{
    int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    int error = errno;
    if (fd  < 0) {
        fprintf(stderr, "Failed to create socket: %d %s\n", errno, strerror(errno));
        errno = error;
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strlcpy(addr.sun_path, interposer_ipc_path, sizeof(addr.sun_path));
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        error = errno;
        fprintf(stderr, "Failed to connect: %d %s\n", errno, strerror(errno));
        errno = error;
        return -1;
    }

    return fd;
}

static int
interposer_send(int fd, const char *traffic)
{
    if (send(fd, traffic, strlen(traffic) + 1, 0) < 0)
    {
        int error = errno;
        fprintf(stderr, "Failed to send: %d %s\n", error, strerror(error));
        errno = error;
        return -1;
    }

    return 0;
}

static void
append_string(char **strp, const char *postfix)
{
    size_t str_len = strlen(*strp);
    size_t postfix_len = strlen(postfix);

    *strp = realloc(*strp, str_len + postfix_len + 1);
    if (*strp == NULL) {
        // TODO free original
        fprintf(stderr, "Out of memory\n");
        return;
    }
    strcat(*strp, postfix);
}

static char *
construct_exec_packet(const char *command, char * const argv[], char * const env[])
{
    char *data = strdup("EXEC\n");

    append_string(&data, command);
    append_string(&data, "\n");

    for (int i = 1; argv[i] != NULL; ++i) {
        append_string(&data, argv[i]);
        append_string(&data, "\n");
    }
    append_string(&data, "\n");

    for (int i = 0; env[i] != NULL; ++i) {
        append_string(&data, env[i]);
        append_string(&data, "\n");
    }
    append_string(&data, "");
    return data;
}

int
interposer_send_exec(int fd, const char *command, char * const argv[], char * const env[])
{
    char *data = construct_exec_packet(command, argv, env);
    if (data == NULL) {
        fprintf(stderr, "Interposer: failed to construct packet\n");
        return -1;
    }

    int rc = interposer_send(fd, data);
    free(data);
    return rc;
}

static char **
read_array(FILE *fp)
{
    int result_len = 2;
    char **result = malloc(result_len * sizeof(char *)); // TODO handle OOM

    char line[8096];
    int line_num = 0;

    result[0] = NULL;
    while (fgets(line, sizeof(line), fp) && line[0] != '\0') {
        if (line_num + 2 >= result_len) {
            result = reallocarray(result, result_len * 2, sizeof(char*)); // TODO handle OOM
            result_len *= 2;
        }
        result[line_num] = strdup(line); // TODO handle OOM
        result[line_num][strlen(result[line_num]) - 1] = '\0';  // strip "\n"
        ++line_num;
        result[line_num] = NULL;
    }

    return result;
}

char **
interposer_accept_packet(int fd)
{
    int connfd = accept(fd, NULL, NULL);
    if (connfd < 0) {
        fprintf(stderr, "Failed to accept interposer connection: %d %s\r\n", errno, strerror(errno));
        return NULL;
    }

    FILE *connfp = fdopen(connfd, "r");  // TODO handle error
    if (connfp == NULL) {
        fprintf(stderr, "Failed to fdopen connection: %d %s\r\n", errno, strerror(errno));
        return NULL;
    }

    char **result = read_array(connfp);  // TODO safety for too much read
    fclose(connfp);

    return result;
}

static int
packet_len(char **packet)
{
    int count = 0;
    for (; packet[count] != NULL; ++count) {}
    return count;
}

int
interposer_unpack_exec(char ** packet, char ***argvp, char ***envp)
{
    int len = packet_len(packet) + 1;
    *argvp = calloc(len, sizeof(char *));
    *envp = calloc(len, sizeof(char *));

    // argv ends (and env starts) at the first newline

    int arg_count = 0;
    int j = 0;
    for (int i=1; packet[i] != NULL; ++i, ++j)
    {
        if (packet[i][0] == '\0') {
            ++arg_count;
            j = -1;
            continue;
        }

        char **dest = NULL;
        switch(arg_count) {
            case 0:
                dest = *argvp + j;
                *dest = packet[i];
                break;
            case 1:
                dest = *envp + j;
                *dest = packet[i];
                break;
            default:
                break;
        }
    }

    return 0;
}

void
interposer_packet_free(char **packet)
{
    if (packet == NULL) {
        return;
    }

    for (int i=0; packet[i] != NULL; ++i) {
        free(packet[i]);
        packet[i] = NULL;
    }

    free(packet);
}
