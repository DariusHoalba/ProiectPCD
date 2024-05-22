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


int handle_bmp(int client_socket, FILE *input_file) {
    //printf("File size in bytes: %ld\n", ftell(input_file));
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
    if (info_header.biCompression != 3) {


        fprintf(stderr, "[LOG]: BMP without BITFIELDS\n");
        BITMAPFILEHEADER2 file_header;
        fseek(input_file, 0, SEEK_SET);
        fread(&file_header, sizeof(BITMAPFILEHEADER2), 1, input_file);

        if (file_header.bfType != 0x4D42) {
            fprintf(stderr, "Error: Not a valid BMP file.\n");
            //fclose(input_file);
            return 1;
        }

        BITMAPINFOHEADER2 info_header;
        fread(&info_header, sizeof(BITMAPINFOHEADER2), 1, input_file);

        if (info_header.biBitCount != 32) {
            fprintf(stderr, "Error: Only 32-bit BMP files are supported.\n");
            //fclose(input_file);
            return 1;
        }

        fseek(input_file, file_header.bfOffBits, SEEK_SET);

        uint8_t *pixel_data = (uint8_t *)malloc(info_header.biSizeImage);
        if (!pixel_data) {
            fprintf(stderr, "Error: Could not allocate memory for pixel data.\n");
            //fclose(input_file);
            return 1;
        }


        fread(pixel_data, 1, info_header.biSizeImage, input_file);
        //fclose(input_file);

        invert_colors(pixel_data, info_header.biSizeImage);


        /// send the data to the client
        long total_size = sizeof(BITMAPFILEHEADER2) + sizeof(BITMAPINFOHEADER2) + info_header.biSizeImage;
        //printf("Total size: %ld\n", total_size);
        uint8_t *bmp_buffer = (uint8_t *)malloc(total_size);
        if (!bmp_buffer) {
            fprintf(stderr, "Error: Could not allocate memory for BMP buffer.\n");
            free(pixel_data);
            return 1;
        }
        memcpy(bmp_buffer, &file_header, sizeof(BITMAPFILEHEADER2));
        memcpy(bmp_buffer + sizeof(BITMAPFILEHEADER2), &info_header, sizeof(BITMAPINFOHEADER2));
        memcpy(bmp_buffer + sizeof(BITMAPFILEHEADER2) + sizeof(BITMAPINFOHEADER2), pixel_data, info_header.biSizeImage);

        if (send(client_socket, bmp_buffer, total_size, 0) == -1) {
            perror("Error sending BMP data");
            free(pixel_data);
            free(bmp_buffer);
            return 1;
        }

        free(pixel_data); // Free the allocated memory for pixel data
        free(bmp_buffer); // Free the allocated memory for BMP buffer



        return 0;
    }

    fseek(input_file, file_header.bfOffBits, SEEK_SET);
    uint8_t *pixel_data = (uint8_t *)malloc(info_header.biSizeImage);
    if (!pixel_data) {
        fprintf(stderr, "Error: Could not allocate memory for pixel data.\n");
        fclose(input_file);
        return 1;
    }
    fread(pixel_data, 1, info_header.biSizeImage, input_file);
    //fclose(input_file);

    uint32_t *pixels = (uint32_t *)pixel_data;
    uint32_t pixel_count = info_header.biSizeImage / 4;

    for (uint32_t i = 0; i < pixel_count; i++) {
        uint32_t pixel = pixels[i];
        uint32_t red = (pixel & info_header.biRedMask);
        uint32_t green = (pixel & info_header.biGreenMask);
        uint32_t blue = (pixel & info_header.biBlueMask);
        uint32_t alpha = (pixel & info_header.biAlphaMask);

        red = invert_color(red, info_header.biRedMask);
        green = invert_color(green, info_header.biGreenMask);
        blue = invert_color(blue, info_header.biBlueMask);

        pixels[i] = red | green | blue | alpha;
    }

    long total_size = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + info_header.biSizeImage;
    uint8_t *bmp_buffer = (uint8_t *)malloc(total_size);
    if (!bmp_buffer) {
        fprintf(stderr, "Error: Could not allocate memory for BMP buffer.\n");
        free(pixel_data);
        return 1;
    }

    // Copy the headers and pixel data into the buffer
    memcpy(bmp_buffer, &file_header, sizeof(BITMAPFILEHEADER));
    memcpy(bmp_buffer + sizeof(BITMAPFILEHEADER), &info_header, sizeof(BITMAPINFOHEADER));
    memcpy(bmp_buffer + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), pixel_data, info_header.biSizeImage);

    // Send the entire BMP buffer to the client
    if (send(client_socket, bmp_buffer, total_size, 0) == -1) {
        perror("Error sending BMP data");
        free(pixel_data);
        free(bmp_buffer);
        return 1;
    }

    free(pixel_data); // Free the allocated memory for pixel data
    free(bmp_buffer); // Free the allocated memory for BMP buffer


return 0;
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
        //printf("file size in bytes before calling handle_bmp: %ld\n", ftell(file));
        handle_bmp(client_socket, file);
    } else if (signature[0] == 0xFF && signature[1] == 0xD8) {
        //printf("File size in bytes before calling handle_jpeg: %ld\n", ftell(file));
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
