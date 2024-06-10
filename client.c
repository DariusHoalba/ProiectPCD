#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>
#include <sqlite3.h>

#define BUFFER_SIZE 4096
#define PORT 12345
#define END_SIGNAL "done"
#define END_SIGNAL_LEN 4

char username[50];
char password[50];

void admin_menu(int client_socket) {
    char buffer[BUFFER_SIZE];
    int command;

    printf("Admin logged in.\n");

    while (1) {
        int isCorrect = 1;
        printf("Admin Options:\n");
        printf("1. See users\n");
        printf("2. See active users\n");
        printf("3. Add users\n");
        printf("4. Delete users\n");
        printf("5. Stats\n");
        printf("6. Shutdown server\n");
        printf("7. Uptime\n");
        printf("Type the number of the desired operation or 'done' to exit:\n");

        scanf("%s", buffer);
        if (strcmp(buffer, "done") == 0) {
            break;
        }

        char *endptr;
        command = strtol(buffer, &endptr, 10);
        if (*endptr == '\0' && command >= 1 && command <= 7) {
            // Valid command
        } else {
            isCorrect = 0;
            printf("Invalid operation code. Please enter a valid operation code (1-6) or 'done' to finish.\n");
        }

        if (isCorrect) {
            // Convert command to network byte order
            int net_command = htonl(command);
            if (send(client_socket, &net_command, sizeof(net_command), 0) == -1) {
                perror("send");
                continue;
            }

            switch (command) {
                case 1: {
                    // Receive and display list of users
                    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        printf("List of users:\n%s\n", buffer);
                    } else {
                        perror("recv");
                    }
                    break;
                }
                case 2: {
                    // Receive and display list of active users
                    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        printf("List of active users:\n%s\n", buffer);
                    } else {
                        perror("recv");
                    }
                    break;
                }
                case 3: {
                    // Send information to add a user
                    char new_username[50];
                    char new_password[50];

                    printf("Enter new username: ");
                    scanf("%s", new_username);
                    send(client_socket, new_username, strlen(new_username) + 1, 0);
                    printf("Enter new password: ");
                    scanf("%s", new_password);
                    send(client_socket, new_password, strlen(new_password) + 1, 0);

                    // Receive confirmation message
                    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        printf("%s\n", buffer);
                    } else {
                        perror("recv");
                    }
                    break;
                }
                case 4: {
                    // Send information to delete a user
                    char del_username[50];

                    printf("Enter username to delete: ");
                    scanf("%s", del_username);

                    if (send(client_socket, del_username, strlen(del_username) + 1, 0) == -1) { // +1 to include null terminator
                        perror("send");
                        continue;
                    }

                    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        printf("%s\n", buffer);
                    } else {
                        perror("recv");
                    }
                    break;
                }
                case 5:
                    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        printf("Stats:\n%s\n", buffer);
                    } else {
                        perror("recv");
                    }
                    break;
                case 6:
                    printf("Server is shutting down. You will be disconnected\n");
                    exit(0);
                    break;
                case 7:
                    int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                    if (bytes > 0) {
                        buffer[bytes] = '\0';
                        printf("Uptime: %s\n", buffer);
                    } else {
                        perror("recv");
                    }
                    break;
            }
        }
    }

    //send(client_socket, END_SIGNAL, strlen(END_SIGNAL), 0);
    close(client_socket);
    printf("Disconnected from server.\n");
    exit(0);
}


void generate_modified_filename(const char *input_filename, char *output_filename) {
    char *dot_position = strrchr(input_filename, '.');
    if (dot_position != NULL) {
        snprintf(output_filename, strlen(input_filename) + 11, "%.*s_modified%s", (int)(dot_position - input_filename), input_filename, dot_position);
    } else {
        snprintf(output_filename, strlen(input_filename) + 11, "%s_modified", input_filename);
    }
}

// Function to check if the file has a valid extension
int is_valid_extension(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) {
        return 0;
    }
    if (strcmp(dot, ".jpeg") == 0 || strcmp(dot, ".jpg") == 0 || strcmp(dot, ".bmp") == 0 || strcmp(dot, ".png") == 0) {
        return 1;
    }
    return 0;
}

void send_file(int client_socket, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (file == NULL) {
        perror("Failed to open file");
        return;
    }

    char buffer[BUFFER_SIZE];
    long long bytes_sent = 0;
    while (!feof(file)) {
        size_t bytes_read = fread(buffer, 1, BUFFER_SIZE, file);
        bytes_sent += bytes_read;
        if (send(client_socket, buffer, bytes_read, 0) < 0) {
            perror("Failed to send file data");
            fclose(file);
            close(client_socket);
            exit(1);
        }
    }
    send(client_socket, END_SIGNAL, strlen(END_SIGNAL), 0);
    fclose(file);
    printf("File %s sent successfully.\n", filepath);
}

