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

uint32_t invert_color(uint32_t color, uint32_t mask) {
    return (~color) & mask;
}

typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BITMAPFILEHEADER2;

typedef struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BITMAPINFOHEADER2;

typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BITMAPFILEHEADER;

typedef struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
    uint32_t biRedMask;
    uint32_t biGreenMask;
    uint32_t biBlueMask;
    uint32_t biAlphaMask;
} BITMAPINFOHEADER;

#pragma pack(pop)

// Function to invert colors for BMP images
void invert_bmp_colors(uint8_t *img, int size) {
    for (unsigned int i = 0; i < size; i += 4) {
        img[i] = 255 - img[i];       // Blue
        img[i+1] = 255 - img[i+1];   // Green
        img[i+2] = 255 - img[i+2];   // Red
    }
}

// Function to rotate BMP image 180 degrees
void rotate_bmp_180(uint8_t *img, int width, int height) {
    unsigned long img_size = width * height * 4; // 4 channels: BGRA
    uint8_t *temp_img = (uint8_t *)malloc(img_size);

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width * 4; j += 4) {
            int src_index = i * width * 4 + j;
            int dest_index = (height - 1 - i) * width * 4 + (width * 4 - 4 - j);

            temp_img[dest_index] = img[src_index];           // Blue
            temp_img[dest_index + 1] = img[src_index + 1];   // Green
            temp_img[dest_index + 2] = img[src_index + 2];   // Red
            temp_img[dest_index + 3] = img[src_index + 3];   // Alpha
        }
    }

    memcpy(img, temp_img, img_size);
    free(temp_img);
}


// Function to convert BMP to black/white
void convert_bmp_to_black_white(uint8_t *img, int width, int height) {
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width * 4; j += 4) {
            unsigned char gray = 0.3 * img[i * width * 4 + j + 2] +
                                 0.59 * img[i * width * 4 + j + 1] +
                                 0.11 * img[i * width * 4 + j];
            img[i * width * 4 + j] = gray;
            img[i * width * 4 + j + 1] = gray;
            img[i * width * 4 + j + 2] = gray;
        }
    }
}

// Function to invert colors for JPEG images
void invert_jpeg_colors(unsigned char *img, unsigned long size) {
    for (unsigned long i = 0; i < size; i++) {
        img[i] = 255 - img[i];
    }
}

// Function to rotate JPEG image 180 degrees
void rotate_jpeg_180(unsigned char *img, int width, int height, int channels) {
    unsigned long img_size = width * height * channels;
    unsigned char *temp_img = (unsigned char *)malloc(img_size);

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width * channels; j += channels) {
            int src_index = i * width * channels + j;
            int dest_index = (height - 1 - i) * width * channels + (width * channels - channels - j);

            for (int k = 0; k < channels; k++) {
                temp_img[dest_index + k] = img[src_index + k];
            }
        }
    }

    memcpy(img, temp_img, img_size);
    free(temp_img);
}


// Function to convert JPEG to black/white
void convert_jpeg_to_black_white(unsigned char *img, int width, int height, int channels) {
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width * channels; j += channels) {
            unsigned char gray = 0.3 * img[i * width * channels + j + 2] +
                                 0.59 * img[i * width * channels + j + 1] +
                                 0.11 * img[i * width * channels + j];
            for (int k = 0; k < channels; k++) {
                img[i * width * channels + j + k] = gray;
            }
        }
    }
}

