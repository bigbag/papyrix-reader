/**
 * @file TinyGifDecoder.cpp
 * @brief Lightweight GIF to BMP decoder implementation
 *
 * Key features and fixes:
 * - LZW decompression with proper CLEAR_CODE and special case (KwKwK) handling
 * - Dynamic memory allocation based on image size (8KB-64KB range)
 * - Heap-allocated code table to avoid stack overflow on ESP32
 * - Proper BMP row padding for 4-byte alignment (fixes column shift artifacts)
 * - Buffer overflow protection with comprehensive bounds checking
 * - GIF sub-block reading for LZW data stream
 *
 * Memory usage:
 * - Code table: ~24KB (heap allocated)
 * - Code table buffer: 8KB-64KB dynamic (heap allocated)
 * - Image buffer: width * height bytes (heap allocated)
 *
 * Limitations:
 * - First frame only (no animation support)
 * - No interlacing support: All GIF data is treated as sequential regardless of
 *   the interlace flag. Truly interlaced GIFs would display with scrambled rows.
 *   Interlacing is rare in modern GIFs (mainly used for dial-up era progressive loading).
 * - No transparency support (transparent pixels rendered as white)
 *
 * Tested with:
 * - GIF87a and GIF89a formats
 * - Images with global and local color tables
 * - Various image dimensions (38-43 pixels wide tested)
 * - Sequential GIFs (including those with incorrectly set interlace flags)
 */

#include "TinyGifDecoder.h"

#include <HardwareSerial.h>

#include <cstring>
#include <vector>

// Basic RGB palette for fallback (256 colors)
const uint8_t basicPalette[768] = {
    0, 0, 0, 128, 0, 0, 0, 128, 0, 128, 128, 0, 0, 0, 128, 128, 0, 128, 0, 128, 128, 192, 192, 192, 128, 128, 128, 255,
    0, 0, 0, 255, 0, 255, 255, 0, 0, 0, 255, 255, 0, 255, 0, 255, 255, 255, 255, 255,
    // Fill the rest with grayscale gradient
    0, 0, 0, 5, 5, 5, 10, 10, 10, 15, 15, 15, 20, 20, 20, 25, 25, 25, 30, 30, 30, 35, 35, 35, 40, 40, 40, 45, 45, 45,
    50, 50, 50, 55, 55, 55, 60, 60, 60, 65, 65, 65, 70, 70, 70, 75, 75, 75, 80, 80, 80, 85, 85, 85, 90, 90, 90, 95, 95,
    95, 100, 100, 100, 105, 105, 105, 110, 110, 110, 115, 115, 115, 120, 120, 120, 125, 125, 125, 130, 130, 130, 135,
    135, 135, 140, 140, 140, 145, 145, 145, 150, 150, 150, 155, 155, 155, 160, 160, 160, 165, 165, 165, 170, 170, 170,
    175, 175, 175, 180, 180, 180, 185, 185, 185, 190, 190, 190, 195, 195, 195, 200, 200, 200, 205, 205, 205, 210, 210,
    210, 215, 215, 215, 220, 220, 220, 225, 225, 225, 230, 230, 230, 235, 235, 235, 240, 240, 240, 245, 245, 245, 250,
    250, 250, 255, 255, 255,
    // Continue filling to 256 colors (768 bytes total)
    0, 0, 0, 5, 5, 5, 10, 10, 10, 15, 15, 15, 20, 20, 20, 25, 25, 25, 30, 30, 30, 35, 35, 35, 40, 40, 40, 45, 45, 45,
    50, 50, 50, 55, 55, 55, 60, 60, 60, 65, 65, 65, 70, 70, 70, 75, 75, 75, 80, 80, 80, 85, 85, 85, 90, 90, 90, 95, 95,
    95, 100, 100, 100, 105, 105, 105, 110, 110, 110, 115, 115, 115, 120, 120, 120, 125, 125, 125, 130, 130, 130, 135,
    135, 135, 140, 140, 140, 145, 145, 145, 150, 150, 150, 155, 155, 155, 160, 160, 160, 165, 165, 165, 170, 170, 170,
    175, 175, 175, 180, 180, 180, 185, 185, 185, 190, 190, 190, 195, 195, 195, 200, 200, 200, 205, 205, 205, 210, 210,
    210, 215, 215, 215, 220, 220, 220, 225, 225, 225, 230, 230, 230, 235, 235, 235, 240, 240, 240, 245, 245, 245, 250,
    250, 250, 255, 255, 255,
    // More colors to fill array
    64, 64, 64, 96, 96, 96, 128, 128, 128, 160, 160, 160, 192, 192, 192, 224, 224, 224, 32, 32, 32, 64, 64, 64, 96, 96,
    96, 128, 128, 128, 160, 160, 160, 192, 192, 192, 224, 224, 224, 255, 255, 255, 0, 0, 64, 0, 0, 96, 0, 0, 128, 0, 0,
    160, 0, 0, 192, 0, 0, 224, 0, 0, 255, 0, 64, 0, 0, 96, 0, 0, 128, 0, 0, 160, 0, 0, 192, 0, 0, 224, 0, 0, 255, 0, 64,
    0, 0, 96, 0, 0, 128, 0, 0, 160, 0, 0, 192, 0, 0, 224, 0, 0, 255, 0, 0, 64, 64, 0, 96, 96, 0, 128, 128, 0, 160, 160,
    0, 192, 192, 0, 224, 224, 0, 255, 255, 0, 0, 64, 64, 0, 96, 96, 0, 128, 128, 0, 160, 160, 0, 192, 192, 0, 224, 224,
    0, 255, 255, 64, 0, 64, 96, 0, 96, 128, 0, 128, 160, 0, 160, 192, 0, 192, 224, 0, 224, 255, 0, 255, 64, 64, 64, 96,
    96, 96, 128, 128, 128, 160, 160, 160, 192, 192, 192, 224, 224, 224, 255, 255, 255};

