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
#include <png.h>
#include <sqlite3.h>

#define BUFFER_SIZE 4096
#define PORT 12345
#define END_SIGNAL "done"

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

#pragma pack(push, 1)

char username[50];
char password[50];

void init_db() {
    sqlite3 *db;
    char *err_msg = 0;

    int rc = sqlite3_open("user_db.sqlite", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    const char *sql = "CREATE TABLE IF NOT EXISTS Users("
                      "Id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "Username TEXT UNIQUE, "
                      "Password TEXT, "
                      "kbmp INTEGER DEFAULT 0, "
                      "kpng INTEGER DEFAULT 0, "
                      "kjpg INTEGER DEFAULT 0, "
                      "isAdmin INTEGER DEFAULT 0, "
                      "isConnected INTEGER DEFAULT 0);";

    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return;
    }

    sqlite3_close(db);
}

void update_database_and_disconnect(const char *username) {
    sqlite3 *db;
    int rc = sqlite3_open("user_db.sqlite", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }
    char query[256];
    snprintf(query, sizeof(query), "UPDATE Clients SET isConnected = 0 WHERE username = '%s'", username);
    rc = sqlite3_exec(db, query, 0, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to execute query: %s\n", sqlite3_errmsg(db));
    }
    sqlite3_close(db);
}

uint32_t invert_color(uint32_t color, uint32_t mask)
{
    return (~color) & mask;
}

typedef struct
{
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BITMAPFILEHEADER2;

typedef struct
{
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

typedef struct
{
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BITMAPFILEHEADER;

typedef struct
{
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

void invert_bmp_colors(uint8_t *img, int size) {
    for (unsigned int i = 0; i < size; i += 4) {
        img[i] = 255 - img[i];       // Blue
        img[i+1] = 255 - img[i+1];   // Green
        img[i+2] = 255 - img[i+2];   // Red
    }
}

void invert_png_colors(png_bytep *row_pointers, int width, int height, int channels)
{
    for (int y = 0; y < height; y++)
    {
        png_bytep row = row_pointers[y];
        for (int x = 0; x < width; x++)
        {
            png_bytep px = &(row[x * channels]);

            if (px[3] != 0)
            {
                px[0] = 255 - px[0]; // Red
                px[1] = 255 - px[1]; // Green
                px[2] = 255 - px[2]; // Blue
                // Alpha channel remains the same
            }
        }
    }
}

void rotate_png_90(png_bytep *row_pointers, int width, int height, int channels)
{
    png_bytep *temp_row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * width);
    for (int x = 0; x < width; x++)
    {
        temp_row_pointers[x] = (png_bytep)malloc(sizeof(png_byte) * height * channels);
    }

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            png_bytep px = &(row_pointers[y][x * channels]);
            png_bytep temp_px = &(temp_row_pointers[x][(height - 1 - y) * channels]);
            memcpy(temp_px, px, channels);
        }
    }

    for (int y = 0; y < height; y++)
    {
        free(row_pointers[y]);
    }
    free(row_pointers);

    // Allocate new memory for the rotated image dimensions
    row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * width);
    for (int y = 0; y < width; y++)
    {
        row_pointers[y] = (png_bytep)malloc(sizeof(png_byte) * height * channels);
        memcpy(row_pointers[y], temp_row_pointers[y], height * channels);
        free(temp_row_pointers[y]);
    }
    free(temp_row_pointers);
}

void rotate_png_180(png_bytep *row_pointers, int width, int height, int channels)
{
    for (int y = 0; y < height / 2; y++)
    {
        png_bytep temp = row_pointers[y];
        row_pointers[y] = row_pointers[height - 1 - y];
        row_pointers[height - 1 - y] = temp;
    }

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width / 2; x++)
        {
            png_bytep px = &(row_pointers[y][x * channels]);
            png_bytep temp_px = &(row_pointers[y][(width - 1 - x) * channels]);
            for (int k = 0; k < channels; k++)
            {
                png_byte temp = px[k];
                px[k] = temp_px[k];
                temp_px[k] = temp;
            }
        }
    }
}

