#!/usr/bin/env node
/**
 * Convert TTF/OTF fonts to Papyrix binary format (.epdfont)
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
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const MAGIC = 0x46445045; // "EPDF" in little-endian
const VERSION = 1;
const DPI = 150;

// Unicode intervals
const INTERVALS_BASE = [
  [0x0000, 0x007f], // Basic Latin (ASCII)
  [0x0080, 0x00ff], // Latin-1 Supplement
  [0x0100, 0x017f], // Latin Extended-A
  [0x2000, 0x206f], // General Punctuation
  [0x2010, 0x203a], // Dashes, quotes, prime marks
  [0x2040, 0x205f], // Misc punctuation
  [0x20a0, 0x20cf], // Currency symbols
  [0x0300, 0x036f], // Combining Diacritical Marks
  [0x0400, 0x04ff], // Cyrillic
  [0x2200, 0x22ff], // Math operators
  [0x2190, 0x21ff], // Arrows
];

const INTERVALS_CJK_BASE = [
  [0x3000, 0x303f], // CJK Symbols and Punctuation
  [0x3040, 0x309f], // Hiragana
  [0x30a0, 0x30ff], // Katakana
  [0x4e00, 0x9fff], // CJK Unified Ideographs (20,992 chars)
  [0x1100, 0x11ff], // Hangul Jamo
  [0x3130, 0x318f], // Hangul Compatibility Jamo
  [0xac00, 0xd7af], // Hangul Syllables (11,172 chars)
];

/**
 * Scanline rasterizer for opentype.js paths - renders glyph to 8-bit grayscale with 4x supersampling
 */
class GlyphRasterizer {
  constructor(font, fontSize, variations = null) {
    this.font = font;
    this.fontSize = fontSize;
    this.scale = (fontSize * DPI) / (72 * font.unitsPerEm);
    this.variations = variations;

    if (font.tables.fvar?.axes?.length > 0) {
      const axisInfo = font.tables.fvar.axes.map(a => `${a.tag}(${a.minValue}-${a.defaultValue}-${a.maxValue})`);
      console.log(`  Variable font axes: ${axisInfo.join(", ")}`);
      if (variations) {
        console.log(`  Applied variations: ${Object.entries(variations).map(([k, v]) => `${k}=${v}`).join(", ")}`);
      }
    }
  }

  renderGlyph(codePoint) {
    const glyphIndex = this.font.charToGlyphIndex(String.fromCodePoint(codePoint));
    if (glyphIndex === 0 && codePoint !== 0) return null;

    const glyph = this.font.glyphs.get(glyphIndex);
    if (!glyph) return null;

    const advanceWidth = Math.round((glyph.advanceWidth ?? 0) * this.scale);
    const bbox = glyph.getBoundingBox();

    if (!bbox || (bbox.x1 === 0 && bbox.y1 === 0 && bbox.x2 === 0 && bbox.y2 === 0)) {
      return { width: 0, height: 0, advanceX: advanceWidth, left: 0, top: 0, data: Buffer.alloc(0) };
    }

    const x1 = Math.floor(bbox.x1 * this.scale);
    const x2 = Math.ceil(bbox.x2 * this.scale);
    const y2 = Math.ceil(bbox.y2 * this.scale);
    const width = Math.max(1, x2 - x1);
    const height = Math.max(1, Math.ceil(bbox.y2 * this.scale) - Math.floor(bbox.y1 * this.scale));

    // 4x supersampling
    const ssScale = 4;
    const ssWidth = width * ssScale;
    const ssHeight = height * ssScale;
    const ssBuffer = new Uint8Array(ssWidth * ssHeight);

    const pathOptions = this.variations ? { variation: this.variations } : {};
    const glyphPath = glyph.getPath(0, 0, this.fontSize * DPI / 72, pathOptions);
    this.rasterizePath(glyphPath, ssBuffer, ssWidth, ssHeight, -x1 * ssScale, y2 * ssScale);

    // Downsample
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

    return { width, height, advanceX: advanceWidth, left: x1, top: y2, data: Buffer.from(buffer) };
  }

