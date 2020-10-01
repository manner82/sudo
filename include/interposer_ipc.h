#ifndef INTERPOSER_IPC_H
#define INTERPOSER_IPC_H

const char *interposer_get_ipc_path(void);
int interposer_init_socket(void);

// for the "client"
void interposer_set_ipc_path(const char *path);
int interposer_client_connect(void);
int interposer_send_exec(int fd, const char *command, char * const argv[], char * const env[]);
int interposer_send_open(int fd, const char *path, int oflag);

// for the "server"
char **interposer_accept_packet(int fd);
int interposer_unpack_exec(char ** packet, char ***argvp, char ***envp);
int interposer_unpack_open(char **packet, char **path, int *oflag);
void interposer_packet_free(char **packet);

#endif // INTERPOSER_IPC_H
