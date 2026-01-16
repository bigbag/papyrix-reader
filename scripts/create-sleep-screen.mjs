#!/usr/bin/env node
/**
 * Convert images to sleep screen format for Papyrix e-paper display.
 *
 * Converts PNG, JPG, BMP images to grayscale BMP format compatible
 * with the Papyrix firmware. Supports 2, 4, or 8 bit output depth.
 *
 * Usage:
 *   node create-sleep-screen.mjs <input> <output> [options]
 *
 * Examples:
 *   node create-sleep-screen.mjs photo.jpg sleep.bmp
 *   node create-sleep-screen.mjs photo.png sleep.bmp --dither --bits 8
 *   node create-sleep-screen.mjs photo.jpg sleep.bmp --orientation landscape
 */

import sharp from "sharp";
import fs from "node:fs";
import path from "node:path";
import { parseArgs } from "node:util";

// Display dimensions
const PORTRAIT_WIDTH = 480;
const PORTRAIT_HEIGHT = 800;
const LANDSCAPE_WIDTH = 800;
const LANDSCAPE_HEIGHT = 480;

/**
 * Create grayscale palette for given bit depth
 */
function createGrayscalePalette(bits) {
  const levels = 2 ** bits;
  const step = Math.floor(255 / (levels - 1));
  const palette = [];
  for (let i = 0; i < levels; i++) {
    const val = i * step;
    palette.push([val, val, val]);
  }
  return palette;
}

/**
 * Quantize grayscale value to target bit depth levels
 */
function quantizeToLevels(gray, bits) {
  const levels = 2 ** bits;
  return Math.min(levels - 1, Math.floor((gray * levels) / 256));
}

/**
 * Apply Floyd-Steinberg dithering to grayscale image
 */
function floydSteinbergDither(pixels, width, height, bits) {
  const result = new Uint8Array(pixels);
  const levels = 2 ** bits;
  const step = Math.floor(255 / (levels - 1));

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const idx = y * width + x;
      const oldPixel = result[idx];

      // Quantize
      const level = Math.min(levels - 1, Math.floor((oldPixel * levels) / 256));
      const newPixel = level * step;
      result[idx] = newPixel;

      // Calculate error
      const error = oldPixel - newPixel;

      // Distribute error to neighbors
      if (x + 1 < width) {
        result[idx + 1] = Math.max(0, Math.min(255, result[idx + 1] + Math.floor((error * 7) / 16)));
      }
      if (y + 1 < height) {
        if (x > 0) {
          result[idx + width - 1] = Math.max(0, Math.min(255, result[idx + width - 1] + Math.floor((error * 3) / 16)));
        }
        result[idx + width] = Math.max(0, Math.min(255, result[idx + width] + Math.floor((error * 5) / 16)));
        if (x + 1 < width) {
          result[idx + width + 1] = Math.max(0, Math.min(255, result[idx + width + 1] + Math.floor(error / 16)));
        }
      }
    }
  }

  return result;
}

/**
 * Write image as indexed grayscale BMP
 */
function writeBmp(pixels, width, height, outputPath, bits) {
  const levels = 2 ** bits;

  // Calculate row bytes with 4-byte alignment
  const pixelsPerByte = 8 / bits;
  const rowBytes = Math.ceil(width / pixelsPerByte);
  const rowBytesPadded = (rowBytes + 3) & ~3;

  // Palette size (4 bytes per color: B, G, R, reserved)
  const paletteSize = levels * 4;

  // File structure sizes
  const fileHeaderSize = 14;
  const dibHeaderSize = 40;
  const pixelDataOffset = fileHeaderSize + dibHeaderSize + paletteSize;
  const pixelDataSize = rowBytesPadded * height;
  const fileSize = pixelDataOffset + pixelDataSize;

  const buffer = Buffer.alloc(fileSize);
  let offset = 0;

  // BMP File Header (14 bytes)
  buffer.write("BM", offset);
  offset += 2;
  buffer.writeUInt32LE(fileSize, offset);
  offset += 4;
  buffer.writeUInt16LE(0, offset); // Reserved
  offset += 2;
  buffer.writeUInt16LE(0, offset); // Reserved
  offset += 2;
  buffer.writeUInt32LE(pixelDataOffset, offset);
  offset += 4;

  // DIB Header (BITMAPINFOHEADER - 40 bytes)
  buffer.writeUInt32LE(dibHeaderSize, offset);
  offset += 4;
  buffer.writeInt32LE(width, offset);
  offset += 4;
  buffer.writeInt32LE(height, offset); // Positive = bottom-up
  offset += 4;
  buffer.writeUInt16LE(1, offset); // Planes
  offset += 2;
  buffer.writeUInt16LE(bits, offset); // Bits per pixel
  offset += 2;
  buffer.writeUInt32LE(0, offset); // Compression (BI_RGB)
  offset += 4;
  buffer.writeUInt32LE(pixelDataSize, offset);
  offset += 4;
  buffer.writeInt32LE(2835, offset); // X pixels per meter (72 DPI)
  offset += 4;
  buffer.writeInt32LE(2835, offset); // Y pixels per meter (72 DPI)
  offset += 4;
  buffer.writeUInt32LE(levels, offset); // Colors used
  offset += 4;
  buffer.writeUInt32LE(levels, offset); // Important colors
  offset += 4;

  // Write grayscale palette
  const palette = createGrayscalePalette(bits);
  for (const [r, g, b] of palette) {
    buffer.writeUInt8(b, offset++); // Blue
    buffer.writeUInt8(g, offset++); // Green
    buffer.writeUInt8(r, offset++); // Red
    buffer.writeUInt8(0, offset++); // Reserved
  }

  // Write pixel data (bottom-up)
  for (let y = height - 1; y >= 0; y--) {
    const rowData = Buffer.alloc(rowBytesPadded);
    let byteIdx = 0;
    let bitPos = 8 - bits;

    for (let x = 0; x < width; x++) {
      const idx = y * width + x;
      const gray = pixels[idx];
      const level = quantizeToLevels(gray, bits);

      rowData[byteIdx] |= level << bitPos;
      bitPos -= bits;

      if (bitPos < 0) {
        byteIdx++;
        bitPos = 8 - bits;
      }
    }

    rowData.copy(buffer, offset);
    offset += rowBytesPadded;
  }

  fs.writeFileSync(outputPath, buffer);
}

