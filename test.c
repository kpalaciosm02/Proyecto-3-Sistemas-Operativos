#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define MAX_INPUT_SIZE 1024
#define MAX_ARG_COUNT 10
#define PORT 8889
#define BACKLOG 10

void *server_thread_func(void *arg);
void *client_thread_func(void *arg);
void handle_command(char *input);
void *handle_client(void *arg);

int server_socket;

int main() {
    pthread_t server_thread, client_thread;

    // Create server socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        exit(1);
    }

    if (listen(server_socket, BACKLOG) == -1) {
        perror("Listen failed");
        exit(1);
    }

    // Create server thread
    if (pthread_create(&server_thread, NULL, server_thread_func, NULL) != 0) {
        perror("Server thread creation failed");
        exit(1);
    }

    // Create client thread
    if (pthread_create(&client_thread, NULL, client_thread_func, NULL) != 0) {
        perror("Client thread creation failed");
        exit(1);
    }

    // Join threads
    pthread_join(server_thread, NULL);
    pthread_join(client_thread, NULL);

    close(server_socket);
    return 0;
}


void *server_thread_func(void *arg) {
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_size);

        if (client_socket == -1) {
            perror("Accept failed");
            continue;
        }

        pthread_t client_handler_thread;
        int *client_sock_ptr = malloc(sizeof(int));
        *client_sock_ptr = client_socket;

        if (pthread_create(&client_handler_thread, NULL, handle_client, client_sock_ptr) != 0) {
            perror("Client handler thread creation failed");
            close(client_socket);
            free(client_sock_ptr);
        }

        pthread_detach(client_handler_thread);
    }
    return NULL;
}

void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    free(arg);

    char buffer[MAX_INPUT_SIZE];
    while (1) {
        int bytes_received = recv(client_socket, buffer, MAX_INPUT_SIZE, 0);
        if (bytes_received <= 0) {
            break;
        }
        buffer[bytes_received] = '\0';
        printf("Received: %s\n", buffer);
    }

    close(client_socket);
    return NULL;
}

void *client_thread_func(void *arg) {
    char input[MAX_INPUT_SIZE];

    while (1) {
        printf("ftp> ");

        if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
            perror("fgets failed");
            exit(1);
        }

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "quit") == 0) {
            break;
        }

        handle_command(input);
    }

    return NULL;
}

void handle_command(char *input) {
    char *args[MAX_ARG_COUNT];
    char *token = strtok(input, " ");
    int arg_count = 0;
    while (token != NULL && arg_count < MAX_ARG_COUNT - 1) {
        args[arg_count++] = token;
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL;

    if (strcmp(args[0], "open") == 0) {
        printf("Open command\n");
    } else if (strcmp(args[0], "close") == 0) {
        printf("Close command\n");
    } else if (strcmp(args[0], "cd") == 0) {
        printf("cd command\n");
    } else if (strcmp(args[0], "get") == 0) {
        printf("Get command\n");
    } else if (strcmp(args[0], "lcd") == 0) {
        printf("lcd command\n");
    } else if (strcmp(args[0], "ls") == 0) {
        printf("ls command\n");
    } else if (strcmp(args[0], "put") == 0) {
        printf("Put command\n");
    } else if (strcmp(args[0], "pwd") == 0) {
        printf("pwd command\n");
    } else {
        printf("Invalid command. Try again.\n");
    }
}
