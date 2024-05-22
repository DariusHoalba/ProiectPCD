#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#pragma pack(push, 1)
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
} BITMAPINFOHEADER;
#pragma pack(pop)

void invert_colors(uint8_t *pixel_data, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        pixel_data[i] = 255 - pixel_data[i];
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.bmp> <output.bmp>\n", argv[0]);
        return 1;
    }

    FILE *input_file = fopen(argv[1], "rb");
    if (!input_file) {
        perror("Error opening input file");
        return 1;
    }

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
    fclose(input_file);

    invert_colors(pixel_data, info_header.biSizeImage);

    FILE *output_file = fopen(argv[2], "wb");
    if (!output_file) {
        perror("Error opening output file");
        free(pixel_data);
        return 1;
    }

    fwrite(&file_header, sizeof(BITMAPFILEHEADER), 1, output_file);
    fwrite(&info_header, sizeof(BITMAPINFOHEADER), 1, output_file);
    fwrite(pixel_data, 1, info_header.biSizeImage, output_file);

    fclose(output_file);
    free(pixel_data);

    printf("Successfully inverted the colors of the BMP image.\n");
    return 0;
}

