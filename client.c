#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUFFER_SIZE 4096
#define PORT 12345
#define END_SIGNAL "END_OF_FILE"
#define END_SIGNAL_LEN 11  // Length of END_SIGNAL

// Function to generate modified file name
void generate_modified_filename(const char *input_filename, char *output_filename) {
    char *dot_position = strrchr(input_filename, '.');
    if (dot_position != NULL) {
        // Append "_modified" before the extension
        snprintf(output_filename, strlen(input_filename) + 11, "%.*s_modified%s", (int)(dot_position - input_filename), input_filename, dot_position);
    } else {
        // Append "_modified" at the end of the filename
        snprintf(output_filename, strlen(input_filename) + 11, "%s_modified", input_filename);
    }
}

int main() {
    int client_socket;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    FILE *file;

    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to server
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        return 1;
    }

    printf("Connected to server. Enter file paths to send (Type 'done' to finish):\n");

    char filepath[1024];
    while (1) {
        printf("Enter file path: ");
        scanf("%1023s", filepath);

        if (strcmp(filepath, "done") == 0) {
            break;
        }

        // Open file for reading
        file = fopen(filepath, "rb");
        if (file == NULL) {
            perror("Failed to open file");
            continue;
        }

        // Send the file in chunks
        while (!feof(file)) {
            size_t bytes_read = fread(buffer, 1, BUFFER_SIZE, file);
            if (send(client_socket, buffer, bytes_read, 0) < 0) {
                perror("Failed to send file data");
                fclose(file);
                close(client_socket);
                return 1;
            }
        }
        fclose(file);
        printf("File sent successfully.\n");

        // Receive the processed file
        char output_filename[1024];
        generate_modified_filename(filepath, output_filename);

        FILE *output_file = fopen(output_filename, "wb");
        if (output_file == NULL) {
            perror("Failed to create output file");
            continue;
        }

        printf("Receiving data...\n");
        int bytes_received;
        while ((bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
            fwrite(buffer, 1, bytes_received, output_file);
            if (bytes_received < BUFFER_SIZE) {
                // End of file reached
                break;
            }
        }

        fclose(output_file);
        printf("Processed file received and saved as %s.\n", output_filename);
    }

    // Close socket
    close(client_socket);
    printf("Disconnected from server.\n");

    return 0;
}
