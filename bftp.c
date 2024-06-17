#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEFAULT_PORT 8889
#define MAX_CONN 10
#define BUFFER_SIZE 1024

int client_sock = -1;
pthread_mutex_t lock;

void *handle_client(void *client_socket);
void *start_server(void *port);
void handle_command(char *command);
int connect_to_peer(const char *address, int port);
void send_file(int sock, char *filename);
void receive_file(int sock, char *filename);

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    pthread_t server_thread;
    pthread_create(&server_thread, NULL, start_server, &port);
    pthread_detach(server_thread);

    char command[BUFFER_SIZE];

    while (1) {
        printf("ftp> ");
        fgets(command, BUFFER_SIZE, stdin);
        handle_command(command);
    }

    return 0;
}

void handle_command(char *command) {
    char *cmd = strtok(command, " \n");
    
    if (cmd == NULL) return;

    if (strcmp(cmd, "open") == 0) {
        char *address = strtok(NULL, " \n");
        if (address) {
            char *ip = strtok(address, ":");
            char *port_str = strtok(NULL, ":");
            int port = port_str ? atoi(port_str) : DEFAULT_PORT;
            client_sock = connect_to_peer(ip, port);
        } else {
            printf("Usage: open <address:port>\n");
        }
    } else if (strcmp(cmd, "close") == 0) {
        if (client_sock >= 0) {
            close(client_sock);
            client_sock = -1;
        }
    } else if (strcmp(cmd, "quit") == 0) {
        if (client_sock >= 0) {
            close(client_sock);
        }
        exit(0);
    } else if (strcmp(cmd, "cd") == 0) {
        char *dir = strtok(NULL, " \n");
        if (client_sock >= 0 && dir) {
            char buffer[BUFFER_SIZE];
            snprintf(buffer, BUFFER_SIZE, "cd %s\n", dir);
            send(client_sock, buffer, strlen(buffer), 0);
        } else {
            printf("Usage: cd <directory>\n");
        }
    } else if (strcmp(cmd, "get") == 0) {
        char *filename = strtok(NULL, " \n");
        if (client_sock >= 0 && filename) {
            char buffer[BUFFER_SIZE];
            snprintf(buffer, BUFFER_SIZE, "get %s\n", filename);
            send(client_sock, buffer, strlen(buffer), 0);
            receive_file(client_sock, filename);
        } else {
            printf("Usage: get <file>\n");
        }
    } else if (strcmp(cmd, "lcd") == 0) {
        char *dir = strtok(NULL, " \n");
        if (dir) {
            if (chdir(dir) == 0) {
                printf("Local directory changed to %s\n", dir);
            } else {
                perror("lcd");
            }
        } else {
            printf("Usage: lcd <directory>\n");
        }
    } else if (strcmp(cmd, "ls") == 0) {
        if (client_sock >= 0) {
            char buffer[BUFFER_SIZE];
            snprintf(buffer, BUFFER_SIZE, "ls\n");
            send(client_sock, buffer, strlen(buffer), 0);
        }
    } else if (strcmp(cmd, "put") == 0) {
        char *filename = strtok(NULL, " \n");
        if (client_sock >= 0 && filename) {
            char buffer[BUFFER_SIZE];
            snprintf(buffer, BUFFER_SIZE, "put %s\n", filename);
            send(client_sock, buffer, strlen(buffer), 0);
            send_file(client_sock, filename);
        } else {
            printf("Usage: put <file>\n");
        }
    } else if (strcmp(cmd, "pwd") == 0) {
        if (client_sock >= 0) {
            char buffer[BUFFER_SIZE];
            snprintf(buffer, BUFFER_SIZE, "pwd\n");
            send(client_sock, buffer, strlen(buffer), 0);
        }
    } else {
        printf("Unknown command: %s\n", cmd);
    }
}

void *start_server(void *port_ptr) {
    int port = *((int *)port_ptr);
    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CONN) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening on port %d\n", port);

    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        pthread_t client_thread;
        int *new_sock = malloc(1);
        *new_sock = client_socket;
        pthread_create(&client_thread, NULL, handle_client, (void *)new_sock);
        pthread_detach(client_thread);
    }

    close(server_fd);
    return NULL;
}

void *handle_client(void *client_socket) {
    int sock = *(int *)client_socket;
    free(client_socket);
    char buffer[BUFFER_SIZE];
    int read_size;

    while ((read_size = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[read_size] = '\0';
        printf("Received: %s", buffer);

        char *cmd = strtok(buffer, " \n");
        if (cmd == NULL) continue;

        if (strcmp(cmd, "cd") == 0) {
            char *dir = strtok(NULL, " \n");
            if (dir) {
                if (chdir(dir) == 0) {
                    send(sock, "Directory changed\n", 18, 0);
                } else {
                    perror("cd");
                    send(sock, "Failed to change directory\n", 27, 0);
                }
            }
        } else if (strcmp(cmd, "get") == 0) {
            char *filename = strtok(NULL, " \n");
            if (filename) {
                send_file(sock, filename);
            }
        } else if (strcmp(cmd, "ls") == 0) {
            DIR *d;
            struct dirent *dir;
            d = opendir(".");
            if (d) {
                while ((dir = readdir(d)) != NULL) {
                    send(sock, dir->d_name, strlen(dir->d_name), 0);
                    send(sock, "\n", 1, 0);
                }
                closedir(d);
            }
        } else if (strcmp(cmd, "put") == 0) {
            char *filename = strtok(NULL, " \n");
            if (filename) {
                receive_file(sock, filename);
            }
        } else if (strcmp(cmd, "pwd") == 0) {
            char cwd[BUFFER_SIZE];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                send(sock, cwd, strlen(cwd), 0);
                send(sock, "\n", 1, 0);
            } else {
                perror("pwd");
            }
        }
    }

    close(sock);
    return NULL;
}

int connect_to_peer(const char *address, int port) {
    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, address, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    printf("Connected to peer at %s:%d\n", address, port);
    return sock;
}

void send_file(int sock, char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("File open error");
        return;
    }

    char buffer[BUFFER_SIZE];
    int n;
    while ((n = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0) {
        if (send(sock, buffer, n, 0) == -1) {
            perror("Send error");
            fclose(fp);
            return;
        }
    }

    fclose(fp);
    send(sock, "EOF", 3, 0); // Send end of file indicator
}

void receive_file(int sock, char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (fp == NULL) {
        perror("File open error");
        return;
    }

    char buffer[BUFFER_SIZE];
    int n;
    while ((n = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        if (strncmp(buffer, "EOF", 3) == 0) break;
        if (fwrite(buffer, sizeof(char), n, fp) != n) {
            perror("Write error");
            fclose(fp);
            return;
        }
    }

    fclose(fp);
}

