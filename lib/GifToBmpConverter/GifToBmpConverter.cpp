#include "GifToBmpConverter.h"

#include <AnimatedGIF.h>
#include <HardwareSerial.h>
#include <SdFat.h>

// Global variables for callbacks
static Print* g_output = nullptr;
static int g_width = 0;
static int g_height = 0;
static std::function<bool()> g_shouldAbort = nullptr;
static uint8_t* g_imageBuffer = nullptr;
static size_t g_bufferSize = 0;

// BMP header structure
#pragma pack(push, 1)
struct BMPHeader {
  uint16_t bfType = 0x4D42;  // "BM"
  uint32_t bfSize = 0;
  uint16_t bfReserved1 = 0;
  uint16_t bfReserved2 = 0;
  uint32_t bfOffBits = 54;  // 14 + 40
  uint32_t biSize = 40;
  int32_t biWidth = 0;
  int32_t biHeight = 0;
  uint16_t biPlanes = 1;
  uint16_t biBitCount = 24;
  uint32_t biCompression = 0;
  uint32_t biSizeImage = 0;
  int32_t biXPelsPerMeter = 0;
  int32_t biYPelsPerMeter = 0;
  uint32_t biClrUsed = 0;
  uint32_t biClrImportant = 0;
};
#pragma pack(pop)

// GIF decoder instance
static AnimatedGIF gif;

// Callback for GIF drawing
void * GIFOpenFile(const char *fname, int32_t *pSize) {
  // Not used since we pass buffer
  return nullptr;
}

void GIFCloseFile(void *pHandle) {
  // Not used
}

int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  // Not used since we pass buffer
  return 0;
}

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  // Not used
  return 0;
}

void GIFDraw(GIFDRAW *pDraw) {
  if (!g_output || !g_imageBuffer || (g_shouldAbort && g_shouldAbort())) return;

  uint8_t* s = pDraw->pPixels;
  int y = pDraw->y;  // current line (top-down)

  // BMP is bottom-up, so flip Y
  int bmpY = g_height - 1 - y;

  // Copy the row to buffer
  uint8_t* dest = g_imageBuffer + (bmpY * g_width * 3);
  for (int x = 0; x < g_width; x++) {
    uint8_t r, g, b;
    if (pDraw->ucHasTransparency && s[x] == pDraw->ucTransparent) {
      r = g = b = 255;  // White background
    } else {
      // Get color from palette
      uint16_t color = pDraw->pPalette[s[x]];
      r = ((color >> 11) & 0x1F) * 255 / 31;  // 5 bits to 8
      g = ((color >> 5) & 0x3F) * 255 / 63;   // 6 bits to 8
      b = (color & 0x1F) * 255 / 31;          // 5 bits to 8
    }
    *dest++ = b;  // BMP is BGR
    *dest++ = g;
    *dest++ = r;
  }
}

bool GifToBmpConverter::gifFileToBmpStream(FsFile& input, Print& output, int maxWidth, int maxHeight) {
  return gifFileToBmpStreamWithSize(input, output, maxWidth, maxHeight, nullptr);
}

bool GifToBmpConverter::gifFileToBmpStreamWithSize(FsFile& input, Print& output, int maxWidth, int maxHeight,
                                                   std::function<bool()> shouldAbort) {
  g_output = &output;
  g_shouldAbort = shouldAbort;

  // Read the entire file into memory (GIFs are typically small)
  size_t fileSize = input.size();
  if (fileSize > 200 * 1024) {  // Limit to 200KB
    Serial.printf("[GIF] File too large: %zu bytes\n", fileSize);
    return false;
  }

  uint8_t* fileBuffer = (uint8_t*)malloc(fileSize);
  if (!fileBuffer) {
    Serial.printf("[GIF] Failed to allocate memory for file\n");
    return false;
  }

  size_t bytesRead = input.read(fileBuffer, fileSize);
  if (bytesRead != fileSize) {
    free(fileBuffer);
    return false;
  }

  // Initialize GIF decoder
  gif.begin(LITTLE_ENDIAN_PIXELS);

  int result = gif.open(fileBuffer, fileSize, nullptr);
  if (result != GIF_SUCCESS) {
    Serial.printf("[GIF] Failed to open GIF: %d\n", result);
    free(fileBuffer);
    return false;
  }

  g_width = gif.getCanvasWidth();
  g_height = gif.getCanvasHeight();

  // Allocate buffer for the image
  g_bufferSize = g_width * g_height * 3;  // 24-bit RGB
  g_imageBuffer = (uint8_t*)malloc(g_bufferSize);
  if (!g_imageBuffer) {
    Serial.printf("[GIF] Failed to allocate image buffer\n");
    gif.close();
    free(fileBuffer);
    return false;
  }

  // Decode first frame
  result = gif.playFrame(true, nullptr);
  if (result != GIF_SUCCESS) {
    Serial.printf("[GIF] Failed to decode frame: %d\n", result);
    free(g_imageBuffer);
    g_imageBuffer = nullptr;
    gif.close();
    free(fileBuffer);
    return false;
  }

  // Write BMP header
  BMPHeader header;
  header.bfSize = sizeof(BMPHeader) + g_bufferSize;
  header.biWidth = g_width;
  header.biHeight = g_height;
  header.biSizeImage = g_bufferSize;

  output.write((uint8_t*)&header, sizeof(header));

  // Write image data
  output.write(g_imageBuffer, g_bufferSize);

  // Cleanup
  free(g_imageBuffer);
  g_imageBuffer = nullptr;
  gif.close();
  free(fileBuffer);

  return true;
}

bool GifToBmpConverter::gifFileToBmpStreamQuick(FsFile& input, Print& output, int maxWidth, int maxHeight) {
  // Quick mode is same as normal for GIF
  return gifFileToBmpStreamWithSize(input, output, maxWidth, maxHeight, nullptr);
}