void rotate_png_270(png_bytep *row_pointers, int width, int height, int channels)
{
    rotate_png_180(row_pointers, width, height, channels);
    rotate_png_90(row_pointers, width, height, channels);
}

void convert_png_to_black_white(png_bytep *row_pointers, int width, int height, int channels)
{
    for (int y = 0; y < height; y++)
    {
        png_bytep row = row_pointers[y];
        for (int x = 0; x < width; x++)
        {
            png_bytep px = &(row[x * channels]);
            unsigned char gray = 0.3 * px[0] + 0.59 * px[1] + 0.11 * px[2];
            if (px[3] != 0)
            {
                px[0] = gray; // Red
                px[1] = gray; // Green
                px[2] = gray; // Blue
                // Alpha channel remains the same
            }
        }
    }
}

// Function to rotate BMP image 90 degrees
void rotate_bmp_90(uint8_t *img, int width, int height)
{
    unsigned long img_size = width * height * 4; // 4 channels: BGRA
    uint8_t *temp_img = (uint8_t *)malloc(img_size);

    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {
            int src_index = i * width * 4 + j * 4;
            int dest_index = j * height * 4 + (height - 1 - i) * 4;

            temp_img[dest_index] = img[src_index];         // Blue
            temp_img[dest_index + 1] = img[src_index + 1]; // Green
            temp_img[dest_index + 2] = img[src_index + 2]; // Red
            temp_img[dest_index + 3] = img[src_index + 3]; // Alpha
        }
    }

    memcpy(img, temp_img, img_size);
    free(temp_img);
}

// Function to rotate BMP image 180 degrees
void rotate_bmp_180(uint8_t *img, int width, int height)
{
    unsigned long img_size = width * height * 4; // 4 channels: BGRA
    uint8_t *temp_img = (uint8_t *)malloc(img_size);

    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width * 4; j += 4)
        {
            int src_index = i * width * 4 + j;
            int dest_index = (height - 1 - i) * width * 4 + (width * 4 - 4 - j);

            temp_img[dest_index] = img[src_index];         // Blue
            temp_img[dest_index + 1] = img[src_index + 1]; // Green
            temp_img[dest_index + 2] = img[src_index + 2]; // Red
            temp_img[dest_index + 3] = img[src_index + 3]; // Alpha
        }
    }

    memcpy(img, temp_img, img_size);
    free(temp_img);
}

// Function to rotate BMP image 270 degrees
void rotate_bmp_270(uint8_t *img, int width, int height)
{
    unsigned long img_size = width * height * 4; // 4 channels: BGRA
    uint8_t *temp_img = (uint8_t *)malloc(img_size);

    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {
            int src_index = i * width * 4 + j * 4;
            int dest_index = (width - 1 - j) * height * 4 + i * 4;

            temp_img[dest_index] = img[src_index];         // Blue
            temp_img[dest_index + 1] = img[src_index + 1]; // Green
            temp_img[dest_index + 2] = img[src_index + 2]; // Red
            temp_img[dest_index + 3] = img[src_index + 3]; // Alpha
        }
    }

    memcpy(img, temp_img, img_size);
    free(temp_img);
}

// Function to convert BMP to black/white
void convert_bmp_to_black_white(uint8_t *img, int width, int height)
{
    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width * 4; j += 4)
        {
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
void invert_jpeg_colors(unsigned char *img, unsigned long size)
{
    for (unsigned long i = 0; i < size; i++)
    {
        img[i] = 255 - img[i];
    }
}

// Function to rotate JPEG image 90 degrees
void rotate_jpeg_90(unsigned char *img, int width, int height, int channels)
{
    unsigned long img_size = width * height * channels;
    unsigned char *temp_img = (unsigned char *)malloc(img_size);

    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {
            int src_index = i * width * channels + j * channels;
            int dest_index = j * height * channels + (height - 1 - i) * channels;

            for (int k = 0; k < channels; k++)
            {
                temp_img[dest_index + k] = img[src_index + k];
            }
        }
    }

    memcpy(img, temp_img, img_size);
    free(temp_img);
}

