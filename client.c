#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>

#define BUFFER_SIZE 4096
#define PORT 12345
#define END_SIGNAL "done"
#define END_SIGNAL_LEN 4  // Length of END_SIGNAL

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

// Function to check if the file has a valid extension
int is_valid_extension(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) {
        return 0;
    }
    if (strcmp(dot, ".jpeg") == 0 || strcmp(dot, ".jpg") == 0 || strcmp(dot, ".bmp") == 0) {
        return 1;
    }
    return 0;
}

int main() {
    int client_socket;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    FILE *file;
    struct stat path_stat;

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
        close(client_socket);
        return 1;
    }

    printf("Connected to server. Enter file paths to send (Type 'done' to finish):\n");

    char filepath[1024];
    while (1) {
        printf("Enter file path: ");
        scanf("%1023s", filepath);

        if (strcmp(filepath, "done") == 0) {
            // Send the done signal to the server
            send(client_socket, END_SIGNAL, strlen(END_SIGNAL), 0);
            break;
        }

        // Check if the path is a directory
        if (stat(filepath, &path_stat) == 0 && S_ISDIR(path_stat.st_mode)) {
            printf("Specified path is a directory, please enter a file path.\n");
            continue;  // Prompt for another file path
        }

        // Check if the file has a valid extension
        if (!is_valid_extension(filepath)) {
            printf("Invalid file type. Only .jpeg, .jpg, and .bmp files are allowed.\n");
            continue;  // Prompt for another file path
        }

        // Open file for reading
        file = fopen(filepath, "rb");
        if (file == NULL) {
            perror("Failed to open file");
            continue;  // Prompt for another file path
        }

        long long bytes_sent = 0;
        // Send the file in chunks
        while (!feof(file)) {
            size_t bytes_read = fread(buffer, 1, BUFFER_SIZE, file);
            bytes_sent += bytes_read;
            if (send(client_socket, buffer, bytes_read, 0) < 0) {
                perror("Failed to send file data");
                fclose(file);
                close(client_socket);
                return 1;
            }
        }
        send(client_socket, END_SIGNAL, strlen(END_SIGNAL), 0);
        fclose(file);
        printf("File sent successfully.\n");

        printf("\n\nBytes sent: %lld\n\n", bytes_sent);

        // Prompt user for operation code
        int operation_code;
        printf("Select operation to perform:\n");
        printf("1. Invert colors\n");
        printf("2. Rotate 180 degrees\n");
        printf("3. Convert to black/white\n");
        printf("Enter operation code: ");
        scanf("%d", &operation_code);

        while(operation_code < 1 || operation_code > 3) {
            printf("Invalid operation code. Please enter a valid operation code: \n");
            scanf("%d", &operation_code);
        }

        // Send operation code to the server
        if (send(client_socket, &operation_code, sizeof(operation_code), 0) < 0) {
            perror("Failed to send operation code");
            close(client_socket);
            return 1;
        }

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
            printf("%s %d\n", "Bytes received: ", bytes_received);
            if (strncmp(buffer, END_SIGNAL, END_SIGNAL_LEN) == 0) {
                break;
            }
            fwrite(buffer, 1, bytes_received, output_file);
        }

        fclose(output_file);
        printf("Processed file received and saved as %s.\n", output_filename);
    }

    // Close socket
    close(client_socket);
    printf("Disconnected from server.\n");

    return 0;
}
