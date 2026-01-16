#!/usr/bin/env node
/**
 * Convert image to C header byte array for firmware logo.
 *
 * Outputs a 128x128 monochrome bitmap as a C uint8_t array.
 *
 * Usage:
 *   node convert-logo.mjs <input_image> [output_header]
 *   node convert-logo.mjs logo.png
 *   node convert-logo.mjs logo.png --invert --threshold 100
 */

import sharp from "sharp";
import fs from "node:fs";
import path from "node:path";
import { parseArgs } from "node:util";

const LOGO_SIZE = 128;

async function convertToLogo(inputPath, outputPath, invert, threshold, rotate) {
  // Load and process image
  let image = sharp(inputPath);

  // Apply rotation if specified
  if (rotate !== 0) {
    image = image.rotate(rotate);
  }

  // Get metadata for aspect ratio calculation
  const metadata = await image.metadata();
  const srcRatio = metadata.width / metadata.height;

  let newWidth, newHeight;
  if (srcRatio > 1) {
    // Wider than tall
    newWidth = LOGO_SIZE;
    newHeight = Math.round(LOGO_SIZE / srcRatio);
  } else {
    // Taller than wide (or square)
    newHeight = LOGO_SIZE;
    newWidth = Math.round(LOGO_SIZE * srcRatio);
  }

  // Resize maintaining aspect ratio
  const resized = await image
    .resize(newWidth, newHeight, { fit: "fill" })
    .grayscale()
    .raw()
    .toBuffer();

  // Create white background and center the image
  const result = Buffer.alloc(LOGO_SIZE * LOGO_SIZE, 255);
  const xOffset = Math.floor((LOGO_SIZE - newWidth) / 2);
  const yOffset = Math.floor((LOGO_SIZE - newHeight) / 2);

  for (let y = 0; y < newHeight; y++) {
    for (let x = 0; x < newWidth; x++) {
      const srcIdx = y * newWidth + x;
      const dstIdx = (y + yOffset) * LOGO_SIZE + (x + xOffset);
      result[dstIdx] = resized[srcIdx];
    }
  }

  // Convert to binary (1-bit) byte array
  const bytesData = [];
  for (let row = 0; row < LOGO_SIZE; row++) {
    for (let byteCol = 0; byteCol < LOGO_SIZE / 8; byteCol++) {
      let byteVal = 0;
      for (let bit = 0; bit < 8; bit++) {
        const pixelIdx = row * LOGO_SIZE + byteCol * 8 + bit;
        const gray = result[pixelIdx];

        // Threshold: white pixels become 1 (0xFF), black become 0
        let isWhite;
        if (invert) {
          isWhite = gray < threshold;
        } else {
          isWhite = gray >= threshold;
        }

        if (isWhite) {
          byteVal |= 1 << (7 - bit);
        }
      }
      bytesData.push(byteVal);
    }
  }

  // Write C header file
  let output = "#pragma once\n";
  output += "#include <cstdint>\n";
  output += "\n";
  output += "static const uint8_t PapyrixLogo[] = {\n";

  // Write bytes, 19 per line to match existing style
  for (let i = 0; i < bytesData.length; i++) {
    if (i % 19 === 0) {
      output += "    ";
    }
    output += `0x${bytesData[i].toString(16).toUpperCase().padStart(2, "0")}`;
    if (i < bytesData.length - 1) {
      output += ", ";
    }
    if ((i + 1) % 19 === 0) {
      output += "\n";
    }
  }

  if (bytesData.length % 19 !== 0) {
    output += "\n";
  }

  output += "};\n";

  // Ensure output directory exists
  const dir = path.dirname(outputPath);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }

  fs.writeFileSync(outputPath, output);

  console.log(`Created: ${outputPath}`);
  console.log(`  Size: ${LOGO_SIZE}x${LOGO_SIZE}`);
  console.log(`  Bytes: ${bytesData.length}`);
}

async function main() {
  const { values, positionals } = parseArgs({
    allowPositionals: true,
    options: {
      invert: { type: "boolean", default: false },
      threshold: { type: "string", default: "128" },
      rotate: { type: "string", default: "0" },
      help: { type: "boolean", short: "h", default: false },
    },
  });

  if (values.help || positionals.length === 0) {
    console.log(`
Convert image to C header logo format (128x128 monochrome)

Usage:
  node convert-logo.mjs <input> [output] [options]

Arguments:
  input     Input image (PNG, JPG, etc.)
  output    Output header file (default: src/images/PapyrixLogo.h)

Options:
  --invert           Invert colors (black becomes white)
  --threshold <n>    Threshold for black/white (0-255, default: 128)
  --rotate <deg>     Rotate image clockwise (0, 90, 180, 270)
  -h, --help         Show this help message

Examples:
  node convert-logo.mjs logo.png
  node convert-logo.mjs logo.png src/images/MyLogo.h
  node convert-logo.mjs logo.png --invert --threshold 100
`);
    process.exit(0);
  }

  const inputPath = positionals[0];
  const outputPath = positionals[1] || "../src/images/PapyrixLogo.h";
  const threshold = parseInt(values.threshold, 10);
  const rotate = parseInt(values.rotate, 10);

  if (!fs.existsSync(inputPath)) {
    console.error(`Error: Input file not found: ${inputPath}`);
    process.exit(1);
  }

  if (![0, 90, 180, 270].includes(rotate)) {
    console.error("Error: Rotate must be 0, 90, 180, or 270");
    process.exit(1);
  }

  try {
    await convertToLogo(inputPath, outputPath, values.invert, threshold, rotate);
  } catch (error) {
    console.error(`Error: ${error.message}`);
    process.exit(1);
  }
}

main();
