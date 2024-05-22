#include <stdio.h>
#include <stdlib.h>

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

void invertColors32(unsigned char* img, unsigned int size) {
    for (unsigned int i = 0; i < size; i += 4) { // Each pixel: B, G, R, A
        img[i] = 255 - img[i];       // Blue
        img[i+1] = 255 - img[i+1];   // Green
        img[i+2] = 255 - img[i+2];   // Red
        // Alpha channel (img[i+3]) is not modified
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <input bitmap file> <output bitmap file>\n", argv[0]);
        return 1;
    }

    FILE* inputFile = fopen(argv[1], "rb");
    FILE* outputFile = fopen(argv[2], "wb");
    if (!inputFile || !outputFile) {
        printf("Error opening files!\n");
        return 2;
    }

    BITMAPFILEHEADER bfHeader;
    BITMAPINFOHEADER biHeader;

    fread(&bfHeader, sizeof(BITMAPFILEHEADER), 1, inputFile);
    fread(&biHeader, sizeof(BITMAPINFOHEADER), 1, inputFile);

    if (bfHeader.type != 0x4D42 || biHeader.bitCount != 32) {
        printf("Unsupported file format or bit depth (only 32-bit supported).\n");
        fclose(inputFile);
        fclose(outputFile);
        return 3;
    }

    fseek(inputFile, bfHeader.offset, SEEK_SET);
    unsigned int imgSize = biHeader.sizeImage == 0 ? (biHeader.width * 4 * abs(biHeader.height)) : biHeader.sizeImage;
    unsigned char* imgData = (unsigned char*)malloc(imgSize);

    fread(imgData, imgSize, 1, inputFile);
    invertColors32(imgData, imgSize);

    // Recalculate image size
    biHeader.sizeImage = imgSize;

    // Convert back from DPI to pixels per meter
    biHeader.xPelsPerMeter = (int)(biHeader.xPelsPerMeter / 0.0254);
    biHeader.yPelsPerMeter = (int)(biHeader.yPelsPerMeter / 0.0254);

    // Calculate the file size
    bfHeader.size = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imgSize;

    // Write headers and image data
    fwrite(&bfHeader, sizeof(BITMAPFILEHEADER), 1, outputFile);
    fwrite(&biHeader, sizeof(BITMAPINFOHEADER), 1, outputFile);
    fwrite(imgData, imgSize, 1, outputFile);

    printf("Inversion complete.\n");

    free(imgData);
    fclose(inputFile);
    fclose(outputFile);

    return 0;
}