  rasterizePath(glyphPath, buffer, width, height, offsetX, offsetY) {
    const edges = [];
    let currentX = 0, currentY = 0, startX = 0, startY = 0;

    for (const cmd of glyphPath.commands) {
      switch (cmd.type) {
        case "M":
          currentX = startX = cmd.x + offsetX;
          currentY = startY = offsetY - cmd.y;
          break;
        case "L": {
          const x = cmd.x + offsetX, y = offsetY - cmd.y;
          this.addEdge(edges, currentX, currentY, x, y);
          currentX = x; currentY = y;
          break;
        }
        case "Q": {
          const x = cmd.x + offsetX, y = offsetY - cmd.y;
          this.addCurve(edges, currentX, currentY, cmd.x1 + offsetX, offsetY - cmd.y1, x, y, 8);
          currentX = x; currentY = y;
          break;
        }
        case "C": {
          const x = cmd.x + offsetX, y = offsetY - cmd.y;
          this.addCubicCurve(edges, currentX, currentY, cmd.x1 + offsetX, offsetY - cmd.y1, cmd.x2 + offsetX, offsetY - cmd.y2, x, y);
          currentX = x; currentY = y;
          break;
        }
        case "Z":
          this.addEdge(edges, currentX, currentY, startX, startY);
          currentX = startX; currentY = startY;
          break;
      }
    }

    edges.sort((a, b) => a.yMin - b.yMin);

    for (let y = 0; y < height; y++) {
      const scanY = y + 0.5;
      const intersections = edges
        .filter(e => e.yMin <= scanY && e.yMax > scanY)
        .map(e => e.x1 + ((scanY - e.y1) * (e.x2 - e.x1)) / (e.y2 - e.y1))
        .sort((a, b) => a - b);

      for (let i = 0; i < intersections.length - 1; i += 2) {
        const xStart = Math.max(0, Math.floor(intersections[i]));
        const xEnd = Math.min(width, Math.ceil(intersections[i + 1]));
        for (let x = xStart; x < xEnd; x++) buffer[y * width + x] = 255;
      }
    }
  }

  addEdge(edges, x1, y1, x2, y2) {
    if (Math.abs(y2 - y1) < 0.001) return;
    if (y1 > y2) { [x1, x2] = [x2, x1]; [y1, y2] = [y2, y1]; }
    edges.push({ x1, y1, x2, y2, yMin: y1, yMax: y2 });
  }

  addCurve(edges, x0, y0, x1, y1, x2, y2, steps) {
    let px = x0, py = y0;
    for (let i = 1; i <= steps; i++) {
      const t = i / steps, mt = 1 - t;
      const x = mt * mt * x0 + 2 * mt * t * x1 + t * t * x2;
      const y = mt * mt * y0 + 2 * mt * t * y1 + t * t * y2;
      this.addEdge(edges, px, py, x, y);
      px = x; py = y;
    }
  }

  addCubicCurve(edges, x0, y0, x1, y1, x2, y2, x3, y3) {
    let px = x0, py = y0;
    for (let i = 1; i <= 12; i++) {
      const t = i / 12, mt = 1 - t;
      const x = mt * mt * mt * x0 + 3 * mt * mt * t * x1 + 3 * mt * t * t * x2 + t * t * t * x3;
      const y = mt * mt * mt * y0 + 3 * mt * mt * t * y1 + 3 * mt * t * t * y2 + t * t * t * y3;
      this.addEdge(edges, px, py, x, y);
      px = x; py = y;
    }
  }
}

