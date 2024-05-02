#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define BUFFER_SIZE 4096
#define PORT 12345
#define END_SIGNAL "END_OF_FILE"

void caesar_cipher(char *buffer, int size) {
    for (int i = 0; i < size; i++) {
        if (buffer[i] >= 'a' && buffer[i] <= 'z') {
            buffer[i] = 'a' + (buffer[i] - 'a' + 3) % 26;
        } else if (buffer[i] >= 'A' && buffer[i] <= 'Z') {
            buffer[i] = 'A' + (buffer[i] - 'A' + 3) % 26;
        }
    }
}

void *handle_client(void *arg) {
    int client_socket = *((int*)arg);
    free(arg);  // Free the socket pointer memory
    char buffer[BUFFER_SIZE];
    int bytes_read, bytes_sent;

    while ((bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        printf("Received %d bytes of data: %.*s\n", bytes_read, bytes_read, buffer);
        caesar_cipher(buffer, bytes_read);
        printf("Processed data: %.*s\n", bytes_read, buffer);
        bytes_sent = send(client_socket, buffer, bytes_read, 0);
        if (bytes_sent < 0) {
            perror("Failed to send data");
            break;
        }
        printf("Data sent (%d bytes): %.*s\n", bytes_sent, bytes_sent, buffer);
    }

    send(client_socket, END_SIGNAL, strlen(END_SIGNAL), 0);
    printf("END_OF_FILE sent.\n");

    if (bytes_read < 0) {
        perror("Failed to receive data");
    }

    close(client_socket);
    printf("Client connection closed.\n");
    return NULL;
}

int main() {
    int server_socket, *new_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t thread_id;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Failed to create socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to bind");
        return 1;
    }

    listen(server_socket, 5);
    printf("Server started on port %d... Waiting for connections...\n", PORT);

    while (1) {
        new_sock = malloc(sizeof(int));
        *new_sock = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (*new_sock < 0) {
            perror("Failed to accept client");
            free(new_sock);
            continue;
        }

        printf("Client connected from %s:%d...\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            perror("Could not create thread");
            free(new_sock);
        }
        pthread_detach(thread_id);
    }

    close(server_socket);
    return 0;
}
