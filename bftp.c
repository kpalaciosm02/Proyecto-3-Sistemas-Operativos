#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

void handle_open(int *sock, char *ip);
void handle_close(int *sock);
void handle_cd(int sock, char *directory);
void handle_get(int sock, char *file);
void handle_lcd(char *directory);
void handle_ls(int sock);
void handle_put(int sock, char *file);
void handle_pwd(int sock);

void handle_open(int *sock, char *ip) {
    struct sockaddr_in server_addr;

    if ((*sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(21); // Puerto FTP por defecto

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        exit(EXIT_FAILURE);
    }

    if (connect(*sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to %s\n", ip);
}

void handle_close(int *sock) {
    close(*sock);
    *sock = -1;
    printf("Connection closed\n");
}

void handle_cd(int sock, char *directory) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "cd %s\n", directory);
    send(sock, buffer, strlen(buffer), 0);
    printf("Changed remote directory to %s\n", directory);
}

void handle_get(int sock, char *file) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "get %s\n", file);
    send(sock, buffer, strlen(buffer), 0);
    
    int fd = open(file, O_WRONLY | O_CREAT, 0666);
    int bytes;
    while ((bytes = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        write(fd, buffer, bytes);
    }
    close(fd);
    printf("File %s downloaded\n", file);
}

void handle_lcd(char *directory) {
    if (chdir(directory) == 0) {
        printf("Changed local directory to %s\n", directory);
    } else {
        perror("lcd error");
    }
}

void handle_ls(int sock) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "ls\n");
    send(sock, buffer, strlen(buffer), 0);
    
    while (recv(sock, buffer, sizeof(buffer), 0) > 0) {
        printf("%s", buffer);
    }
    printf("\n");
}

void handle_put(int sock, char *file) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "put %s\n", file);
    send(sock, buffer, strlen(buffer), 0);
    
    int fd = open(file, O_RDONLY);
    int bytes;
    while ((bytes = read(fd, buffer, sizeof(buffer))) > 0) {
        send(sock, buffer, bytes, 0);
    }
    close(fd);
    printf("File %s uploaded\n", file);
}

void handle_pwd(int sock) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "pwd\n");
    send(sock, buffer, strlen(buffer), 0);
    
    recv(sock, buffer, sizeof(buffer), 0);
    printf("Remote directory: %s\n", buffer);
}

void *command_handler(void *arg) {
    int sock = -1;
    char command[1024];

    while (1) {
        printf("ftp> ");
        fgets(command, sizeof(command), stdin);

        char *cmd = strtok(command, " \n");
        if (strcmp(cmd, "open") == 0) {
            char *ip = strtok(NULL, " \n");
            handle_open(&sock, ip);
        } else if (strcmp(cmd, "close") == 0) {
            handle_close(&sock);
        } else if (strcmp(cmd, "quit") == 0) {
            if (sock != -1) {
                handle_close(&sock);
            }
            exit(0);
        } else if (strcmp(cmd, "cd") == 0) {
            char *dir = strtok(NULL, " \n");
            handle_cd(sock, dir);
        } else if (strcmp(cmd, "get") == 0) {
            char *file = strtok(NULL, " \n");
            handle_get(sock, file);
        } else if (strcmp(cmd, "lcd") == 0) {
            char *dir = strtok(NULL, " \n");
            handle_lcd(dir);
        } else if (strcmp(cmd, "ls") == 0) {
            handle_ls(sock);
        } else if (strcmp(cmd, "put") == 0) {
            char *file = strtok(NULL, " \n");
            handle_put(sock, file);
        } else if (strcmp(cmd, "pwd") == 0) {
            handle_pwd(sock);
        } else {
            printf("Unknown command: %s\n", cmd);
        }
    }

    return NULL;
}

int main() {
    pthread_t thread;

    if (pthread_create(&thread, NULL, command_handler, NULL) != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    pthread_join(thread, NULL);
    return 0;
}