function renderDownsampled(data, width, height, is2Bit) {
  const bitsPerPixel = is2Bit ? 2 : 1;
  const pixelsPerByte = 8 / bitsPerPixel;
  const pixels = [];
  let px = 0;

  for (let i = 0; i < width * height; i++) {
    const value = data[i] >> 4; // Convert 8-bit to 4-bit
    const quantized = is2Bit
      ? (value >= 12 ? 3 : value >= 8 ? 2 : value >= 4 ? 1 : 0)
      : (value >= 2 ? 1 : 0);
    px = (px << bitsPerPixel) | quantized;
    if (i % pixelsPerByte === pixelsPerByte - 1) { pixels.push(px); px = 0; }
  }

  const remainder = (width * height) % pixelsPerByte;
  if (remainder !== 0) {
    px <<= (pixelsPerByte - remainder) * bitsPerPixel;
    pixels.push(px);
  }

  return Buffer.from(pixels);
}

function validateIntervals(font, intervals) {
  const validIntervals = [];
  for (const [iStart, iEnd] of intervals) {
    let start = iStart;
    for (let cp = iStart; cp <= iEnd; cp++) {
      if (font.charToGlyphIndex(String.fromCodePoint(cp)) === 0 && cp !== 0) {
        if (start < cp) validIntervals.push([start, cp - 1]);
        start = cp + 1;
      }
    }
    if (start <= iEnd) validIntervals.push([start, iEnd]);
  }
  return validIntervals;
}

/**
 * Render all glyphs from a font - shared by binary and header output
 */
function renderAllGlyphs(font, rasterizer, intervals, is2Bit, progressLabel) {
  const validIntervals = validateIntervals(font, [...intervals].sort((a, b) => a[0] - b[0]));
  if (!validIntervals.length) return null;

  const totalGlyphs = validIntervals.reduce((sum, [s, e]) => sum + (e - s + 1), 0);
  const allGlyphs = [];
  let totalBitmapSize = 0;
  let processed = 0;
  let lastPercent = -1;

  for (const [iStart, iEnd] of validIntervals) {
    for (let codePoint = iStart; codePoint <= iEnd; codePoint++) {
      const glyph = rasterizer.renderGlyph(codePoint);
      const width = glyph?.width ?? 0;
      const height = glyph?.height ?? 0;
      const pixelData = (width > 0 && height > 0 && glyph?.data)
        ? renderDownsampled(glyph.data, width, height, is2Bit)
        : Buffer.alloc(0);

      allGlyphs.push({
        width, height,
        advanceX: glyph?.advanceX ?? 0,
        left: glyph?.left ?? 0,
        top: glyph?.top ?? 0,
        dataLength: pixelData.length,
        dataOffset: totalBitmapSize,
        codePoint,
        pixelData,
      });

      totalBitmapSize += pixelData.length;
      processed++;

      const percent = Math.floor((processed / totalGlyphs) * 100);
      if (percent !== lastPercent && percent % 10 === 0) {
        process.stdout.write(`\r  ${progressLabel} (${percent}%)`);
        lastPercent = percent;
      }
    }
  }
  process.stdout.write("\r" + " ".repeat(80) + "\r");

  const scale = rasterizer.scale;
  return {
    glyphs: allGlyphs,
    validIntervals,
    totalBitmapSize,
    metrics: {
      advanceY: Math.ceil((font.ascender - font.descender) * scale),
      ascender: Math.ceil(font.ascender * scale),
      descender: Math.floor(font.descender * scale),
    },
  };
}

