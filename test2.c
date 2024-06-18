#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dirent.h>

#define MAX_INPUT_SIZE 1024
#define BUF_SIZE 1024
#define MAX_ARG_COUNT 10
#define PORT 8889
#define BACKLOG 10
#define MAX_CLIENTS 100

void *server_thread_func(void *arg);
void *client_thread_func(void *arg);
void handle_command(char *input);
void *handle_client(void *arg);
void *handle_server(void *arg);
void cleanup();
void close_client_connection();
void list_files(int client_sock);
void change_directory(int client_sock, const char* path);
void print_working_directory(int client_sock);
void change_local_directory(const char* path);
void print_local_working_directory();
void print_client_directory();

int server_socket;
int running = 1;
pthread_t server_thread, client_thread, server_handler_thread;
pthread_t client_threads[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t quit_cond = PTHREAD_COND_INITIALIZER;
int client_socket = -1;

int main(int argc, char *argv[]) {
    int  port = PORT;
    // Create server socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    if (argc > 1) {
        port = atoi(argv[1]);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        exit(1);
    }

    if (listen(server_socket, BACKLOG) == -1) {
        perror("Listen failed");
        exit(1);
    }
    printf("Server listening on port: %d\n", port);

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

    // Join client thread
    pthread_join(client_thread, NULL);

    // Cleanup and exit
    cleanup();
    return 0;
}

void *server_thread_func(void *arg) {
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_size);

        if (client_socket == -1) {
            if (running) {
                perror("Accept failed");
            }
            continue;
        }

        printf("New connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        pthread_mutex_lock(&client_mutex);
        if (client_count < MAX_CLIENTS) {
            pthread_t client_handler_thread;
            int *client_sock_ptr = malloc(sizeof(int));
            *client_sock_ptr = client_socket;

            if (pthread_create(&client_handler_thread, NULL, handle_client, client_sock_ptr) != 0) {
                perror("Client handler thread creation failed");
                close(client_socket);
                free(client_sock_ptr);
            } else {
                client_threads[client_count++] = client_handler_thread;
            }
        } else {
            close(client_socket);
            printf("Max client limit reached. Connection closed.\n");
        }
        pthread_mutex_unlock(&client_mutex);
    }
    return NULL;
}

