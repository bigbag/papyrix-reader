#!/usr/bin/env node
/**
 * Convert TTF/OTF fonts to Papyrix binary format (.epdfont)
 *
 * Creates a font family directory with all style variants for use with Papyrix themes.
 *
 * Usage:
 *   node convert-fonts.mjs my-font -r Regular.ttf -b Bold.ttf -i Italic.ttf
 *   node convert-fonts.mjs my-font -r Regular.ttf --size 16
 *   node convert-fonts.mjs my-font -r Regular.ttf -o /path/to/sdcard/fonts/
 *
 * Requirements:
 *   npm install (installs opentype.js)
 */

import opentype from "opentype.js";
import fs from "node:fs";
import path from "node:path";
import { parseArgs } from "node:util";

// Binary format constants
const MAGIC = 0x46445045; // "EPDF" in little-endian
const VERSION = 1;
const DPI = 150;

// Unicode intervals - base set (Latin, Cyrillic, punctuation)
const INTERVALS_BASE = [
  // Basic Latin (ASCII)
  [0x0000, 0x007f],
  // Latin-1 Supplement
  [0x0080, 0x00ff],
  // Latin Extended-A
  [0x0100, 0x017f],
  // General Punctuation
  [0x2000, 0x206f],
  // Dashes, quotes, prime marks
  [0x2010, 0x203a],
  // Misc punctuation
  [0x2040, 0x205f],
  // Currency symbols
  [0x20a0, 0x20cf],
  // Combining Diacritical Marks
  [0x0300, 0x036f],
  // Cyrillic
  [0x0400, 0x04ff],
  // Math operators
  [0x2200, 0x22ff],
  // Arrows
  [0x2190, 0x21ff],
];

// CJK intervals - full set (~32,000 chars, results in large files ~4MB+)
const INTERVALS_CJK_FULL = [
  // CJK Symbols and Punctuation
  [0x3000, 0x303f],
  // Hiragana (Japanese)
  [0x3040, 0x309f],
  // Katakana (Japanese)
  [0x30a0, 0x30ff],
  // CJK Unified Ideographs - FULL (20,992 chars)
  [0x4e00, 0x9fff],
  // Hangul Jamo (Korean)
  [0x1100, 0x11ff],
  // Hangul Compatibility Jamo (Korean)
  [0x3130, 0x318f],
  // Hangul Syllables - FULL (11,172 chars)
  [0xac00, 0xd7af],
  // Halfwidth and Fullwidth Forms
  [0xff00, 0xffef],
];

// CJK intervals - full CJK and Hangul, reduced fullwidth forms
const INTERVALS_CJK_COMMON = [
  // CJK Symbols and Punctuation
  [0x3000, 0x303f],
  // Hiragana (Japanese)
  [0x3040, 0x309f],
  // Katakana (Japanese)
  [0x30a0, 0x30ff],
  // CJK Unified Ideographs - Full range (20,992 chars)
  [0x4e00, 0x9fff],
  // Hangul Jamo (Korean)
  [0x1100, 0x11ff],
  // Hangul Compatibility Jamo (Korean)
  [0x3130, 0x318f],
  // Hangul Syllables - Full range (11,172 chars)
  [0xac00, 0xd7af],
  // Fullwidth ASCII and punctuation
  [0xff00, 0xff5f],
];

/**
 * Simple scanline rasterizer for opentype.js paths
 * Renders glyph to 8-bit grayscale with 4x supersampling
 */
class GlyphRasterizer {
  constructor(font, fontSize) {
    this.font = font;
    this.fontSize = fontSize;
    this.scale = (fontSize * DPI) / (72 * font.unitsPerEm);
  }