// BMP header structure (same as in GifToBmpConverter)
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

bool TinyGifDecoder::decodeGifToBmp(const uint8_t* gifData, size_t gifSize, Print& output, int maxWidth, int maxHeight,
                                    std::function<bool()> shouldAbort) {
  const uint8_t* data = gifData;
  size_t size = gifSize;

  // Parse GIF header
  if (!parseHeader(data, size)) {
    Serial.printf("[TinyGIF] ERROR: Invalid GIF header\n");
    return false;
  }

  // Parse logical screen descriptor
  LogicalScreenDescriptor lsd;
  if (!parseLogicalScreen(data, size, lsd)) {
    Serial.printf("[TinyGIF] ERROR: Invalid logical screen descriptor\n");
    return false;
  }

  // Parse global color table if present
  uint8_t* globalColorTable = nullptr;
  int globalColorTableSize = 0;
  if ((lsd.flags & 0x80) != 0) {
    int tableSize = 1 << ((lsd.flags & 0x07) + 1);
    globalColorTableSize = tableSize * 3;
    if (size < (size_t)globalColorTableSize) {
      Serial.printf("[TinyGIF] ERROR: Not enough data for global color table\n");
      return false;
    }
    globalColorTable = (uint8_t*)malloc(globalColorTableSize);
    if (!globalColorTable) {
      Serial.printf("[TinyGIF] ERROR: Failed to allocate global color table\n");
      return false;
    }
    memcpy(globalColorTable, data, globalColorTableSize);
    data += globalColorTableSize;
    size -= globalColorTableSize;
  }

  // Skip extensions until we find the first image
  while (size > 0 && *data != 0x2C) {
    if (!skipExtensions(data, size)) {
      if (globalColorTable) free(globalColorTable);
      return false;
    }
  }

  if (size == 0 || *data != 0x2C) {
    Serial.printf("[TinyGIF] ERROR: No image found\n");
    if (globalColorTable) free(globalColorTable);
    return false;
  }

  // Parse image descriptor
  ImageDescriptor imgDesc;
  if (!parseImageDescriptor(data, size, imgDesc)) {
    Serial.printf("[TinyGIF] ERROR: Invalid image descriptor\n");
    if (globalColorTable) free(globalColorTable);
    return false;
  }

  // Parse local color table if present
  uint8_t* localColorTable = nullptr;
  int localColorTableSize = 0;
  if ((imgDesc.flags & 0x80) != 0) {
    int tableSize = 1 << ((imgDesc.flags & 0x07) + 1);
    localColorTableSize = tableSize * 3;
    if (size < (size_t)localColorTableSize) {
      Serial.printf("[TinyGIF] ERROR: Not enough data for local color table\n");
      if (globalColorTable) free(globalColorTable);
      return false;
    }
    localColorTable = (uint8_t*)malloc(localColorTableSize);
    if (!localColorTable) {
      Serial.printf("[TinyGIF] ERROR: Failed to allocate local color table\n");
      if (globalColorTable) free(globalColorTable);
      return false;
    }
    memcpy(localColorTable, data, localColorTableSize);
    data += localColorTableSize;
    size -= localColorTableSize;
  }

  // Use local color table if present, otherwise global, otherwise basic palette
  const uint8_t* colorTable = localColorTable ? localColorTable : (globalColorTable ? globalColorTable : basicPalette);
  int colorTableEntries =
      localColorTable ? (localColorTableSize / 3) : (globalColorTable ? (globalColorTableSize / 3) : 256);

  // Validate and allocate buffer for decompressed image
  int width = imgDesc.width;
  int height = imgDesc.height;

  if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
    Serial.printf("[TinyGIF] ERROR: Invalid dimensions %dx%d\n", width, height);
    if (globalColorTable) free(globalColorTable);
    if (localColorTable) free(localColorTable);
    return false;
  }
  if (height > SIZE_MAX / width) {
    Serial.printf("[TinyGIF] ERROR: Image dimensions would overflow\n");
    if (globalColorTable) free(globalColorTable);
    if (localColorTable) free(localColorTable);
    return false;
  }

  size_t imageSize = (size_t)width * height;
  uint8_t* imageBuffer = (uint8_t*)malloc(imageSize);
  if (!imageBuffer) {
    Serial.printf("[TinyGIF] ERROR: Failed to allocate image buffer\n");
    if (globalColorTable) free(globalColorTable);
    if (localColorTable) free(localColorTable);
    return false;
  }

  // Read LZW minimum code size
  if (size < 1) {
    Serial.printf("[TinyGIF] ERROR: Missing LZW code size\n");
    free(imageBuffer);
    if (globalColorTable) free(globalColorTable);
    if (localColorTable) free(localColorTable);
    return false;
  }
  uint8_t lzwMinCodeSize = *data++;
  size--;

  // Decompress LZW data
  if (!decompressLZW(data, size, imageBuffer, imageSize, width, height, colorTable, colorTableEntries,
                     lzwMinCodeSize)) {
    Serial.printf("[TinyGIF] ERROR: LZW decompression failed\n");
    free(imageBuffer);
    if (globalColorTable) free(globalColorTable);
    if (localColorTable) free(localColorTable);
    return false;
  }

  // Write BMP header
  writeBmpHeader(output, width, height);

  // Write image data (BMP is bottom-up with 4-byte row alignment)
  int rowBytes = width * 3;  // 24-bit RGB
  int padding = (4 - (rowBytes % 4)) % 4;

  for (int y = height - 1; y >= 0; y--) {
    // Write pixel data for this row
    for (int x = 0; x < width; x++) {
      size_t pixelIndex = (size_t)y * width + x;
      if (pixelIndex >= imageSize) {
        // Safety check - should never happen but prevents crash
        output.write((uint8_t)0);
        output.write((uint8_t)0);
        output.write((uint8_t)0);
        continue;
      }
      uint8_t pixel = imageBuffer[pixelIndex];
      if (pixel < colorTableEntries) {
        uint8_t r = colorTable[pixel * 3];
        uint8_t g = colorTable[pixel * 3 + 1];
        uint8_t b = colorTable[pixel * 3 + 2];
        output.write(b);  // BMP is BGR
        output.write(g);
        output.write(r);
      } else {
        // Out of range, use black
        output.write((uint8_t)0);
        output.write((uint8_t)0);
        output.write((uint8_t)0);
      }
    }

    // Write padding bytes to align row to 4-byte boundary
    for (int p = 0; p < padding; p++) {
      output.write((uint8_t)0);
    }
  }

  // Cleanup
  free(imageBuffer);
  if (globalColorTable) free(globalColorTable);
  if (localColorTable) free(localColorTable);
  return true;
}