// Process BMP image
int process_bmp(int client_socket, FILE *input_file, int operation) {
    BITMAPFILEHEADER file_header;
    fread(&file_header, sizeof(BITMAPFILEHEADER), 1, input_file);
    if (file_header.bfType != 0x4D42) {
        fprintf(stderr, "Error: Not a valid BMP file.\n");
        fclose(input_file);
        return 1;
    }
    BITMAPINFOHEADER info_header;
    fread(&info_header, sizeof(BITMAPINFOHEADER), 1, input_file);

    if (info_header.biBitCount != 32) {
        fprintf(stderr, "Error: Only 32-bit BMP files are supported.\n");
        fclose(input_file);
        return 1;
    }

    fseek(input_file, file_header.bfOffBits, SEEK_SET);
    uint8_t *pixel_data = (uint8_t *)malloc(info_header.biSizeImage);
    if (!pixel_data) {
        fprintf(stderr, "Error: Could not allocate memory for pixel data.\n");
        fclose(input_file);
        return 1;
    }
    fread(pixel_data, 1, info_header.biSizeImage, input_file);

    switch (operation) {
        case 1:
            invert_bmp_colors(pixel_data, info_header.biSizeImage);
            break;
        case 2:
            rotate_bmp_180(pixel_data, info_header.biWidth, info_header.biHeight);
            break;
        case 3:
            convert_bmp_to_black_white(pixel_data, info_header.biWidth, info_header.biHeight);
            break;
        default:
            fprintf(stderr, "Error: Invalid operation code.\n");
            free(pixel_data);
            fclose(input_file);
            return 1;
    }

    long total_size = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + info_header.biSizeImage;
    uint8_t *bmp_buffer = (uint8_t *)malloc(total_size);
    if (!bmp_buffer) {
        fprintf(stderr, "Error: Could not allocate memory for BMP buffer.\n");
        free(pixel_data);
        return 1;
    }

    memcpy(bmp_buffer, &file_header, sizeof(BITMAPFILEHEADER));
    memcpy(bmp_buffer + sizeof(BITMAPFILEHEADER), &info_header, sizeof(BITMAPINFOHEADER));
    memcpy(bmp_buffer + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), pixel_data, info_header.biSizeImage);

    if (send(client_socket, bmp_buffer, total_size, 0) == -1) {
        perror("Error sending BMP data");
        free(pixel_data);
        free(bmp_buffer);
        return 1;
    }

    free(pixel_data);
    free(bmp_buffer);
    return 0;
}

// Handle JPEG image
void handle_jpeg(int client_socket, FILE *file, int operation) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

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

    switch (operation) {
        case 1:
            invert_jpeg_colors(img_data, img_size);
            break;
        case 2:
            rotate_jpeg_180(img_data, width, height, pixel_size);
            break;
        case 3:
            convert_jpeg_to_black_white(img_data, width, height, pixel_size);
            break;
        default:
            fprintf(stderr, "Error: Invalid operation code.\n");
            free(img_data);
            fclose(file);
            return;
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

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
    jpeg_set_quality(&cinfo_out, 90, TRUE);
    jpeg_start_compress(&cinfo_out, TRUE);

    while (cinfo_out.next_scanline < cinfo_out.image_height) {
        unsigned char *buffer_array[1];
        buffer_array[0] = img_data + (cinfo_out.next_scanline) * width * pixel_size;
        jpeg_write_scanlines(&cinfo_out, buffer_array, 1);
    }

    jpeg_finish_compress(&cinfo_out);
    jpeg_destroy_compress(&cinfo_out);

    free(img_data);

    send(client_socket, jpeg_buffer, jpeg_size, 0);
    free(jpeg_buffer);
}

// Handle client connection
void *handle_client(void *arg) {
    int client_socket = *((int*)arg);
    free(arg);
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
        if (bytes_read >= strlen(END_SIGNAL) && strncmp(buffer + bytes_read - strlen(END_SIGNAL), END_SIGNAL, strlen(END_SIGNAL)) == 0) {
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

    int operation_code;
    if (recv(client_socket, &operation_code, sizeof(operation_code), 0) <= 0) {
        perror("Failed to receive operation code");
        fclose(file);
        close(client_socket);
        return NULL;
    }

    if (signature[0] == 0x42 && signature[1] == 0x4D) {
        process_bmp(client_socket, file, operation_code);
    } else if (signature[0] == 0xFF && signature[1] == 0xD8) {
        handle_jpeg(client_socket, file, operation_code);
    } else {
        const char *error_msg = "Unsupported file format";
        send(client_socket, error_msg, strlen(error_msg), 0);
    }

    fclose(file);
    close(client_socket);
    printf("Client connection closed.\n");
    return NULL;
}

// Main server function
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