  /**
   * Render a glyph to 8-bit grayscale bitmap
   */
  renderGlyph(codePoint) {
    const char = String.fromCodePoint(codePoint);
    const glyphIndex = this.font.charToGlyphIndex(char);
    if (glyphIndex === 0 && codePoint !== 0) {
      return null; // Glyph not found
    }

    const glyph = this.font.glyphs.get(glyphIndex);
    if (!glyph) return null;

    // Get glyph metrics
    const advanceWidth = Math.round(glyph.advanceWidth * this.scale);

    // Get bounding box
    const bbox = glyph.getBoundingBox();
    if (!bbox || (bbox.x1 === 0 && bbox.y1 === 0 && bbox.x2 === 0 && bbox.y2 === 0)) {
      // Empty glyph (space, etc.)
      return {
        width: 0,
        height: 0,
        advanceX: advanceWidth,
        left: 0,
        top: 0,
        data: Buffer.alloc(0),
      };
    }

    // Scale bounding box
    const x1 = Math.floor(bbox.x1 * this.scale);
    const y1 = Math.floor(bbox.y1 * this.scale);
    const x2 = Math.ceil(bbox.x2 * this.scale);
    const y2 = Math.ceil(bbox.y2 * this.scale);

    const width = Math.max(1, x2 - x1);
    const height = Math.max(1, y2 - y1);

    // 4x supersampling
    const ssScale = 4;
    const ssWidth = width * ssScale;
    const ssHeight = height * ssScale;
    const ssBuffer = new Uint8Array(ssWidth * ssHeight);

    // Get path commands
    const path = glyph.getPath(0, 0, this.fontSize * DPI / 72);

    // Rasterize with edge-based coverage
    this.rasterizePath(path, ssBuffer, ssWidth, ssHeight, -x1 * ssScale, y2 * ssScale);

    // Downsample to final resolution
    const buffer = new Uint8Array(width * height);
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        let sum = 0;
        for (let sy = 0; sy < ssScale; sy++) {
          for (let sx = 0; sx < ssScale; sx++) {
            sum += ssBuffer[(y * ssScale + sy) * ssWidth + (x * ssScale + sx)];
          }
        }
        buffer[y * width + x] = Math.min(255, Math.round(sum / (ssScale * ssScale)));
      }
    }

    return {
      width,
      height,
      advanceX: advanceWidth,
      left: x1,
      top: y2, // baseline-relative top
      data: Buffer.from(buffer),
    };
  }

  /**
   * Rasterize an opentype.js path to a buffer using scanline fill
   */
  rasterizePath(path, buffer, width, height, offsetX, offsetY) {
    // Build edge list from path commands
    const edges = [];
    let currentX = 0;
    let currentY = 0;
    let startX = 0;
    let startY = 0;

    for (const cmd of path.commands) {
      switch (cmd.type) {
        case "M":
          currentX = cmd.x + offsetX;
          currentY = offsetY - cmd.y;
          startX = currentX;
          startY = currentY;
          break;

        case "L":
          {
            const x = cmd.x + offsetX;
            const y = offsetY - cmd.y;
            this.addEdge(edges, currentX, currentY, x, y);
            currentX = x;
            currentY = y;
          }
          break;

        case "Q":
          {
            const x = cmd.x + offsetX;
            const y = offsetY - cmd.y;
            const x1 = cmd.x1 + offsetX;
            const y1 = offsetY - cmd.y1;
            this.addQuadraticCurve(edges, currentX, currentY, x1, y1, x, y);
            currentX = x;
            currentY = y;
          }
          break;

        case "C":
          {
            const x = cmd.x + offsetX;
            const y = offsetY - cmd.y;
            const x1 = cmd.x1 + offsetX;
            const y1 = offsetY - cmd.y1;
            const x2 = cmd.x2 + offsetX;
            const y2 = offsetY - cmd.y2;
            this.addCubicCurve(edges, currentX, currentY, x1, y1, x2, y2, x, y);
            currentX = x;
            currentY = y;
          }
          break;

        case "Z":
          this.addEdge(edges, currentX, currentY, startX, startY);
          currentX = startX;
          currentY = startY;
          break;
      }
    }

    // Sort edges by y-min
    edges.sort((a, b) => a.yMin - b.yMin);

    // Scanline fill
    for (let y = 0; y < height; y++) {
      const scanY = y + 0.5;
      const intersections = [];

      for (const edge of edges) {
        if (edge.yMin <= scanY && edge.yMax > scanY) {
          const x = edge.x1 + ((scanY - edge.y1) * (edge.x2 - edge.x1)) / (edge.y2 - edge.y1);
          intersections.push(x);
        }
      }

      intersections.sort((a, b) => a - b);

      // Fill between pairs of intersections
      for (let i = 0; i < intersections.length - 1; i += 2) {
        const xStart = Math.max(0, Math.floor(intersections[i]));
        const xEnd = Math.min(width, Math.ceil(intersections[i + 1]));
        for (let x = xStart; x < xEnd; x++) {
          buffer[y * width + x] = 255;
        }
      }
    }
  }

  addEdge(edges, x1, y1, x2, y2) {
    if (Math.abs(y2 - y1) < 0.001) return; // Skip horizontal edges

    // Ensure y1 < y2
    if (y1 > y2) {
      [x1, x2] = [x2, x1];
      [y1, y2] = [y2, y1];
    }

    edges.push({
      x1,
      y1,
      x2,
      y2,
      yMin: y1,
      yMax: y2,
    });
  }

  addQuadraticCurve(edges, x0, y0, x1, y1, x2, y2) {
    // Approximate with line segments
    const steps = 8;
    let px = x0;
    let py = y0;

    for (let i = 1; i <= steps; i++) {
      const t = i / steps;
      const mt = 1 - t;
      const x = mt * mt * x0 + 2 * mt * t * x1 + t * t * x2;
      const y = mt * mt * y0 + 2 * mt * t * y1 + t * t * y2;
      this.addEdge(edges, px, py, x, y);
      px = x;
      py = y;
    }
  }

  addCubicCurve(edges, x0, y0, x1, y1, x2, y2, x3, y3) {
    // Approximate with line segments
    const steps = 12;
    let px = x0;
    let py = y0;

    for (let i = 1; i <= steps; i++) {
      const t = i / steps;
      const mt = 1 - t;
      const x =
        mt * mt * mt * x0 +
        3 * mt * mt * t * x1 +
        3 * mt * t * t * x2 +
        t * t * t * x3;
      const y =
        mt * mt * mt * y0 +
        3 * mt * mt * t * y1 +
        3 * mt * t * t * y2 +
        t * t * t * y3;
      this.addEdge(edges, px, py, x, y);
      px = x;
      py = y;
    }
  }
}