bool TinyGifDecoder::parseHeader(const uint8_t*& data, size_t& size) {
  if (size < 6) return false;
  if (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0) return false;
  data += 6;
  size -= 6;
  return true;
}

bool TinyGifDecoder::parseLogicalScreen(const uint8_t*& data, size_t& size, LogicalScreenDescriptor& lsd) {
  if (size < 7) return false;
  lsd.width = data[0] | (data[1] << 8);
  lsd.height = data[2] | (data[3] << 8);
  lsd.flags = data[4];
  lsd.bgColorIndex = data[5];
  lsd.pixelAspectRatio = data[6];
  data += 7;
  size -= 7;
  return true;
}

bool TinyGifDecoder::parseImageDescriptor(const uint8_t*& data, size_t& size, ImageDescriptor& imgDesc) {
  if (size < 10 || *data != 0x2C) return false;
  data++;  // Skip separator
  size--;

  imgDesc.left = data[0] | (data[1] << 8);
  imgDesc.top = data[2] | (data[3] << 8);
  imgDesc.width = data[4] | (data[5] << 8);
  imgDesc.height = data[6] | (data[7] << 8);
  imgDesc.flags = data[8];

  data += 9;
  size -= 9;
  return true;
}

bool TinyGifDecoder::skipExtensions(const uint8_t*& data, size_t& size) {
  if (size < 2) return false;

  uint8_t extensionType = *data++;
  size--;

  if (extensionType == 0x21) {  // Extension
    if (size < 1) return false;
    uint8_t label = *data++;
    size--;

    // Skip extension data
    while (size > 0) {
      if (size < 1) return false;
      uint8_t blockSize = *data++;
      size--;
      if (blockSize == 0) break;  // End of extension
      if (size < blockSize) return false;
      data += blockSize;
      size -= blockSize;
    }
  } else if (extensionType == 0x3B) {  // Trailer
    return false;                      // End of GIF
  }

  return true;
}