/**
 * Resize image according to fit mode using sharp
 */
async function resizeImage(inputPath, targetWidth, targetHeight, fit) {
  let image = sharp(inputPath).grayscale();

  if (fit === "stretch") {
    return image.resize(targetWidth, targetHeight, { fit: "fill" }).raw().toBuffer();
  } else if (fit === "cover") {
    return image.resize(targetWidth, targetHeight, { fit: "cover" }).raw().toBuffer();
  } else {
    // contain (default) - fit within target, white background
    const metadata = await sharp(inputPath).metadata();
    const srcRatio = metadata.width / metadata.height;
    const dstRatio = targetWidth / targetHeight;

    let newWidth, newHeight;
    if (srcRatio > dstRatio) {
      newWidth = targetWidth;
      newHeight = Math.round(metadata.height * targetWidth / metadata.width);
    } else {
      newHeight = targetHeight;
      newWidth = Math.round(metadata.width * targetHeight / metadata.height);
    }

    const resized = await image
      .resize(newWidth, newHeight, { fit: "fill" })
      .raw()
      .toBuffer();

    // Create white background and paste centered
    const result = Buffer.alloc(targetWidth * targetHeight, 255);
    const xOffset = Math.floor((targetWidth - newWidth) / 2);
    const yOffset = Math.floor((targetHeight - newHeight) / 2);

    for (let y = 0; y < newHeight; y++) {
      for (let x = 0; x < newWidth; x++) {
        const srcIdx = y * newWidth + x;
        const dstIdx = (y + yOffset) * targetWidth + (x + xOffset);
        result[dstIdx] = resized[srcIdx];
      }
    }

    return result;
  }
}

async function convertImage(inputPath, outputPath, orientation, bits, dither, fit) {
  // Determine target dimensions
  let targetWidth, targetHeight;
  if (orientation === "landscape") {
    targetWidth = LANDSCAPE_WIDTH;
    targetHeight = LANDSCAPE_HEIGHT;
  } else {
    targetWidth = PORTRAIT_WIDTH;
    targetHeight = PORTRAIT_HEIGHT;
  }

  // Resize to target dimensions
  let pixels = await resizeImage(inputPath, targetWidth, targetHeight, fit);

  // Apply dithering if requested
  if (dither) {
    pixels = floydSteinbergDither(pixels, targetWidth, targetHeight, bits);
  }

  // Write output BMP
  writeBmp(pixels, targetWidth, targetHeight, outputPath, bits);

  console.log(`Created: ${outputPath}`);
  console.log(`  Size: ${targetWidth}x${targetHeight}`);
  console.log(`  Depth: ${bits}-bit (${2 ** bits} levels)`);
  console.log(`  Dithering: ${dither ? "enabled" : "disabled"}`);
}

async function main() {
  const { values, positionals } = parseArgs({
    allowPositionals: true,
    options: {
      orientation: { type: "string", default: "portrait" },
      bits: { type: "string", default: "4" },
      dither: { type: "boolean", default: false },
      fit: { type: "string", default: "contain" },
      help: { type: "boolean", short: "h", default: false },
    },
  });

  if (values.help || positionals.length < 2) {
    console.log(`
Convert images to Papyrix sleep screen format

Usage:
  node create-sleep-screen.mjs <input> <output> [options]

Arguments:
  input     Input image (PNG, JPG, BMP)
  output    Output BMP file

Options:
  --orientation <mode>  Screen orientation: portrait, landscape (default: portrait)
  --bits <n>            Output bit depth: 2, 4, 8 (default: 4)
  --dither              Enable Floyd-Steinberg dithering
  --fit <mode>          Resize mode: contain, cover, stretch (default: contain)
  -h, --help            Show this help message

Examples:
  node create-sleep-screen.mjs photo.jpg sleep.bmp
  node create-sleep-screen.mjs photo.png sleep.bmp --dither
  node create-sleep-screen.mjs photo.jpg sleep.bmp --bits 8 --orientation landscape
`);
    process.exit(positionals.length < 2 && !values.help ? 1 : 0);
  }

  const inputPath = positionals[0];
  const outputPath = positionals[1];
  const bits = parseInt(values.bits, 10);

  if (!fs.existsSync(inputPath)) {
    console.error(`Error: Input file not found: ${inputPath}`);
    process.exit(1);
  }

  if (![2, 4, 8].includes(bits)) {
    console.error("Error: Bits must be 2, 4, or 8");
    process.exit(1);
  }

  if (!["portrait", "landscape"].includes(values.orientation)) {
    console.error("Error: Orientation must be portrait or landscape");
    process.exit(1);
  }

  if (!["contain", "cover", "stretch"].includes(values.fit)) {
    console.error("Error: Fit must be contain, cover, or stretch");
    process.exit(1);
  }

  try {
    await convertImage(inputPath, outputPath, values.orientation, bits, values.dither, values.fit);
  } catch (error) {
    console.error(`Error: ${error.message}`);
    process.exit(1);
  }
}

main();
