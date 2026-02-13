#pragma once

#include <Print.h>

#include <functional>

/**
 * @brief Lightweight GIF decoder for extracting the first frame
 *
 * TinyGifDecoder is a minimal GIF decoder designed for resource-constrained
 * embedded systems. It extracts only the first frame of a GIF file and converts
 * it to 24-bit BMP format.
 *
 * Features:
 * - LZW decompression with dynamic memory allocation
 * - Supports GIF87a and GIF89a formats
 * - Handles global and local color tables
 * - Proper BMP row padding (4-byte alignment)
 * - Memory-efficient (scales buffer size based on image dimensions)
 *
 * Limitations:
 * - First frame only (no animation support)
 * - No transparency support (transparent pixels rendered as white)
 * - No interlacing support: All GIF data is read sequentially regardless of the
 *   interlace flag. If a GIF file contains truly interlaced data (4-pass format),
 *   the output will have scrambled rows. Modern GIFs rarely use interlacing as it
 *   was mainly for progressive loading over dial-up connections.
 * - Maximum image size: 4096x4096 pixels
 * - Maximum file size: ~200KB
 */
class TinyGifDecoder {
 public:
  /**
   * @brief Decode a GIF file to 24-bit BMP format
   *
   * Decodes the first frame of a GIF file and writes it as a 24-bit BMP
   * to the provided Print stream. The BMP includes proper row padding for
   * 4-byte alignment.
   *
   * @param gifData Pointer to GIF file data in memory
   * @param gifSize Size of GIF data in bytes
   * @param output Print stream to write BMP data to
   * @param maxWidth Maximum allowed image width (default 480)
   * @param maxHeight Maximum allowed image height (default 800)
   * @param shouldAbort Optional callback to check if decoding should abort
   * @return true if decoding succeeds, false on error
   *
   * @note The output BMP is written in bottom-up format (standard BMP)
   * @note Row padding is automatically added for proper BMP alignment
   * @note GIF interlace flag is ignored - all data read sequentially
   * @note Only ERROR messages are printed to Serial on failure
   */
  static bool decodeGifToBmp(const uint8_t* gifData, size_t gifSize, Print& output, int maxWidth = 480,
                             int maxHeight = 800, std::function<bool()> shouldAbort = nullptr);

 private:
  // GIF file format structures
  struct GifHeader {
    char signature[6];  // "GIF87a" or "GIF89a"
  };

  struct LogicalScreenDescriptor {
    uint16_t width;            // Canvas width
    uint16_t height;           // Canvas height
    uint8_t flags;             // Packed fields (color table, resolution, etc.)
    uint8_t bgColorIndex;      // Background color index
    uint8_t pixelAspectRatio;  // Pixel aspect ratio
  };

  struct ImageDescriptor {
    uint16_t left;    // X position of image on canvas
    uint16_t top;     // Y position of image on canvas
    uint16_t width;   // Image width
    uint16_t height;  // Image height
    uint8_t flags;    // Packed fields (local color table, interlace, etc.)
  };

  // Internal helper functions

  /** @brief Parse GIF file header ("GIF87a" or "GIF89a") */
  static bool parseHeader(const uint8_t*& data, size_t& size);

  /** @brief Parse logical screen descriptor (canvas size, flags) */
  static bool parseLogicalScreen(const uint8_t*& data, size_t& size, LogicalScreenDescriptor& lsd);

  /** @brief Parse image descriptor (position, size, flags) */
  static bool parseImageDescriptor(const uint8_t*& data, size_t& size, ImageDescriptor& imgDesc);

  /** @brief Skip GIF extension blocks */
  static bool skipExtensions(const uint8_t*& data, size_t& size);

  /**
   * @brief Decompress LZW-compressed GIF image data
   *
   * Implements LZW decompression for GIF format, handling variable-width
   * codes and GIF-specific sub-block structure. Uses dynamic memory
   * allocation scaled to image size.
   *
   * @param compressedData LZW compressed data (in GIF sub-blocks)
   * @param compressedSize Size of compressed data
   * @param output Buffer to write decompressed palette indices
   * @param outputSize Expected output size (width * height)
   * @param width Image width (for validation)
   * @param height Image height (for validation)
   * @param colorTable RGB color palette (3 bytes per entry)
   * @param colorTableSize Number of palette entries
   * @param minCodeSize Minimum LZW code size (from GIF)
   * @return true on success, false on error
   */
  static bool decompressLZW(const uint8_t* compressedData, size_t compressedSize, uint8_t* output, size_t outputSize,
                            int width, int height, const uint8_t* colorTable, int colorTableSize, uint8_t minCodeSize);

  /**
   * @brief Write BMP file header with proper row padding calculation
   *
   * Generates a 24-bit BMP header with correct file size including
   * row padding bytes for 4-byte alignment.
   *
   * @param output Print stream to write header to
   * @param width Image width
   * @param height Image height
   */
  static void writeBmpHeader(Print& output, int width, int height);

  /** @brief Convert RGB565 to RGB888 (unused, kept for compatibility) */
  static void rgb565ToRgb888(uint16_t rgb565, uint8_t& r, uint8_t& g, uint8_t& b);
};