function convertFont(fontPath, outputPath, size, is2Bit, intervals, variations = null) {
  if (!fs.existsSync(fontPath)) {
    console.error(`  Warning: Font file not found: ${fontPath}`);
    return false;
  }

  const label = `Converting: ${path.basename(fontPath)} -> ${path.basename(outputPath)}`;
  console.log(`  ${label}`);

  try {
    const font = opentype.loadSync(fontPath);
    const rasterizer = new GlyphRasterizer(font, size, variations);
    const result = renderAllGlyphs(font, rasterizer, intervals, is2Bit, label);

    if (!result) {
      console.error("  Error: No valid glyphs found");
      return false;
    }

    const { glyphs, validIntervals, totalBitmapSize, metrics } = result;

    // Build binary file
    fs.mkdirSync(path.dirname(outputPath), { recursive: true });

    const headerSize = 16, metricsSize = 18;
    const intervalsSize = validIntervals.length * 12;
    const glyphsSize = glyphs.length * 14;
    const buffer = Buffer.alloc(headerSize + metricsSize + intervalsSize + glyphsSize + totalBitmapSize);
    let offset = 0;

    // Header
    buffer.writeUInt32LE(MAGIC, offset); offset += 4;
    buffer.writeUInt16LE(VERSION, offset); offset += 2;
    buffer.writeUInt16LE(is2Bit ? 0x01 : 0x00, offset); offset += 2;
    offset += 8; // reserved

    // Metrics
    buffer.writeUInt8(metrics.advanceY & 0xff, offset++);
    buffer.writeUInt8(0, offset++); // pad
    buffer.writeInt16LE(metrics.ascender, offset); offset += 2;
    buffer.writeInt16LE(metrics.descender, offset); offset += 2;
    buffer.writeUInt32LE(validIntervals.length, offset); offset += 4;
    buffer.writeUInt32LE(glyphs.length, offset); offset += 4;
    buffer.writeUInt32LE(totalBitmapSize, offset); offset += 4;

    // Intervals
    let glyphOffset = 0;
    for (const [iStart, iEnd] of validIntervals) {
      buffer.writeUInt32LE(iStart, offset); offset += 4;
      buffer.writeUInt32LE(iEnd, offset); offset += 4;
      buffer.writeUInt32LE(glyphOffset, offset); offset += 4;
      glyphOffset += iEnd - iStart + 1;
    }

    // Glyphs
    for (const g of glyphs) {
      buffer.writeUInt8(g.width, offset++);
      buffer.writeUInt8(g.height, offset++);
      buffer.writeUInt8(g.advanceX & 0xff, offset++);
      buffer.writeUInt8(0, offset++); // pad
      buffer.writeInt16LE(g.left, offset); offset += 2;
      buffer.writeInt16LE(g.top, offset); offset += 2;
      buffer.writeUInt16LE(g.dataLength, offset); offset += 2;
      buffer.writeUInt32LE(g.dataOffset, offset); offset += 4;
    }

    // Bitmap data
    for (const g of glyphs) {
      g.pixelData.copy(buffer, offset);
      offset += g.pixelData.length;
    }

    fs.writeFileSync(outputPath, buffer);
    console.log(`  Created: ${outputPath} (${buffer.length} bytes)`);
    return true;
  } catch (error) {
    console.error(`  Error: ${error.message}`);
    return false;
  }
}