/**
 * Convert 8-bit grayscale to packed 4-bit
 */
function to4BitGrayscale(data, width, height) {
  const result = [];
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x += 2) {
      const i = y * width + x;
      const low = data[i] >> 4;
      const high = x + 1 < width ? (data[i + 1] >> 4) << 4 : 0;
      result.push(low | high);
    }
  }
  return Buffer.from(result);
}

/**
 * Quantize a 4-bit pixel to 2-bit or 1-bit
 */
function quantizePixel(value, is2Bit) {
  if (is2Bit) {
    if (value >= 12) return 3;
    if (value >= 8) return 2;
    if (value >= 4) return 1;
    return 0;
  }
  return value >= 2 ? 1 : 0;
}

/**
 * Get a 4-bit pixel from packed buffer
 */
function getPixel4Bit(pixels4g, pitch, x, y) {
  if (pitch === 0 || !pixels4g.length) return 0;
  const idx = y * pitch + Math.floor(x / 2);
  if (idx >= pixels4g.length) return 0;
  const byte = pixels4g[idx];
  return (byte >> ((x % 2) * 4)) & 0xf;
}

/**
 * Downsample 4-bit grayscale to 2-bit or 1-bit packed bitmap
 */
function renderDownsampled(data, width, height, is2Bit) {
  const pixels4g = to4BitGrayscale(data, width, height);
  const pitch = Math.ceil(width / 2);
  const bitsPerPixel = is2Bit ? 2 : 1;
  const pixelsPerByte = 8 / bitsPerPixel;
  const totalPixels = width * height;

  const pixels = [];
  let px = 0;

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      px = (px << bitsPerPixel) | quantizePixel(getPixel4Bit(pixels4g, pitch, x, y), is2Bit);
      const pixelIdx = y * width + x;
      if (pixelIdx % pixelsPerByte === pixelsPerByte - 1) {
        pixels.push(px);
        px = 0;
      }
    }
  }

  const remainder = totalPixels % pixelsPerByte;
  if (remainder !== 0) {
    px <<= (pixelsPerByte - remainder) * bitsPerPixel;
    pixels.push(px);
  }

  return Buffer.from(pixels);
}

