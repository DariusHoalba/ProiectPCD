#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// BMP header structures
#pragma pack(push, 1)
typedef struct {
    unsigned short bfType;
    unsigned int bfSize;
    unsigned short bfReserved1;
    unsigned short bfReserved2;
    unsigned int bfOffBits;
} BITMAPFILEHEADER;

typedef struct {
    unsigned int biSize;
    int biWidth;
    int biHeight;
    unsigned short biPlanes;
    unsigned short biBitCount;
    unsigned int biCompression;
    unsigned int biSizeImage;
    int biXPelsPerMeter;
    int biYPelsPerMeter;
    unsigned int biClrUsed;
    unsigned int biClrImportant;
} BITMAPINFOHEADER;
#pragma pack(pop)

void invertColors(unsigned char* pixelData, int width, int height, int rowSize) {
    for (int y = 0; y < height; y++) {
        unsigned char* row = pixelData + y * rowSize;
        for (int x = 0; x < width * 4; x++) {
            row[x] = ~row[x]; // Invert the color
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <bmp_file>\n", argv[0]);
        return 1;
    }

    FILE* file = fopen(argv[1], "rb");
    if (!file) {
        perror("Error opening file");
        return 1;
    }

    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;

    // Read the file header
    fread(&fileHeader, sizeof(BITMAPFILEHEADER), 1, file);
    if (fileHeader.bfType != 0x4D42) { // 'BM' in hexadecimal
        printf("Not a valid BMP file.\n");
        fclose(file);
        return 1;
    }

    // Read the info header
    fread(&infoHeader, sizeof(BITMAPINFOHEADER), 1, file);
    if (infoHeader.biBitCount != 32) {
        printf("The BMP file is not 32-bit.\n");
        fclose(file);
        return 1;
    }

    int width = infoHeader.biWidth;
    int height = infoHeader.biHeight;
    int rowSize = (width * 4 + 3) & ~3; // Ensure row size is a multiple of 4

    // Allocate memory for pixel data
    unsigned char* pixelData = (unsigned char*)malloc(rowSize * height);
    if (!pixelData) {
        printf("Memory allocation failed.\n");
        fclose(file);
        return 1;
    }

    // Move file pointer to the start of pixel data
    fseek(file, fileHeader.bfOffBits, SEEK_SET);

    // Read the pixel data
    fread(pixelData, 1, rowSize * height, file);
    fclose(file);

    // Invert colors
    invertColors(pixelData, width, height, rowSize);

    // Create the output file name
    char outputFileName[256];
    strncpy(outputFileName, argv[1], 252);
    strcat(outputFileName, "_modified.bmp");

    // Write the modified pixel data to a new file
    FILE* outputFile = fopen(outputFileName, "wb");
    if (!outputFile) {
        perror("Error opening output file");
        free(pixelData);
        return 1;
    }

    // Update headers for the new file size
    fileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + rowSize * height;
    infoHeader.biSizeImage = rowSize * height;

    // Write headers
    fwrite(&fileHeader, sizeof(BITMAPFILEHEADER), 1, outputFile);
    fwrite(&infoHeader, sizeof(BITMAPINFOHEADER), 1, outputFile);

    // Write modified pixel data
    fwrite(pixelData, 1, rowSize * height, outputFile);

    fclose(outputFile);
    free(pixelData);

    printf("Inverted color BMP saved as %s\n", outputFileName);

    return 0;
}