void process_files_in_directory(int client_socket, const char *directory_path, int operation_code) {
    struct dirent *entry;
    DIR *dir = opendir(directory_path);

    if (dir == NULL) {
        perror("Failed to open directory");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", directory_path, entry->d_name);

        struct stat path_stat;
        if (stat(filepath, &path_stat) != 0 || !S_ISREG(path_stat.st_mode)) {
            continue;
        }

        if (!is_valid_extension(filepath)) {
            printf("Invalid file type for %s. Only .jpeg, .jpg, .bmp, and .png files are allowed.\n", filepath);
            continue;
        }

        send_file(client_socket, filepath);

        char confirmation[BUFFER_SIZE];
        int confirmation_len = recv(client_socket, confirmation, sizeof(confirmation) - 1, 0);
        if (confirmation_len <= 0) {
            perror("Failed to receive confirmation from server");
            break;
        }
        confirmation[confirmation_len] = '\0';
        printf("Server confirmation: %s\n", confirmation);

        if (send(client_socket, &operation_code, sizeof(operation_code), 0) < 0) {
            perror("Failed to send operation code");
            break;
        }

        char output_filename[1024];
        generate_modified_filename(filepath, output_filename);

        FILE *output_file = fopen(output_filename, "wb");
        if (output_file == NULL) {
            perror("Failed to create output file");
            continue;
        }

        char buffer[BUFFER_SIZE];
        printf("Receiving processed data for %s...\n", filepath);
        int bytes_received;
        while ((bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
            if (bytes_received >= END_SIGNAL_LEN && strncmp(buffer + bytes_received - END_SIGNAL_LEN, END_SIGNAL, END_SIGNAL_LEN) == 0) {
                fwrite(buffer, 1, bytes_received - END_SIGNAL_LEN, output_file);
                break;
            }
            fwrite(buffer, 1, bytes_received, output_file);
        }

        fclose(output_file);
        printf("Processed file received and saved as %s.\n", output_filename);
    }

    closedir(dir);
}

int main() {
    int client_socket;
    struct sockaddr_in server_addr;

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Socket creation failed");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(client_socket);
        return 1;
    }

    while (1) {
        char option;
        while (1) {
            printf("Enter 1 for login, 2 for registration: ");
            scanf(" %c", &option);

            if (option == '1' || option == '2') {
                break;
            } else {
                printf("Invalid option. Please enter 1 for login or 2 for registration.\n");
            }
        }

        send(client_socket, &option, sizeof(option), 0);

        printf("Enter username: ");
        scanf("%s", username);
        send(client_socket, username, strlen(username), 0);

        printf("Enter password: ");
        scanf("%s", password);
        send(client_socket, password, strlen(password), 0);

        if (option == '1') {
            char response[BUFFER_SIZE];
            int bytes_read = recv(client_socket, response, sizeof(response) - 1, 0);
            if (bytes_read > 0) {
                response[bytes_read] = '\0';
                if ((strstr(response, "Invalid username or password") != NULL)  ){
                    printf("Invalid username or password\n");
                    continue;
                }
                else if((strstr(response, "User already connected") != NULL)){
                    printf("User already connected\n");
                    continue;
                }
                else {
                    if (bytes_read > 0) {
                        if (strcmp(response, "You are an admin.") == 0) {
                            admin_menu(client_socket);
                        } else {
                            break;
                        }
                    }
                }
            }
        } else if (option == '2') {
            char response[BUFFER_SIZE];
            int bytes_read = recv(client_socket, response, sizeof(response) - 1, 0);
            if (bytes_read > 0) {
                response[bytes_read] = '\0';
                printf("%s\n", response);
                if (strstr(response, "Registration failed") != NULL) {
                    continue;
                } else {
                    printf("Registration successful. Please log in.\n");
                }
            }
        }
    }

    printf("Connected to server. Enter directory path to send files (Type 'done' to finish):\n");

    char directory_path[1024];
    while (1) {
        printf("Enter directory path: ");
        scanf("%1023s", directory_path);

        if (strcmp(directory_path, "done") == 0) {
            send(client_socket, END_SIGNAL, strlen(END_SIGNAL), 0);
            break;
        }

        struct stat path_stat;
        if (stat(directory_path, &path_stat) != 0 || !S_ISDIR(path_stat.st_mode)) {
            printf("Specified path is not a directory, please enter a valid directory path.\n");
            continue;
        }

        int operation_code;
        char input_buffer[10];

        while (1) {
            printf("Select operation to perform on all files in the directory:\n");
            printf("1. Invert colors\n");
            printf("2. Rotate 90 degrees\n");
            printf("3. Rotate 180 degrees\n");
            printf("4. Rotate 270 degrees\n");
            printf("5. Convert to black/white\n");
            printf("Enter operation code (or 'done' to finish): ");
            scanf("%s", input_buffer);

            if (strcmp(input_buffer, "done") == 0) {
                break;
            }

            char *endptr;
            operation_code = strtol(input_buffer, &endptr, 10);
            if (*endptr == '\0' && operation_code >= 1 && operation_code <= 5) {
                break;
            } else {
                printf("Invalid operation code. Please enter a valid operation code (1-5) or 'done' to finish.\n");
            }
        }

        if (strcmp(input_buffer, "done") == 0) {
            break;
        }

        process_files_in_directory(client_socket, directory_path, operation_code);
    }

    close(client_socket);
    printf("Disconnected from server.\n");

    return 0;
}
