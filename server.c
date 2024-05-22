#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <jpeglib.h>
#include <jerror.h>

#define BUFFER_SIZE 4096
#define PORT 12345
#define END_SIGNAL "done"

#pragma pack(push, 1)
typedef struct {
    unsigned short type;
    unsigned int size;
    unsigned short reserved1, reserved2;
    unsigned int offset;
} BITMAPFILEHEADER;

typedef struct {
    unsigned int size;
    int width, height;
    unsigned short planes;
    unsigned short bitCount;
    unsigned int compression;
    unsigned int sizeImage;
    int xPelsPerMeter, yPelsPerMeter;
    unsigned int clrUsed;
    unsigned int clrImportant;
} BITMAPINFOHEADER;
#pragma pack(pop)

// Function to invert colors for 32-bit BMP images
void invertColors32(unsigned char* img, unsigned int size) {
    for (unsigned int i = 0; i < size; i += 4) { // Each pixel: B, G, R, A
        img[i] = 255 - img[i];       // Blue
        img[i+1] = 255 - img[i+1];   // Green
        img[i+2] = 255 - img[i+2];   // Red
        // Alpha channel (img[i+3]) is not modified
    }
}

// Function to invert colors for JPEG images
void invert_colors(unsigned char *img, unsigned long size) {
    for (unsigned long i = 0; i < size; i++) {
        img[i] = 255 - img[i]; // Invert color
    }
}

// Function to handle BMP files
void handle_bmp(int client_socket, FILE *file) {
    BITMAPFILEHEADER bfHeader;
    BITMAPINFOHEADER biHeader;

    fread(&bfHeader, sizeof(BITMAPFILEHEADER), 1, file);
    fread(&biHeader, sizeof(BITMAPINFOHEADER), 1, file);

    if (bfHeader.type != 0x4D42 || biHeader.bitCount != 32) {
        perror("Unsupported BMP file format or bit depth (only 32-bit supported)");
        return;
    }

    fseek(file, bfHeader.offset, SEEK_SET);
    unsigned int imgSize = biHeader.sizeImage == 0 ? (biHeader.width * 4 * abs(biHeader.height)) : biHeader.sizeImage;
    unsigned char* imgData = (unsigned char*)malloc(imgSize);

    fread(imgData, imgSize, 1, file);
    invertColors32(imgData, imgSize);

    send(client_socket, imgData, imgSize, 0);
    free(imgData);
}

// Function to handle JPEG files
void handle_jpeg(int client_socket, FILE *file) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    printf("%s \n", "Handling jpeg");

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, file);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    int width = cinfo.output_width;
    int height = cinfo.output_height;
    int pixel_size = cinfo.output_components;

    unsigned long img_size = width * height * pixel_size;
    unsigned char *img_data = (unsigned char *)malloc(img_size);

    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char *buffer_array[1];
        buffer_array[0] = img_data + (cinfo.output_scanline) * width * pixel_size;
        jpeg_read_scanlines(&cinfo, buffer_array, 1);
    }

    invert_colors(img_data, img_size);

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    // Compress the image back to JPEG format
    struct jpeg_compress_struct cinfo_out;
    struct jpeg_error_mgr jerr_out;
    cinfo_out.err = jpeg_std_error(&jerr_out);
    jpeg_create_compress(&cinfo_out);

    unsigned char *jpeg_buffer = NULL;
    unsigned long jpeg_size = 0;
    jpeg_mem_dest(&cinfo_out, &jpeg_buffer, &jpeg_size);

    cinfo_out.image_width = width;
    cinfo_out.image_height = height;
    cinfo_out.input_components = pixel_size;
    cinfo_out.in_color_space = cinfo.out_color_space;

    jpeg_set_defaults(&cinfo_out);
    jpeg_set_quality(&cinfo_out, 90, TRUE);  // Set quality to 90 to reduce data loss
    jpeg_start_compress(&cinfo_out, TRUE);

    while (cinfo_out.next_scanline < cinfo_out.image_height) {
        unsigned char *buffer_array[1];
        buffer_array[0] = img_data + (cinfo_out.next_scanline) * width * pixel_size;
        jpeg_write_scanlines(&cinfo_out, buffer_array, 1);
    }

    jpeg_finish_compress(&cinfo_out);
    jpeg_destroy_compress(&cinfo_out);

    free(img_data);

    // Send the compressed JPEG image back to the client
    send(client_socket, jpeg_buffer, jpeg_size, 0);
    free(jpeg_buffer);
}

void *handle_client(void *arg) {
    int client_socket = *((int*)arg);
    free(arg);  // Free the socket pointer memory
    char buffer[BUFFER_SIZE];
    int bytes_read;

    // Create a temporary file to store received data
    FILE *file = tmpfile();
    if (!file) {
        perror("Failed to create temporary file");
        close(client_socket);
        return NULL;
    }

    long long total_bytes = 0;
    while ((bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        // Check if the end signal is in the received data
        if (bytes_read >= strlen(END_SIGNAL) && strncmp(buffer + bytes_read - strlen(END_SIGNAL), END_SIGNAL, strlen(END_SIGNAL)) == 0) {
            // Write data excluding the end signal
            fwrite(buffer, 1, bytes_read - strlen(END_SIGNAL), file);
            total_bytes += bytes_read - strlen(END_SIGNAL);
            break;
        }
        fwrite(buffer, 1, bytes_read, file);
        total_bytes += bytes_read;
    }

    printf("\n\nTotal bytes: %lld\n\n", total_bytes);
    fseek(file, 0, SEEK_SET);

    unsigned char signature[2];
    fread(signature, 1, 2, file);
    fseek(file, 0, SEEK_SET);  // Reset file pointer

    if (signature[0] == 0x42 && signature[1] == 0x4D) {
        handle_bmp(client_socket, file);
    } else if (signature[0] == 0xFF && signature[1] == 0xD8) {
        handle_jpeg(client_socket, file);
    } else {
        const char *error_msg = "Unsupported file format";
        send(client_socket, error_msg, strlen(error_msg), 0);
    }

    fclose(file);
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
        close(server_socket);
        return 1;
    }

    if (listen(server_socket, 5) < 0) {
        perror("Failed to listen");
        close(server_socket);
        return 1;
    }

    printf("Server started on port %d... Waiting for connections...\n", PORT);

    while (1) {
        new_sock = malloc(sizeof(int));
        if (new_sock == NULL) {
            perror("Failed to allocate memory for new socket");
            continue;
        }

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