// Function to rotate JPEG image 180 degrees
void rotate_jpeg_180(unsigned char *img, int width, int height, int channels)
{
    unsigned long img_size = width * height * channels;
    unsigned char *temp_img = (unsigned char *)malloc(img_size);

    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width * channels; j += channels)
        {
            int src_index = i * width * channels + j;
            int dest_index = (height - 1 - i) * width * channels + (width * channels - channels - j);

            for (int k = 0; k < channels; k++)
            {
                temp_img[dest_index + k] = img[src_index + k];
            }
        }
    }

    memcpy(img, temp_img, img_size);
    free(temp_img);
}

// Function to rotate JPEG image 270 degrees
void rotate_jpeg_270(unsigned char *img, int width, int height, int channels)
{
    unsigned long img_size = width * height * channels;
    unsigned char *temp_img = (unsigned char *)malloc(img_size);

    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {
            int src_index = i * width * channels + j * channels;
            int dest_index = (width - 1 - j) * height * channels + i * channels;

            for (int k = 0; k < channels; k++)
            {
                temp_img[dest_index + k] = img[src_index + k];
            }
        }
    }

    memcpy(img, temp_img, img_size);
    free(temp_img);
}

// Function to convert JPEG to black/white
void convert_jpeg_to_black_white(unsigned char *img, int width, int height, int channels)
{
    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width * channels; j += channels)
        {
            unsigned char gray = 0.3 * img[i * width * channels + j + 2] +
                                 0.59 * img[i * width * channels + j + 1] +
                                 0.11 * img[i * width * channels + j];
            for (int k = 0; k < channels; k++)
            {
                img[i * width * channels + j + k] = gray;
            }
        }
    }
}

int process_bmp(int client_socket, FILE *input_file, int operation)
{
    BITMAPFILEHEADER file_header;
    fread(&file_header, sizeof(BITMAPFILEHEADER), 1, input_file);
    if (file_header.bfType != 0x4D42)
    {
        fprintf(stderr, "Error: Not a valid BMP file.\n");
        fclose(input_file);
        return 1;
    }
    BITMAPINFOHEADER info_header;
    fread(&info_header, sizeof(BITMAPINFOHEADER), 1, input_file);

    if (info_header.biBitCount != 32)
    {
        fprintf(stderr, "Error: Only 32-bit BMP files are supported.\n");
        fclose(input_file);
        return 1;
    }

    fseek(input_file, file_header.bfOffBits, SEEK_SET);
    uint8_t *pixel_data = (uint8_t *)malloc(info_header.biSizeImage);
    if (!pixel_data)
    {
        fprintf(stderr, "Error: Could not allocate memory for pixel data.\n");
        fclose(input_file);
        return 1;
    }
    fread(pixel_data, 1, info_header.biSizeImage, input_file);

    switch (operation)
    {
        case 1:
            invert_bmp_colors(pixel_data, info_header.biSizeImage);
            break;
        case 2:
            rotate_bmp_90(pixel_data, info_header.biWidth, info_header.biHeight);
            {
                int temp = info_header.biWidth;
                info_header.biWidth = info_header.biHeight;
                info_header.biHeight = temp;
            }
            rotate_bmp_90(pixel_data, info_header.biWidth, info_header.biHeight);
            {
                int temp = info_header.biWidth;
                info_header.biWidth = info_header.biHeight;
                info_header.biHeight = temp;
            }
            rotate_bmp_90(pixel_data, info_header.biWidth, info_header.biHeight);
            {
                int temp = info_header.biWidth;
                info_header.biWidth = info_header.biHeight;
                info_header.biHeight = temp;
            }
            break;
        case 3:
            rotate_bmp_180(pixel_data, info_header.biWidth, info_header.biHeight);
            break;
        case 4:
            rotate_bmp_270(pixel_data, info_header.biWidth, info_header.biHeight);
            {
                int temp = info_header.biWidth;
                info_header.biWidth = info_header.biHeight;
                info_header.biHeight = temp;
            }
            break;
        case 5:
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
    if (!bmp_buffer)
    {
        fprintf(stderr, "Error: Could not allocate memory for BMP buffer.\n");
        free(pixel_data);
        return 1;
    }

    memcpy(bmp_buffer, &file_header, sizeof(BITMAPFILEHEADER));
    memcpy(bmp_buffer + sizeof(BITMAPFILEHEADER), &info_header, sizeof(BITMAPINFOHEADER));
    memcpy(bmp_buffer + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), pixel_data, info_header.biSizeImage);

    if (send(client_socket, bmp_buffer, total_size, 0) == -1)
    {
        perror("Error sending BMP data");
        free(pixel_data);
        free(bmp_buffer);
        return 1;
    }

    if (send(client_socket, END_SIGNAL, strlen(END_SIGNAL), 0) == -1)
    {
        perror("Error sending END_SIGNAL");
        free(pixel_data);
        free(bmp_buffer);
        return 1;
    }

    free(pixel_data);
    free(bmp_buffer);

    return 0;
}