void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    free(arg);

    char buffer[MAX_INPUT_SIZE];
    char cwd[MAX_INPUT_SIZE];
    FILE *file = NULL;
    while (running) {
        int bytes_received = recv(client_socket, buffer, MAX_INPUT_SIZE, 0);
        if (bytes_received <= 0) {
            break;
        }
        buffer[bytes_received] = '\0';

        if (strcmp(buffer, "pwd\n") == 0) {
            // Handle the pwd command
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                //strcat(cwd, "\n");
                send(client_socket, cwd, strlen(cwd), 0);
            } else {
                char error_msg[] = "Failed to get current working directory\n";
                send(client_socket, error_msg, strlen(error_msg), 0);
            }
        } else if (strcmp(buffer, "ls") == 0) {
            // Handle the ls command
            DIR *d;
            struct dirent *dir;
            char result[MAX_INPUT_SIZE] = "";
            d = opendir(".");
            if (d) {
                while ((dir = readdir(d)) != NULL) {
                    strcat(result, dir->d_name);
                    strcat(result, "\n");
                }
                closedir(d);
                send(client_socket, result, strlen(result), 0);
            } else {
                char error_msg[] = "Failed to open directory.\n";
                send(client_socket, error_msg, strlen(error_msg), 0);
            }
        } else if (strncmp(buffer, "cd ", 3) == 0) {
            // Handle the cd command
            char *path = buffer + 3;
            path[strlen(path) - 1] = '\0'; // Remove the trailing newline character
            if (chdir(path) == 0) {
                char success_msg[MAX_INPUT_SIZE];
                snprintf(success_msg, MAX_INPUT_SIZE, "Changed directory to %s\n", path);
                send(client_socket, success_msg, strlen(success_msg), 0);
            } else {
                char error_msg[] = "Failed to change directory\n";
                send(client_socket, error_msg, strlen(error_msg), 0);
            }
        } else if (strncmp(buffer, "put ", 4) == 0) {
            char *filename = buffer + 4;
            filename[strlen(filename) - 1] = '\0'; // Remove the trailing newline character

            file = fopen(filename, "wb");
            if (file == NULL) {
                char error_msg[] = "Failed to open file for writing\n";
                send(client_socket, error_msg, strlen(error_msg), 0);
            } else {
                char success_msg[] = "Ready to receive file\n";
                send(client_socket, success_msg, strlen(success_msg), 0);

                char buffer[BUF_SIZE];
                int bytes_read;
                while ((bytes_read = recv(client_socket, buffer, BUF_SIZE, 0)) > 0) {
                    fwrite(buffer, 1, bytes_read, file);
                }
                fclose(file);
                printf("File %s received successfully.\n", filename);
            }
        } else if (strncmp(buffer, "get ", 4) == 0) {
            // Handle the get command
            char *filename = buffer + 4;
            filename[strlen(filename) - 1] = '\0'; // Remove the trailing newline character

            FILE *file = fopen(filename, "rb");
            if (file == NULL) {
                char error_msg[] = "Failed to open file for reading\n";
                send(client_socket, error_msg, strlen(error_msg), 0);
            } else {
                char success_msg[] = "Ready to send file\n";
                send(client_socket, success_msg, strlen(success_msg), 0);

                char buffer[BUF_SIZE];
                int bytes_read;
                while ((bytes_read = fread(buffer, 1, BUF_SIZE, file)) > 0) {
                    send(client_socket, buffer, bytes_read, 0);
                }
                fclose(file);
                printf("File %s sent successfully.\n", filename);
            }
        } else {
            printf("Received from client: %s\n", buffer);
        }
    }

    printf("Connection closed with client\n");
    close(client_socket);
    return NULL;
}