function convertFontToHeader(fontPath, outputPath, size, is2Bit, intervals, headerName, variations = null) {
  if (!fs.existsSync(fontPath)) {
    console.error(`  Warning: Font file not found: ${fontPath}`);
    return false;
  }

  const label = `Converting: ${path.basename(fontPath)} -> ${path.basename(outputPath)}`;
  console.log(`  ${label}`);

  try {
    const font = opentype.loadSync(fontPath);
    const rasterizer = new GlyphRasterizer(font, size, variations);
    const result = renderAllGlyphs(font, rasterizer, intervals, is2Bit, label);

    if (!result) {
      console.error("  Error: No valid glyphs found");
      return false;
    }

    const { glyphs, validIntervals, totalBitmapSize, metrics } = result;
    fs.mkdirSync(path.dirname(outputPath), { recursive: true });

    const lines = [
      "/**",
      " * generated by convert-fonts.mjs",
      ` * name: ${headerName}`,
      ` * size: ${size}`,
      ` * mode: ${is2Bit ? "2-bit" : "1-bit"}`,
      " */",
      "#pragma once",
      '#include "EpdFontData.h"',
      "",
    ];

    // Bitmap array
    const combinedBitmap = Buffer.concat(glyphs.map(g => g.pixelData));
    lines.push(`static const uint8_t ${headerName}Bitmaps[${totalBitmapSize}] = {`);
    for (let i = 0; i < combinedBitmap.length; i += 19) {
      const chunk = combinedBitmap.slice(i, Math.min(i + 19, combinedBitmap.length));
      const hex = Array.from(chunk).map(b => `0x${b.toString(16).toUpperCase().padStart(2, "0")}`).join(", ");
      lines.push(`    ${hex}${i + 19 >= combinedBitmap.length ? "" : ","}`);
    }
    lines.push("};", "");

    // Glyphs array
    const getCharComment = (cp) => {
      if (cp < 0x20 || cp === 0x5c || cp === 0x2a) return cp === 0x5c ? " // \\\\" : cp === 0x2a ? " // *" : "";
      try {
        const char = String.fromCodePoint(cp);
        return /[\p{C}\p{Z}]/u.test(char) && cp !== 0x20 ? "" : ` // ${char}`;
      } catch { return ""; }
    };

    lines.push(`static const EpdGlyph ${headerName}Glyphs[] = {`);
    for (const g of glyphs) {
      lines.push(`    {${g.width}, ${g.height}, ${g.advanceX}, ${g.left}, ${g.top}, ${g.dataLength}, ${g.dataOffset}},${getCharComment(g.codePoint)}`);
    }
    lines.push("};", "");

    // Intervals array
    lines.push(`static const EpdUnicodeInterval ${headerName}Intervals[] = {`);
    let glyphOffset = 0;
    const intervalEntries = validIntervals.map(([s, e]) => {
      const entry = `{0x${s.toString(16)}, 0x${e.toString(16)}, 0x${glyphOffset.toString(16)}}`;
      glyphOffset += e - s + 1;
      return entry;
    });
    for (let i = 0; i < intervalEntries.length; i += 4) {
      const chunk = intervalEntries.slice(i, Math.min(i + 4, intervalEntries.length));
      lines.push(`    ${chunk.join(", ")}${i + 4 >= intervalEntries.length ? "" : ","}`);
    }
    lines.push("};", "");

    // Font struct
    lines.push(`static const EpdFontData ${headerName} = {`);
    lines.push(`    ${headerName}Bitmaps, ${headerName}Glyphs, ${headerName}Intervals, ${validIntervals.length}, ${metrics.advanceY}, ${metrics.ascender}, ${metrics.descender}, ${is2Bit},`);
    lines.push("};");

    fs.writeFileSync(outputPath, lines.join("\n") + "\n");
    console.log(`  Created: ${outputPath} (${totalBitmapSize} bytes bitmap, ${glyphs.length} glyphs)`);
    return true;
  } catch (error) {
    console.error(`  Error: ${error.message}`);
    return false;
  }
}