/**
 * Validate intervals to only include code points present in font
 */
function validateIntervals(font, intervals) {
  const validIntervals = [];

  for (const [iStart, iEnd] of intervals) {
    let start = iStart;
    for (let codePoint = iStart; codePoint <= iEnd; codePoint++) {
      const glyphIndex = font.charToGlyphIndex(String.fromCodePoint(codePoint));
      if (glyphIndex === 0 && codePoint !== 0) {
        if (start < codePoint) {
          validIntervals.push([start, codePoint - 1]);
        }
        start = codePoint + 1;
      }
    }
    if (start <= iEnd) {
      validIntervals.push([start, iEnd]);
    }
  }

  return validIntervals;
}

/**
 * Convert a single font file to .epdfont binary format
 */
function convertFont(fontPath, outputPath, size, is2Bit, intervals) {
  if (!fs.existsSync(fontPath)) {
    console.error(`  Warning: Font file not found: ${fontPath}`);
    return false;
  }

  console.log(`  Converting: ${path.basename(fontPath)} -> ${path.basename(outputPath)}`);

  try {
    const font = opentype.loadSync(fontPath);
    const rasterizer = new GlyphRasterizer(font, size);

    // Validate intervals
    const validIntervals = validateIntervals(
      font,
      [...intervals].sort((a, b) => a[0] - b[0])
    );
    if (!validIntervals.length) {
      console.error("  Error: No valid glyphs found");
      return false;
    }

    // Render all glyphs
    const allGlyphs = [];
    let totalBitmapSize = 0;

    let totalGlyphs = 0;
    for (const [iStart, iEnd] of validIntervals) {
      totalGlyphs += iEnd - iStart + 1;
    }

    let processed = 0;
    let lastPercent = -1;

    for (const [iStart, iEnd] of validIntervals) {
      for (let codePoint = iStart; codePoint <= iEnd; codePoint++) {
        const glyph = rasterizer.renderGlyph(codePoint);
        if (!glyph) continue;

        const pixelData =
          glyph.width > 0 && glyph.height > 0
            ? renderDownsampled(glyph.data, glyph.width, glyph.height, is2Bit)
            : Buffer.alloc(0);

        allGlyphs.push({
          width: glyph.width,
          height: glyph.height,
          advanceX: glyph.advanceX,
          left: glyph.left,
          top: glyph.top,
          dataLength: pixelData.length,
          dataOffset: totalBitmapSize,
          codePoint,
          pixelData,
        });

        totalBitmapSize += pixelData.length;
        processed++;

        // Progress indicator
        const percent = Math.floor((processed / totalGlyphs) * 100);
        if (percent !== lastPercent && percent % 10 === 0) {
          process.stdout.write(`\r  Converting: ${path.basename(fontPath)} -> ${path.basename(outputPath)} (${percent}%)`);
          lastPercent = percent;
        }
      }
    }
    process.stdout.write("\r" + " ".repeat(80) + "\r");

    // Get font metrics
    const scale = (size * DPI) / (72 * font.unitsPerEm);
    const advanceY = Math.ceil((font.ascender - font.descender) * scale);
    const ascender = Math.ceil(font.ascender * scale);
    const descender = Math.floor(font.descender * scale);

    // Build binary file
    const dir = path.dirname(outputPath);
    if (!fs.existsSync(dir)) {
      fs.mkdirSync(dir, { recursive: true });
    }

    const headerSize = 16;
    const metricsSize = 18;
    const intervalsSize = validIntervals.length * 12;
    const glyphsSize = allGlyphs.length * 14;
    const totalSize = headerSize + metricsSize + intervalsSize + glyphsSize + totalBitmapSize;

    const buffer = Buffer.alloc(totalSize);
    let offset = 0;

    // Header: magic(4) + version(2) + flags(2) + reserved(8) = 16 bytes
    const flags = is2Bit ? 0x01 : 0x00;
    buffer.writeUInt32LE(MAGIC, offset);
    offset += 4;
    buffer.writeUInt16LE(VERSION, offset);
    offset += 2;
    buffer.writeUInt16LE(flags, offset);
    offset += 2;
    offset += 8; // reserved

    // Metrics: advanceY(1) + pad(1) + ascender(2) + descender(2) + counts(12) = 18 bytes
    buffer.writeUInt8(advanceY & 0xff, offset);
    offset += 1;
    buffer.writeUInt8(0, offset); // pad
    offset += 1;
    buffer.writeInt16LE(ascender, offset);
    offset += 2;
    buffer.writeInt16LE(descender, offset);
    offset += 2;
    buffer.writeUInt32LE(validIntervals.length, offset);
    offset += 4;
    buffer.writeUInt32LE(allGlyphs.length, offset);
    offset += 4;
    buffer.writeUInt32LE(totalBitmapSize, offset);
    offset += 4;

    // Intervals: first(4) + last(4) + offset(4) = 12 bytes each
    let glyphOffset = 0;
    for (const [iStart, iEnd] of validIntervals) {
      buffer.writeUInt32LE(iStart, offset);
      offset += 4;
      buffer.writeUInt32LE(iEnd, offset);
      offset += 4;
      buffer.writeUInt32LE(glyphOffset, offset);
      offset += 4;
      glyphOffset += iEnd - iStart + 1;
    }

    // Glyphs: w(1) + h(1) + adv(1) + pad(1) + left(2) + top(2) + len(2) + off(4) = 14 bytes
    for (const glyph of allGlyphs) {
      buffer.writeUInt8(glyph.width, offset);
      offset += 1;
      buffer.writeUInt8(glyph.height, offset);
      offset += 1;
      buffer.writeUInt8(glyph.advanceX & 0xff, offset);
      offset += 1;
      buffer.writeUInt8(0, offset); // pad
      offset += 1;
      buffer.writeInt16LE(glyph.left, offset);
      offset += 2;
      buffer.writeInt16LE(glyph.top, offset);
      offset += 2;
      buffer.writeUInt16LE(glyph.dataLength, offset);
      offset += 2;
      buffer.writeUInt32LE(glyph.dataOffset, offset);
      offset += 4;
    }

    // Bitmap data
    for (const glyph of allGlyphs) {
      glyph.pixelData.copy(buffer, offset);
      offset += glyph.pixelData.length;
    }

    fs.writeFileSync(outputPath, buffer);
    console.log(`  Created: ${outputPath} (${buffer.length} bytes)`);
    return true;
  } catch (error) {
    console.error(`  Error: ${error.message}`);
    return false;
  }
}