void *client_thread_func(void *arg) {
    char input[MAX_INPUT_SIZE];

    while (running) {
        printf("ftp> ");

        if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
            perror("fgets failed");
            exit(1);
        }

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "quit") == 0) {
            pthread_mutex_lock(&client_mutex);
            running = 0;
            pthread_cond_broadcast(&quit_cond);
            pthread_mutex_unlock(&client_mutex);
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
    char buffer[BUF_SIZE];
    char filename[MAX_INPUT_SIZE];
    while (token != NULL && arg_count < MAX_ARG_COUNT - 1) {
        args[arg_count++] = token;
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL;

    if (strcmp(args[0], "open") == 0) {
        //printf("Open command\n");
        if (arg_count != 2) {
            printf("Usage: open <hostname:port>\n");
            return;
        }
        char *hostname = strtok(args[1], ":");
        char *port_str = strtok(NULL, ":");
        if (hostname == NULL || port_str == NULL) {
            printf("Invalid format. Usage: open <hostname:port>\n");
            return;
        }
        int port = atoi(port_str);
        if (port <= 0) {
            printf("Invalid port number.\n");
            return;
        }
        // Create client socket
        client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket == -1) {
            perror("Socket creation failed");
            return;
        }
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, hostname, &server_addr.sin_addr) <= 0) {
            perror("Invalid address or address not supported");
            close(client_socket);
            client_socket = -1;
            return;
        }
        // Connect to server
        if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            perror("Connection failed");
            close(client_socket);
            client_socket = -1;
            return;
        }
        printf("Connected to %s:%d\n", hostname, port);
        // Create a thread to handle server responses
        if (pthread_create(&server_handler_thread, NULL, handle_server, &client_socket) != 0) {
            perror("Server handler thread creation failed");
            close(client_socket);
            client_socket = -1;
        }
    } else if (strcmp(args[0], "close") == 0) {
        printf("Close command\n");
        close_client_connection();
    } else if (strcmp(args[0], "cd") == 0) {
        if (client_socket != -1) {
            char cd_cmd[MAX_INPUT_SIZE];
            snprintf(cd_cmd, MAX_INPUT_SIZE, "cd %s\n", args[1]);
            write(client_socket, cd_cmd, strlen(cd_cmd));
        } else {
            printf("No active connection to send cd command.\n");
        }
    } else if (strcmp(args[0], "put") == 0) {
        if (client_socket != -1) {
        if (arg_count != 2) {
            printf("Usage: put <filename>\n");
            return;
        }
        char get_cmd[MAX_INPUT_SIZE];
        snprintf(get_cmd, MAX_INPUT_SIZE, "put %s\n", args[1]);
        write(client_socket, get_cmd, strlen(get_cmd));

        char buffer[BUF_SIZE];
        int bytes_read;
        FILE *file = fopen(args[1], "rb");
        if (file == NULL) {
            perror("Error opening file");
            return;
        }

        // Wait for the server to be ready
        if (recv(client_socket, buffer, BUF_SIZE, 0) > 0) {
            printf("Server: %s\n", buffer);

            // Send the file data
            while ((bytes_read = fread(buffer, 1, BUF_SIZE, file)) > 0) {
                write(client_socket, buffer, bytes_read);
            }
        }

        fclose(file);
        } else {
            printf("No active connection to send put command.\n");
        }
    } else if (strcmp(args[0], "lcd") == 0) {
        change_local_directory(args[1]);
    } else if (strcmp(args[0], "ls") == 0) {
        if (client_socket != -1) {
            char ls_cmd[] = "ls";
            write(client_socket, ls_cmd, strlen(ls_cmd));
        } else {
            printf("No active connection to send ls command.\n");
        }
    } else if (strcmp(args[0], "get") == 0) {
        if (client_socket != -1) {
            if (arg_count != 2) {
                printf("Usage: get <filename>\n");
                return;
            }
            char get_cmd[MAX_INPUT_SIZE];
            snprintf(get_cmd, MAX_INPUT_SIZE, "get %s\n", args[1]);
            write(client_socket, get_cmd, strlen(get_cmd));

            char buffer[BUF_SIZE];
            int bytes_received;
            FILE *file = fopen(args[1], "wb");
            if (file == NULL) {
                perror("Error opening file for writing");
                return;
            }

            // Wait for the server to be ready
            if (recv(client_socket, buffer, BUF_SIZE, 0) > 0) {
                printf("Server: %s\n", buffer);

                // Receive the file data
                while ((bytes_received = recv(client_socket, buffer, BUF_SIZE, 0)) > 0) {
                    fwrite(buffer, 1, bytes_received, file);
                }
            }

            fclose(file);
            printf("File %s received successfully.\n", args[1]);
        } else {
            printf("No active connection to send get command.\n");
        }
    } else if (strcmp(args[0], "pwd") == 0) {
        if (client_socket != -1) {
            char pwd_cmd[] = "pwd\n";
            write(client_socket, pwd_cmd, strlen(pwd_cmd));
        } else {
            printf("No active connection to send pwd command.\n");
        }
    } else if (strcmp(args[0], "lpwd") == 0) {
        print_local_working_directory();
    }else {
        printf("Invalid command. Try again.\n");
    }
}