void TinyGifDecoder::writeBmpHeader(Print& output, int width, int height) {
  // Calculate row size with padding (BMP rows must be aligned to 4-byte boundaries)
  int rowBytes = width * 3;  // 24-bit RGB
  int padding = (4 - (rowBytes % 4)) % 4;
  int paddedRowBytes = rowBytes + padding;
  size_t imageDataSize = paddedRowBytes * height;

  BMPHeader header;
  header.bfSize = sizeof(BMPHeader) + imageDataSize;
  header.biWidth = width;
  header.biHeight = height;
  header.biSizeImage = imageDataSize;

  output.write((uint8_t*)&header, sizeof(header));
}

bool TinyGifDecoder::decompressLZW(const uint8_t* compressedData, size_t compressedSize, uint8_t* output,
                                   size_t outputSize, int width, int height, const uint8_t* colorTable,
                                   int colorTableSize, uint8_t minCodeSize) {
  // Validate input parameters
  if (!compressedData || !output || !colorTable) {
    Serial.printf("[TinyGIF] LZW ERROR: NULL pointer\n");
    return false;
  }
  if (compressedSize == 0 || outputSize == 0) {
    Serial.printf("[TinyGIF] LZW ERROR: Invalid size\n");
    return false;
  }
  if (width <= 0 || height <= 0 || (size_t)width * height != outputSize) {
    Serial.printf("[TinyGIF] LZW ERROR: Dimension mismatch\n");
    return false;
  }
  if (minCodeSize < 2 || minCodeSize > 8) {
    Serial.printf("[TinyGIF] LZW ERROR: Invalid code size %d\n", minCodeSize);
    return false;
  }

  const uint8_t* data = compressedData;
  size_t size = compressedSize;
  uint8_t* outPtr = output;
  size_t outRemaining = outputSize;

  // Initialize LZW parameters
  int codeSize = minCodeSize + 1;
  const int MAX_CODE = 4096;
  const int CLEAR_CODE = 1 << minCodeSize;
  const int END_CODE = CLEAR_CODE + 1;

  if (CLEAR_CODE >= MAX_CODE || END_CODE >= MAX_CODE) {
    Serial.printf("[TinyGIF] LZW ERROR: Invalid code range\n");
    return false;
  }

  // Allocate code table (dynamic size based on image dimensions)
  size_t calculatedSize = outputSize * 16;
  if (calculatedSize > 64 * 1024) calculatedSize = 64 * 1024;
  if (calculatedSize < 8 * 1024) calculatedSize = 8 * 1024;
  const int MAX_CODE_TABLE_SIZE = calculatedSize;

  uint8_t* codeTableBuffer = (uint8_t*)malloc(MAX_CODE_TABLE_SIZE);
  if (!codeTableBuffer) {
    Serial.printf("[TinyGIF] LZW ERROR: Failed to allocate code table buffer\n");
    return false;
  }

  struct CodeEntry {
    uint16_t offset;
    uint16_t length;
    bool used;
  };

  CodeEntry* codeTable = (CodeEntry*)malloc(MAX_CODE * sizeof(CodeEntry));
  if (!codeTable) {
    Serial.printf("[TinyGIF] LZW ERROR: Failed to allocate code table\n");
    free(codeTableBuffer);
    return false;
  }
  memset(codeTable, 0, MAX_CODE * sizeof(CodeEntry));

  uint16_t bufferUsed = 0;

  // Initialize code table with single bytes
  int nextCode = END_CODE + 1;
  for (int i = 0; i < CLEAR_CODE; i++) {
    if (bufferUsed + 1 > MAX_CODE_TABLE_SIZE) {
      Serial.printf("[TinyGIF] LZW: Code table buffer overflow\n");
      free(codeTable);
      free(codeTableBuffer);
      return false;
    }
    codeTable[i].offset = bufferUsed;
    codeTable[i].length = 1;
    codeTable[i].used = true;
    codeTableBuffer[bufferUsed++] = (uint8_t)i;
  }

  // Bit reading state
  uint32_t bitBuffer = 0;
  int bitsInBuffer = 0;
  uint8_t currentBlockSize = 0;
  int bytesRemainingInBlock = 0;

  // Function to read bits from the GIF sub-block stream
  auto readBits = [&](int numBits) -> int {
    // Validate requested bits
    if (numBits < 0 || numBits > 12) return -1;

    // Fill bit buffer as needed (limit to 24 bits before adding 8 more = 32 max)
    while (bitsInBuffer < numBits) {
      // Safety check to prevent overflow
      if (bitsInBuffer >= 24) {
        Serial.printf("[TinyGIF] LZW: Bit buffer overflow prevention (have %d, need %d)\n", bitsInBuffer, numBits);
        return -1;
      }

      // Check if we need to start a new block
      if (bytesRemainingInBlock == 0) {
        if (size == 0) return -1;
        currentBlockSize = *data++;
        size--;
        if (currentBlockSize == 0) return -1;  // End of data
        if (size < currentBlockSize) return -1;
        bytesRemainingInBlock = currentBlockSize;
      }

      // Read one byte from current block
      bitBuffer |= ((uint32_t)*data++) << bitsInBuffer;
      bitsInBuffer += 8;
      bytesRemainingInBlock--;
      size--;
    }

    // Extract the requested bits
    int result = bitBuffer & ((1 << numBits) - 1);
    bitBuffer >>= numBits;
    bitsInBuffer -= numBits;
    return result;
  };

  // Main decompression loop
  int prevCode = -1;
  bool firstCode = true;

  while (true) {
    int code = readBits(codeSize);
    if (code < 0) break;  // End of data

    if (code == END_CODE) break;

    if (code == CLEAR_CODE) {
      // Reset code table
      for (int i = END_CODE + 1; i < nextCode; i++) {
        codeTable[i].used = false;
      }
      nextCode = END_CODE + 1;
      codeSize = minCodeSize + 1;
      prevCode = -1;
      firstCode = true;
      bufferUsed = CLEAR_CODE;
      continue;
    }

    // Get current string data
    uint16_t currentOffset;
    uint16_t currentLength;

    if (code < 0 || code >= MAX_CODE) {
      Serial.printf("[TinyGIF] LZW ERROR: Code %d out of bounds\n", code);
      free(codeTable);
      free(codeTableBuffer);
      return false;
    }

    if (code < nextCode && codeTable[code].used) {
      currentOffset = codeTable[code].offset;
      currentLength = codeTable[code].length;
    } else if (code == nextCode && prevCode >= 0 && prevCode < MAX_CODE && codeTable[prevCode].used) {
      // Special case: KwKwK pattern
      currentOffset = codeTable[prevCode].offset;
      currentLength = codeTable[prevCode].length + 1;

      if (currentOffset >= bufferUsed || currentOffset + currentLength - 1 >= MAX_CODE_TABLE_SIZE) {
        Serial.printf("[TinyGIF] LZW ERROR: Buffer overflow\n");
        free(codeTable);
        free(codeTableBuffer);
        return false;
      }
    } else {
      Serial.printf("[TinyGIF] LZW ERROR: Invalid code %d\n", code);
      free(codeTable);
      free(codeTableBuffer);
      return false;
    }

    if (currentOffset >= bufferUsed) {
      Serial.printf("[TinyGIF] LZW ERROR: Invalid offset\n");
      free(codeTable);
      free(codeTableBuffer);
      return false;
    }

    // Output the decoded string
    if (outRemaining < currentLength) {
      Serial.printf("[TinyGIF] LZW ERROR: Output buffer overflow\n");
      free(codeTable);
      free(codeTableBuffer);
      return false;
    }

    if (code == nextCode && prevCode >= 0) {
      // Special case: KwKwK pattern
      if (currentOffset + currentLength - 1 > bufferUsed || codeTable[prevCode].offset >= bufferUsed) {
        Serial.printf("[TinyGIF] LZW ERROR: Invalid special case offset\n");
        free(codeTable);
        free(codeTableBuffer);
        return false;
      }
      memcpy(outPtr, codeTableBuffer + currentOffset, currentLength - 1);
      outPtr[currentLength - 1] = codeTableBuffer[codeTable[prevCode].offset];
    } else {
      // Normal case
      if (currentOffset + currentLength > bufferUsed) {
        Serial.printf("[TinyGIF] LZW ERROR: Source buffer overflow\n");
        free(codeTable);
        free(codeTableBuffer);
        return false;
      }
      memcpy(outPtr, codeTableBuffer + currentOffset, currentLength);
    }
    outPtr += currentLength;
    outRemaining -= currentLength;

    // Add new code to table
    if (!firstCode && prevCode >= 0 && prevCode < MAX_CODE && nextCode < MAX_CODE) {
      if (!codeTable[prevCode].used || codeTable[prevCode].offset + codeTable[prevCode].length > bufferUsed ||
          bufferUsed + codeTable[prevCode].length + 1 > MAX_CODE_TABLE_SIZE || currentOffset >= bufferUsed) {
        Serial.printf("[TinyGIF] LZW ERROR: Cannot add new table entry\n");
        free(codeTable);
        free(codeTableBuffer);
        return false;
      }

      uint16_t prevLength = codeTable[prevCode].length;
      uint16_t prevOffset = codeTable[prevCode].offset;

      memcpy(codeTableBuffer + bufferUsed, codeTableBuffer + prevOffset, prevLength);
      codeTableBuffer[bufferUsed + prevLength] = codeTableBuffer[currentOffset];

      codeTable[nextCode].offset = bufferUsed;
      codeTable[nextCode].length = prevLength + 1;
      codeTable[nextCode].used = true;
      bufferUsed += prevLength + 1;
      nextCode++;

      if (nextCode == (1 << codeSize) && codeSize < 12) {
        codeSize++;
      }
    }

    prevCode = code;
    firstCode = false;
  }

  bool success = outPtr > output;
  free(codeTable);
  free(codeTableBuffer);
  return success;
}