function generatePreview(fontPath, outputPath, size, variations = null) {
  if (!fs.existsSync(fontPath)) return false;

  try {
    const font = opentype.loadSync(fontPath);
    const rasterizer = new GlyphRasterizer(font, size, variations);

    const sampleChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%&*()";
    const glyphs = [...sampleChars]
      .map(char => {
        const glyph = rasterizer.renderGlyph(char.codePointAt(0));
        return glyph?.width > 0 ? { char, ...glyph } : null;
      })
      .filter(Boolean);

    const escapeHtml = c => c === "<" ? "&lt;" : c === ">" ? "&gt;" : c === "&" ? "&amp;" : c;

    const html = `<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Font Preview: ${path.basename(fontPath)}</title>
  <style>
    body { font-family: monospace; background: #f0f0f0; padding: 20px; }
    h1 { margin-bottom: 10px; }
    .info { color: #666; margin-bottom: 20px; }
    .glyphs { display: flex; flex-wrap: wrap; gap: 8px; background: white; padding: 20px; border-radius: 8px; }
    .glyph { display: flex; flex-direction: column; align-items: center; padding: 8px; border: 1px solid #ddd; border-radius: 4px; }
    .glyph canvas { image-rendering: pixelated; }
    .glyph .char { font-size: 12px; color: #333; margin-top: 4px; }
    .glyph .metrics { font-size: 10px; color: #999; }
  </style>
</head>
<body>
  <h1>Font Preview: ${path.basename(fontPath)}</h1>
  <div class="info">Size: ${size}pt | Glyphs: ${glyphs.length}</div>
  <div class="glyphs">
${glyphs.map(g => `    <div class="glyph">
      <canvas width="${g.width}" height="${g.height}" data-pixels="data:application/octet-stream;base64,${g.data.toString("base64")}" style="width: ${g.width * 2}px; height: ${g.height * 2}px;"></canvas>
      <span class="char">${escapeHtml(g.char)}</span>
      <span class="metrics">${g.width}x${g.height}</span>
    </div>`).join("\n")}
  </div>
  <script>
    document.querySelectorAll('canvas[data-pixels]').forEach(canvas => {
      const ctx = canvas.getContext('2d');
      const w = canvas.width, h = canvas.height;
      const data = atob(canvas.dataset.pixels.split(',')[1]);
      const imgData = ctx.createImageData(w, h);
      for (let i = 0; i < data.length && i < w * h; i++) {
        const v = 255 - data.charCodeAt(i);
        imgData.data[i * 4] = imgData.data[i * 4 + 1] = imgData.data[i * 4 + 2] = v;
        imgData.data[i * 4 + 3] = 255;
      }
      ctx.putImageData(imgData, 0, 0);
    });
  </script>
</body>
</html>`;

    fs.writeFileSync(outputPath, html);
    console.log(`  Preview: ${outputPath}`);
    return true;
  } catch (error) {
    console.error(`  Preview error: ${error.message}`);
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
      "cjk-2500": { type: "boolean", default: false },
      header: { type: "boolean", default: false },
      var: { type: "string", multiple: true },
      preview: { type: "boolean", default: false },
      help: { type: "boolean", short: "h", default: false },
    },
  });

  if (values.help || positionals.length === 0) {
    console.log(`
Convert TTF/OTF fonts to Papyrix binary format (.epdfont)

Usage:
  node convert-fonts.mjs <family-name> -r <regular.ttf> [options]

Options:
  -r, --regular  Path to regular style TTF/OTF file (required)
  -b, --bold     Path to bold style TTF/OTF file
  -i, --italic   Path to italic style TTF/OTF file
  -o, --output   Output directory (default: current directory)
  -s, --size     Font size in points (default: 16)
  --2bit         Generate 2-bit grayscale (smoother but larger)
  --all-sizes    Generate all reader sizes (14, 16, 18pt)
  --cjk-common   Use full CJK (20,992) + Hangul (11,172), reduced fullwidth
  --cjk-2500     Use minimal CJK (~2500 chars): Joyo kanji + kana + punctuation
  --header       Output C header file instead of binary .epdfont
  --var          Variable font axis value (e.g., --var wght=700 --var wdth=100)
  --preview      Generate HTML preview of rendered glyphs
  -h, --help     Show this help message

Examples:
  node convert-fonts.mjs my-font -r MyFont-Regular.ttf -b MyFont-Bold.ttf -i MyFont-Italic.ttf
  node convert-fonts.mjs roboto -r Roboto-VariableFont_wdth,wght.ttf --var wght=400
  node convert-fonts.mjs noto-sans-jp -r NotoSansJP-Regular.ttf --all-sizes --cjk-2500
`);
    process.exit(0);
  }

  const family = positionals[0];
  if (!values.regular) {
    console.error("Error: Regular font (-r) is required");
    process.exit(1);
  }

  // Build interval set
  let intervals = [...INTERVALS_BASE];
  if (values["cjk-2500"]) {
    const joyoPath = path.join(__dirname, "joyo-kanji.json");
    if (!fs.existsSync(joyoPath)) {
      console.error(`Error: Joyo kanji file not found: ${joyoPath}`);
      process.exit(1);
    }
    const joyoKanji = JSON.parse(fs.readFileSync(joyoPath, "utf-8")).codepoints;
    intervals.push(
      [0x3000, 0x303f], [0x3040, 0x309f], [0x30a0, 0x30ff],
      ...joyoKanji.map(cp => [cp, cp]),
      [0xff01, 0xff5e]
    );
    console.log(`Using minimal CJK: ${joyoKanji.length} Joyo kanji + kana (~2500 chars)`);
  } else if (values["cjk-common"]) {
    intervals.push(...INTERVALS_CJK_BASE, [0xff00, 0xff5f]);
    console.log("Using full CJK (20,992 chars) + full Hangul (11,172 chars)");
  } else {
    intervals.push(...INTERVALS_CJK_BASE, [0xff00, 0xffef]);
  }

  // Parse variations
  let variations = null;
  if (values.var?.length > 0) {
    variations = {};
    for (const spec of values.var) {
      const match = spec.match(/^(\w+)=(\d+(?:\.\d+)?)$/);
      if (!match) {
        console.error(`Error: Invalid --var format: ${spec} (use axis=value)`);
        process.exit(1);
      }
      variations[match[1]] = parseFloat(match[2]);
    }
  }

  const { output: outputBase, "2bit": is2Bit, header: outputHeader, preview: doPreview } = values;
  const baseSize = parseInt(values.size, 10);
  if (isNaN(baseSize) || baseSize <= 0) {
    console.error("Error: Invalid font size");
    process.exit(1);
  }

  console.log(`Converting font family: ${family}`);
  console.log(`Output directory: ${outputBase}`);
  console.log(`Font size: ${baseSize}pt`);
  if (is2Bit) console.log("Mode: 2-bit grayscale");
  if (outputHeader) console.log("Output: C header files");
  if (variations) console.log(`Variable font: ${Object.entries(variations).map(([k, v]) => `${k}=${v}`).join(", ")}`);
  console.log();

  const styles = [["regular", values.regular], ["bold", values.bold], ["italic", values.italic]];
  const sizes = values["all-sizes"] ? [14, 16, 18] : [baseSize];
  let successCount = 0, totalCount = 0;

  for (const size of sizes) {
    const familyDir = values["all-sizes"] ? path.join(outputBase, `${family}-${size}`) : path.join(outputBase, family);
    if (values["all-sizes"]) console.log(`Size: ${size}pt -> ${path.basename(familyDir)}/`);

    for (const [styleName, fontPath] of styles) {
      if (!fontPath) continue;
      totalCount++;

      const outputFile = outputHeader
        ? path.join(outputBase, `${family.replace(/-/g, "_")}_${styleName}${values["all-sizes"] ? `_${size}` : ""}_2b.h`)
        : path.join(familyDir, `${styleName}.epdfont`);

      const success = outputHeader
        ? convertFontToHeader(fontPath, outputFile, size, is2Bit, intervals, path.basename(outputFile, ".h"), variations)
        : convertFont(fontPath, outputFile, size, is2Bit, intervals, variations);

      if (success) {
        successCount++;
        if (doPreview) {
          generatePreview(fontPath, outputFile.replace(/\.(epdfont|h)$/, ".html"), size, variations);
        }
      }
    }
  }

  console.log();
  console.log(`Converted ${successCount}/${totalCount} fonts`);

  if (successCount > 0) {
    console.log("\nTo use this font in your theme, add to your .theme file:\n");
    console.log("[fonts]");
    for (const [name, sz] of [["small", 14], ["medium", 16], ["large", 18]]) {
      console.log(`reader_font_${name} = ${values["all-sizes"] ? `${family}-${sz}` : family}`);
    }
    console.log("\nThen copy the font folder(s) to /config/fonts/ on your SD card.");
  }

  process.exit(successCount === totalCount ? 0 : 1);
}

main();