void *handle_server(void *arg) {
    int sock = *((int *)arg);
    char buffer[MAX_INPUT_SIZE];
    FILE *file = NULL;
    char filename[MAX_INPUT_SIZE];
    memset(filename, 0, sizeof(filename));
    int receiving_file = 0;
    while (running) {
        int bytes_received = recv(sock, buffer, MAX_INPUT_SIZE, 0);
        if (bytes_received <= 0) {
            break;
        }
        buffer[bytes_received] = '\0';

        if (strncmp(buffer, "put ", 4) == 0) {
            // Handle the put command
            receiving_file = 1;
            char *file_name = buffer + 4;
            file_name[strlen(file_name) - 1] = '\0'; // Remove the trailing newline character
            strcpy(filename, file_name);
            file = fopen(filename, "wb");
            if (file == NULL) {
                perror("Error opening file");
                receiving_file = 0;
            } else {
                printf("Receiving file: %s\n", filename);
            }
        } else if (receiving_file) {
            // Receiving file data
            if (file != NULL) {
                fwrite(buffer, 1, bytes_received, file);
            } else {
                fprintf(stderr, "Error: file not open for writing\n");
                receiving_file = 0;
            }
        } else if (strcmp(buffer, "ls") == 0) {
            // Handle the ls command
            DIR *d;
            struct dirent *dir;
            char result[MAX_INPUT_SIZE] = "";
            d = opendir(".");
            if (d) {
                while ((dir = readdir(d)) != NULL) {
                    strcat(result, dir->d_name);
                    strcat(result, "\n");
                }
                closedir(d);
                printf("Handle server print: %s\n",result);
                send(sock, result, strlen(result), 0);
            } else {
                char error_msg[] = "Failed to open directory.\n";
                send(sock, error_msg, strlen(error_msg), 0);
            }
        } else if (strcmp(buffer, "pwd") == 0){
            char cwd[MAX_INPUT_SIZE];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                strcat(cwd, "\n");
                send(sock, cwd, strlen(cwd), 0);
            } else {
                char error_msg[] = "Failed to get current working directory\n";
                send(sock, error_msg, strlen(error_msg), 0);
            }
        } else {
            printf("Client: %s\n", buffer);
        }
    }
    close(sock);
    return NULL;
}

void cleanup() {
    // Signal the server thread to stop accepting new connections
    close(server_socket);

    // Wait for all client threads to finish
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < client_count; i++) {
        pthread_cancel(client_threads[i]);
        pthread_join(client_threads[i], NULL);
    }
    pthread_mutex_unlock(&client_mutex);

    // Cancel and join the server thread
    pthread_cancel(server_thread);
    pthread_join(server_thread, NULL);

    // Close the client connection if open
    close_client_connection();
}


void close_client_connection() {
    if (client_socket != -1) {
        close(client_socket);
        client_socket = -1;
        pthread_cancel(server_handler_thread);
        pthread_join(server_handler_thread, NULL);
        printf("Connection closed.\n");
    } else {
        printf("No active connection to close.\n");
    }
}

void list_files(int client_sock) {
    DIR *d;
    struct dirent *dir;
    char buffer[BUF_SIZE] = "";
    d = opendir(".");

    if (d) {
        while ((dir = readdir(d)) != NULL) {
            strcat(buffer, dir->d_name);
            strcat(buffer, "\n");
        }
        closedir(d);
    } else {
        strcpy(buffer, "Failed to open directory.\n");
    }

    write(client_sock, buffer, strlen(buffer));
}

void change_directory(int client_sock, const char* path) {
    char buffer[BUF_SIZE];
    printf("Shown path: %s\n",path);
    if (chdir(path) == 0) {
        snprintf(buffer, sizeof(buffer), "Changed directory to %s\n", path);
    } else {
        snprintf(buffer, sizeof(buffer), "Failed to change directory to %s\n", path);
    }
    write(client_sock, buffer, strlen(buffer));
}

void print_working_directory(int client_sock) {
    char cwd[BUF_SIZE];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        strcat(cwd, "\n");
        write(client_sock, cwd, strlen(cwd));
    } else {
        perror("getcwd() error");
        char error_msg[] = "Failed to get current working directory\n";
        write(client_sock, error_msg, strlen(error_msg));
    }
}

void print_local_working_directory() {
    char cwd[BUF_SIZE];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        strcat(cwd, "\n");
        printf("%s",cwd);
    } else {
        perror("getcwd() error");
        char error_msg[] = "Failed to get current working directory\n";
        printf("%s",error_msg);
    }
}

void change_local_directory(const char* path) {
    char buffer[BUF_SIZE];
    if (chdir(path) == 0) {
        snprintf(buffer, sizeof(buffer), "Local directory changed to %s\n", path);
    } else {
        snprintf(buffer, sizeof(buffer), "Failed to change local directory to %s\n", path);
    }
    printf("%s", buffer);
}

void print_client_directory() {
    char cwd[BUF_SIZE];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("Local directory: %s\n", cwd);
    } else {
        perror("getcwd() error");
    }
}