void handle_png(int client_socket, FILE *file, int operation)
{
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png)
    {
        perror("Failed to create png read struct");
        return;
    }

    png_infop info = png_create_info_struct(png);
    if (!info)
    {
        perror("Failed to create png info struct");
        png_destroy_read_struct(&png, NULL, NULL);
        return;
    }

    if (setjmp(png_jmpbuf(png)))
    {
        perror("Failed to set libpng jump buffer");
        png_destroy_read_struct(&png, &info, NULL);
        return;
    }

    png_init_io(png, file);
    png_read_info(png, info);

    int width = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16)
        png_set_strip_16(png);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);

    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);

    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    int channels = png_get_channels(png, info);
    png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
    for (int y = 0; y < height; y++)
    {
        row_pointers[y] = (png_byte *)malloc(png_get_rowbytes(png, info));
    }

    png_read_image(png, row_pointers);
    int temp;
    switch (operation)
    {
        case 1:
            invert_png_colors(row_pointers, width, height, channels);
            break;
        case 2:
            rotate_png_90(row_pointers, width, height, channels);
            temp = width;
            width = height;
            height = temp;
            break;
        case 3:
            rotate_png_180(row_pointers, width, height, channels);
            break;
        case 4:
            rotate_png_270(row_pointers, width, height, channels);
            temp = width;
            width = height;
            height = temp;
            break;
        case 5:
            convert_png_to_black_white(row_pointers, width, height, channels);
            break;
        default:
            fprintf(stderr,"Code: %d\n",operation);
            fprintf(stderr, "Error: Invalid operation code for PNG.\n");
            for (int y = 0; y < height; y++)
            {
                free(row_pointers[y]);
            }
            free(row_pointers);
            png_destroy_read_struct(&png, &info, NULL);
            return;
    }

    png_destroy_read_struct(&png, &info, NULL);

    png_structp png_out = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_out)
    {
        perror("Failed to create png write struct");
        return;
    }

    png_infop info_out = png_create_info_struct(png_out);
    if (!info_out)
    {
        perror("Failed to create png info struct for output");
        png_destroy_write_struct(&png_out, NULL);
        return;
    }

    if (setjmp(png_jmpbuf(png_out)))
    {
        perror("Failed to set libpng jump buffer for output");
        png_destroy_write_struct(&png_out, &info_out);
        return;
    }

    png_set_IHDR(png_out, info_out, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_rows(png_out, info_out, row_pointers);

    unsigned char *png_buffer = NULL;
    size_t png_size = 0;
    FILE *memstream = open_memstream((char **)&png_buffer, &png_size);
    if (!memstream)
    {
        perror("Failed to create memory stream for PNG");
        for (int y = 0; y < height; y++)
        {
            free(row_pointers[y]);
        }
        free(row_pointers);
        png_destroy_write_struct(&png_out, &info_out);
        return;
    }

    png_init_io(png_out, memstream);
    png_write_png(png_out, info_out, PNG_TRANSFORM_IDENTITY, NULL);
    fclose(memstream);

    png_destroy_write_struct(&png_out, &info_out);

    for (int y = 0; y < height; y++)
    {
        free(row_pointers[y]);
    }
    free(row_pointers);

    send(client_socket, png_buffer, png_size, 0);
    free(png_buffer);

    if (send(client_socket, END_SIGNAL, strlen(END_SIGNAL), 0) == -1)
    {
        perror("Error sending END_SIGNAL");
    }
}