function main() {
  const { values, positionals } = parseArgs({
    allowPositionals: true,
    options: {
      regular: { type: "string", short: "r" },
      bold: { type: "string", short: "b" },
      italic: { type: "string", short: "i" },
      output: { type: "string", short: "o", default: "." },
      size: { type: "string", short: "s", default: "16" },
      "2bit": { type: "boolean", default: false },
      "all-sizes": { type: "boolean", default: false },
      "cjk-common": { type: "boolean", default: false },
      help: { type: "boolean", short: "h", default: false },
    },
  });

  if (values.help || positionals.length === 0) {
    console.log(`
Convert TTF/OTF fonts to Papyrix binary format (.epdfont)

Usage:
  node convert-fonts.mjs <family-name> -r <regular.ttf> [options]

Arguments:
  family-name    Font family name (used as directory name)

Options:
  -r, --regular  Path to regular style TTF/OTF file (required)
  -b, --bold     Path to bold style TTF/OTF file
  -i, --italic   Path to italic style TTF/OTF file
  -o, --output   Output directory (default: current directory)
  -s, --size     Font size in points (default: 16)
  --2bit         Generate 2-bit grayscale (smoother but larger)
  --all-sizes    Generate all reader sizes (14, 16, 18pt)
  --cjk-common   Use full CJK (20,992) + Hangul (11,172), reduced fullwidth
  -h, --help     Show this help message

Examples:
  # Convert a complete font family
  node convert-fonts.mjs my-font -r MyFont-Regular.ttf -b MyFont-Bold.ttf -i MyFont-Italic.ttf

  # Convert only regular style
  node convert-fonts.mjs my-font -r MyFont-Regular.ttf

  # Specify font size (default: 16)
  node convert-fonts.mjs my-font -r MyFont-Regular.ttf --size 14

  # Output to SD card
  node convert-fonts.mjs my-font -r MyFont-Regular.ttf -o /Volumes/SDCARD/config/fonts/

  # Use 2-bit grayscale for smoother rendering
  node convert-fonts.mjs my-font -r MyFont-Regular.ttf --2bit

  # Generate CJK fonts for all reader sizes
  node convert-fonts.mjs noto-sans-jp -r NotoSansJP-Regular.ttf --all-sizes --cjk-common

Output structure:
  <output>/
  └── <family-name>/
      ├── regular.epdfont
      ├── bold.epdfont
      └── italic.epdfont
`);
    process.exit(0);
  }

  const family = positionals[0];
  if (!values.regular) {
    console.error("Error: Regular font (-r) is required");
    process.exit(1);
  }

  // Select interval set
  let intervals = [...INTERVALS_BASE];
  if (values["cjk-common"]) {
    intervals = [...INTERVALS_BASE, ...INTERVALS_CJK_COMMON];
    console.log("Using full CJK (20,992 chars) + full Hangul (11,172 chars)");
  } else {
    intervals = [...INTERVALS_BASE, ...INTERVALS_CJK_FULL];
  }

  const outputBase = values.output;
  const is2Bit = values["2bit"];
  const baseSize = parseInt(values.size, 10);

  console.log(`Converting font family: ${family}`);
  console.log(`Output directory: ${outputBase}`);
  console.log(`Font size: ${baseSize}pt`);
  if (is2Bit) {
    console.log("Mode: 2-bit grayscale");
  }
  console.log();

  const styles = [
    ["regular", values.regular],
    ["bold", values.bold],
    ["italic", values.italic],
  ];

  const sizes = values["all-sizes"] ? [14, 16, 18] : [baseSize];
  let successCount = 0;
  let totalCount = 0;

  for (const size of sizes) {
    const familyDir = values["all-sizes"]
      ? path.join(outputBase, `${family}-${size}`)
      : path.join(outputBase, family);

    if (values["all-sizes"]) {
      console.log(`Size: ${size}pt -> ${path.basename(familyDir)}/`);
    }

    for (const [styleName, fontPath] of styles) {
      if (!fontPath) continue;

      totalCount++;
      const outputFile = path.join(familyDir, `${styleName}.epdfont`);

      if (convertFont(fontPath, outputFile, size, is2Bit, intervals)) {
        successCount++;
      }
    }
  }

  console.log();
  console.log(`Converted ${successCount}/${totalCount} fonts`);

  if (successCount > 0) {
    console.log();
    console.log("To use this font in your theme, add to your .theme file:");
    console.log();
    console.log("[fonts]");
    for (const [sizeName, size] of [
      ["small", 14],
      ["medium", 16],
      ["large", 18],
    ]) {
      const fontName = values["all-sizes"] ? `${family}-${size}` : family;
      console.log(`reader_font_${sizeName} = ${fontName}`);
    }
    console.log();
    console.log("Then copy the font folder(s) to /config/fonts/ on your SD card.");
  }

  process.exit(successCount === totalCount ? 0 : 1);
}

main();