void handle_jpeg(int client_socket, FILE *file, int operation)
{
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

    while (cinfo.output_scanline < cinfo.output_height)
    {
        unsigned char *buffer_array[1];
        buffer_array[0] = img_data + (cinfo.output_scanline) * width * pixel_size;
        jpeg_read_scanlines(&cinfo, buffer_array, 1);
    }

    switch (operation)
    {
        case 1:
            invert_jpeg_colors(img_data, img_size);
            break;
        case 2:
            rotate_jpeg_90(img_data, width, height, pixel_size);
            {
                int temp = width;
                width = height;
                height = temp;
            }
            break;
        case 3:
            rotate_jpeg_180(img_data, width, height, pixel_size);
            break;
        case 4:
            rotate_jpeg_270(img_data, width, height, pixel_size);
            {
                int temp = width;
                width = height;
                height = temp;
            }
            break;
        case 5:
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

    while (cinfo_out.next_scanline < cinfo_out.image_height)
    {
        unsigned char *buffer_array[1];
        buffer_array[0] = img_data + (cinfo_out.next_scanline) * width * pixel_size;
        jpeg_write_scanlines(&cinfo_out, buffer_array, 1);
    }

    jpeg_finish_compress(&cinfo_out);
    jpeg_destroy_compress(&cinfo_out);

    free(img_data);

    send(client_socket, jpeg_buffer, jpeg_size, 0);

    if (send(client_socket, END_SIGNAL, strlen(END_SIGNAL), 0) == -1)
    {
        perror("Error sending END_SIGNAL");
    }

    free(jpeg_buffer);
}

int register_user(const char *username, const char *password) {
    sqlite3 *db;
    sqlite3_stmt *stmt;

    int rc = sqlite3_open("user_db.sqlite", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "INSERT INTO Clients(Username, Password) VALUES(?, ?);";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Execution failed: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 0;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 1;
}

int login_user(const char *username, const char *password) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int login_success = 0;
    //1 for success, 0 for failure, -1 for user connected

    int rc = sqlite3_open("user_db.sqlite", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "SELECT Password FROM Clients WHERE Username = ?;";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *db_password = sqlite3_column_text(stmt, 0);
        if (strcmp((const char *)db_password, password) == 0) {
            login_success = 1;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    sqlite3 *db2;
    sqlite3_stmt *stmt2;

    int rc2 = sqlite3_open("user_db.sqlite", &db2);
    const char *sql2 = "SELECT isConnected FROM Clients WHERE Username = ?;";
    rc2 = sqlite3_prepare_v2(db2, sql2, -1, &stmt2, 0);
    if (rc2 != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    rc2 = sqlite3_step(stmt);
    if (rc2 == SQLITE_ROW) {
        int isConnected = sqlite3_column_int(stmt, 0);
        if (isConnected == 1) {
            login_success = -1;
        }
    }

    sqlite3_finalize(stmt2);
    sqlite3_close(db2);
    return login_success;
}

int login(int client_socket) {


    // Receive username
    int bytes_read = recv(client_socket, username, sizeof(username), 0);
    if (bytes_read <= 0) {
        return 0;
    }
    username[bytes_read] = '\0';

    // Receive password
    bytes_read = recv(client_socket, password, sizeof(password), 0);
    if (bytes_read <= 0) {
        return 0;
    }
    password[bytes_read] = '\0';

    // Attempt login
    int val = login_user(username, password);
    switch (val) {
        case -1:
            return -1;
            break;
        case 0:
            return 0;
            break;
        case 1:
            return 1;
            break;
        default:
            return 0;
            break;
    }
}

void send_message(int client_socket, const char *message) {
    size_t length = strlen(message);
    if (send(client_socket, message, length, 0) == -1) {
        perror("send_message");
    }
}

void see_users(int client_socket) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc = sqlite3_open("user_db.sqlite", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    const char *sql = "SELECT Id, Username, Password, kpng, kjpg, kbmp, isAdmin, isConnected FROM Clients;";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    // Buffer for sending data
    char buffer[2048];
    size_t offset = 0;

    // Send table header
    const char *header = "ID\tUsername\tisAdmin\tisConnected\n"
                         "-------------------------------------------------------------\n";
    strncpy(buffer, header, sizeof(buffer));
    offset = strlen(header);

    // Send each user's details in table format
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *username = sqlite3_column_text(stmt, 1);
        int isAdmin = sqlite3_column_int(stmt, 6);
        int isConnected = sqlite3_column_int(stmt, 7);

        int written = snprintf(buffer + offset, sizeof(buffer) - offset,
                               "%d\t%s\t\t%d\t%d\n",
                               id, username, isAdmin, isConnected);

        if (written > 0 && (size_t)written < sizeof(buffer) - offset) {
            offset += written;
        } else {
            // If buffer is full, send its contents and reset
            send_message(client_socket, buffer);
            offset = 0;
            written = snprintf(buffer + offset, sizeof(buffer) - offset,
                               "%d\t%s\t\t%d\t%d\n",
                               id, username, isAdmin, isConnected);
            offset += written;
        }
    }

    // Send any remaining data in the buffer
    if (offset > 0) {
        send_message(client_socket, buffer);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void see_active_users(int client_socket) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc = sqlite3_open("user_db.sqlite", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    const char *sql = "SELECT Id, Username, Password, kpng, kjpg, kbmp, isAdmin, isConnected FROM Clients WHERE isConnected = 1;";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    // Buffer for sending data
    char buffer[2048];
    size_t offset = 0;

    // Send table header
    const char *header = "ID\tUsername\tisAdmin\tisConnected\n"
                         "-------------------------------------------------------------\n";
    strncpy(buffer, header, sizeof(buffer));
    offset = strlen(header);

    // Send each active user's details in table format
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *username = sqlite3_column_text(stmt, 1);
        int isAdmin = sqlite3_column_int(stmt, 6);
        int isConnected = sqlite3_column_int(stmt, 7);

        int written = snprintf(buffer + offset, sizeof(buffer) - offset,
                               "%d\t%s\t\t%d\t%d\n",
                               id, username, isAdmin, isConnected);

        if (written > 0 && (size_t)written < sizeof(buffer) - offset) {
            offset += written;
        } else {
            // If buffer is full, send its contents and reset
            send_message(client_socket, buffer);
            offset = 0;
            written = snprintf(buffer + offset, sizeof(buffer) - offset,
                               "%d\t%s\t\t%d\t%d\n",
                               id, username, isAdmin, isConnected);
            offset += written;
        }
    }

    // Send any remaining data in the buffer
    if (offset > 0) {
        send_message(client_socket, buffer);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void add_user(int client_socket) {
    char username[50];
    char password[50];

    // Receive username
    int bytes_read = recv(client_socket, username, sizeof(username), 0);
    if (bytes_read <= 0) {
        return;
    }
    username[bytes_read] = '\0';

    // Receive password
    bytes_read = recv(client_socket, password, sizeof(password), 0);
    if (bytes_read <= 0) {
        return;
    }
    password[bytes_read] = '\0';

    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc = sqlite3_open("user_db.sqlite", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    const char *sql = "INSERT INTO Clients (Username, Password) VALUES (?, ?);";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        const char *success_msg = "User added successfully\n";
        send(client_socket, success_msg, strlen(success_msg), 0);
    } else {
        const char *error_msg = "Failed to add user\n";
        send(client_socket, error_msg, strlen(error_msg), 0);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void delete_user(int client_socket) {
    char username[50];

    // Receive username
    int bytes_read = recv(client_socket, username, sizeof(username), 0);
    if (bytes_read <= 0) {
        return;
    }
    username[bytes_read] = '\0';

    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc = sqlite3_open("user_db.sqlite", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    const char *sql = "DELETE FROM Clients WHERE Username = ?;";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        const char *success_msg = "User deleted successfully\n";
        send(client_socket, success_msg, strlen(success_msg), 0);
    } else {
        const char *error_msg = "Failed to delete user\n";
        send(client_socket, error_msg, strlen(error_msg), 0);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}


void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    free(arg);
    char buffer[BUFFER_SIZE];
    int bytes_read;
    char localusername[50];
    char localpassword[50];
    int isAdmin = 0;

    while (1) {
        char option;
        bytes_read = recv(client_socket, &option, sizeof(option), 0);
        if (bytes_read <= 0) {
            close(client_socket);
            return NULL;
        }

        if (option == '1') { // Login
            int val = login(client_socket);
            if (val == 1) { //1 success, 0 failure, -1 user connected
                const char *success_msg = "Login successful\n";
                strcpy(localusername, username);
                strcpy(localpassword, password);

                //open db
                sqlite3 *db;
                sqlite3_stmt *stmt;
                int rc = sqlite3_open("user_db.sqlite", &db);
                if (rc != SQLITE_OK) {
                    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
                    return 0;
                }

                //send(client_socket, success_msg, strlen(success_msg), 0);
                char query[256];
                snprintf(query, sizeof(query), "UPDATE Clients SET isConnected = 1 WHERE username = '%s'", localusername);
                //execute query
                rc = sqlite3_exec(db, query, 0, 0, 0);
                if (rc != SQLITE_OK) {
                    fprintf(stderr, "Failed to execute query: %s\n", sqlite3_errmsg(db));
                }

                // Check if the user is an admin
                const char *admin_check_sql = "SELECT isAdmin FROM Clients WHERE Username = ?;";
                rc = sqlite3_prepare_v2(db, admin_check_sql, -1, &stmt, 0);
                if (rc != SQLITE_OK) {
                    fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
                    sqlite3_close(db);
                    //return;
                }

                sqlite3_bind_text(stmt, 1, localusername, -1, SQLITE_STATIC);
                rc = sqlite3_step(stmt);
                if (rc == SQLITE_ROW) {
                    isAdmin = sqlite3_column_int(stmt, 0);
                    if (isAdmin == 1) {
                        printf("User %s is an admin.\n", localusername);
                        send(client_socket, "You are an admin.", strlen("You are an admin."), 0);
                    } else {
                        printf("User %s is not an admin.\n", localusername);
                        send(client_socket, "You bbb an admin.", strlen("You are an admin."), 0);
                    }
                }

                sqlite3_finalize(stmt);
                sqlite3_close(db);

                break;
            } else if (val == 0) {
                const char *error_msg = "Invalid username or password\n";
                send(client_socket, error_msg, strlen(error_msg), 0);
            } else {
                const char *error_msg = "User already connected\n";
                send(client_socket, error_msg, strlen(error_msg), 0);
            }
        } else if (option == '2') { // Register
            char username[50];
            char password[50];

            // Receive username
            bytes_read = recv(client_socket, username, sizeof(username), 0);
            if (bytes_read <= 0) {
                close(client_socket);
                return NULL;
            }
            username[bytes_read] = '\0';

            // Receive password
            bytes_read = recv(client_socket, password, sizeof(password), 0);
            if (bytes_read <= 0) {
                close(client_socket);
                return NULL;
            }
            password[bytes_read] = '\0';

            if (register_user(username, password)) {
                const char *success_msg = "Registration successful\n";
                send(client_socket, success_msg, strlen(success_msg), 0);
            } else {
                const char *error_msg = "Registration failed (username might be taken)\n";
                send(client_socket, error_msg, strlen(error_msg), 0);
            }
        }
    }

    while (1) {
        if (!isAdmin) {
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

            if (total_bytes == 0) {
                printf("Client disconnected.\n");
                update_database_and_disconnect(localusername);
                fclose(file);
                close(client_socket);
                return NULL;
            }
            const char *confirmation = "File received";
            send(client_socket, confirmation, strlen(confirmation), 0);

            unsigned char signature[2];
            fread(signature, 1, 2, file);
            fseek(file, 0, SEEK_SET);

            int operation_code;
            if (recv(client_socket, &operation_code, sizeof(operation_code), 0) <= 0) {
                perror("Failed to receive operation code");
                update_database_and_disconnect(localusername);
                fclose(file);
                close(client_socket);
                return NULL;
            }
            if (operation_code > 100) //python client
                operation_code = ntohl(operation_code);
            pthread_mutex_lock(&lock);
            if (signature[0] == 0x42 && signature[1] == 0x4D) {
                process_bmp(client_socket, file, operation_code);
            } else if (signature[0] == 0xFF && signature[1] == 0xD8) {
                handle_jpeg(client_socket, file, operation_code);
            } else if (signature[0] == 0x89 && signature[1] == 0x50) {
                handle_png(client_socket, file, operation_code);
            } else {
                const char *error_msg = "Unsupported file format";
                send(client_socket, error_msg, strlen(error_msg), 0);
            }
            pthread_mutex_unlock(&lock);

            fclose(file);
            printf("Ready for next operation.\n");
        } else { // Admin
            int operation_code;

            printf("Sa-l ia dracu\n\n");

            if (recv(client_socket, &operation_code, sizeof(operation_code), 0) <= 0) {
                perror("Client disconnected");
                update_database_and_disconnect(localusername);
                close(client_socket);
                return NULL;
            }

            printf("Operation code: %d\n", operation_code);
            operation_code = ntohl(operation_code);
            printf("Operation code formatted: %d\n", operation_code);

            switch (operation_code) {
                case 1:
                    see_users(client_socket);
                    //send(client_socket, "La misto", strlen("La misto"), 0);
                    break;
                case 2:
                    see_active_users(client_socket);
                    break;
                case 3:
                    add_user(client_socket);
                    break;
                case 4:
                    delete_user(client_socket);
                    break;
                default:
                    const char *error_msg = "Invalid operation code";
                    send(client_socket, error_msg, strlen(error_msg), 0);
                    break;
            }
        }
    }
}



int main()
{
    int server_socket, *new_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t thread_id;

    init_db();

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Failed to create socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Failed to bind");
        close(server_socket);
        return 1;
    }

    if (listen(server_socket, 5) < 0)
    {
        perror("Failed to listen");
        close(server_socket);
        return 1;
    }

    printf("Server started on port %d... Waiting for connections...\n", PORT);

    while (1)
    {
        new_sock = malloc(sizeof(int));
        if (new_sock == NULL)
        {
            perror("Failed to allocate memory for new socket");
            continue;
        }

        *new_sock = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (*new_sock < 0)
        {
            perror("Failed to accept client");
            free(new_sock);
            continue;
        }

        printf("Client connected from %s:%d...\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        if (pthread_create(&thread_id, NULL, handle_client, (void *)new_sock) < 0)
        {
            perror("Could not create thread");
            free(new_sock);
        }
        pthread_detach(thread_id);
    }

    close(server_socket);
    return 0;
}
