package main

import (
	"fmt"
	"image"
	"math"
	"os"
	"path/filepath"
	"strings"

	"golang.org/x/image/font"
	"golang.org/x/image/font/opentype"
	"golang.org/x/image/font/sfnt"
	"golang.org/x/image/math/fixed"
)

var cjkSamples = []rune{'一', '中', '文', '字', '日', '本'}

const latinEnd = 0x024F

type Config struct {
	FontPath      string
	LatinFontPath string
	OutputDir     string
	Name          string
	Size          int
	PixelHeight   int
	LatinSize     int
	DPI           int
	MaxCodepoint  int
	CJKOnly       bool
}

type Result struct {
	OutputPath   string
	CellWidth    int
	CellHeight   int
	BytesPerChar int
	Rendered     int
	Empty        int
	Filtered     int
	Latin        int
}

func deriveName(filename string) string {
	base := filepath.Base(filename)
	ext := filepath.Ext(base)
	name := base[:len(base)-len(ext)]
	name = strings.ToLower(name)
	name = strings.ReplaceAll(name, "_", "-")
	name = strings.ReplaceAll(name, " ", "-")
	for _, suffix := range []string{"-regular", "-medium", "-normal", "-book"} {
		if strings.HasSuffix(name, suffix) {
			name = name[:len(name)-len(suffix)]
			break
		}
	}
	return name
}

func isIdeograph(cp int) bool {
	return (cp >= 0x4E00 && cp <= 0x9FFF) || // CJK Unified
		(cp >= 0x3400 && cp <= 0x4DBF) || // Extension A
		(cp >= 0x20000 && cp <= 0x2A6DF) || // Extension B+
		(cp >= 0xF900 && cp <= 0xFAFF) // Compat Ideographs
}


func loadFont(path string) (*sfnt.Font, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read %s: %w", path, err)
	}
	f, err := opentype.Parse(data)
	if err != nil {
		return nil, fmt.Errorf("parse %s: %w", path, err)
	}
	return f, nil
}

func makeFace(f *sfnt.Font, pixelHeight, sizePt, dpi int) (font.Face, error) {
	opts := &opentype.FaceOptions{
		DPI:     float64(dpi),
		Hinting: font.HintingFull,
	}
	if pixelHeight > 0 {
		opts.Size = float64(pixelHeight) * 72.0 / float64(dpi)
	} else {
		opts.Size = float64(sizePt)
	}
	return opentype.NewFace(f, opts)
}

func faceAscender(fc font.Face) int {
	m := fc.Metrics()
	return int(math.Ceil(float64(m.Ascent) / 64.0))
}

// glyphAlpha reads the alpha value of a mask pixel, handling different image types.
func glyphAlpha(mask image.Image, x, y int) uint8 {
	switch m := mask.(type) {
	case *image.Alpha:
		return m.AlphaAt(x, y).A
	case *image.Uniform:
		_, _, _, a := m.At(x, y).RGBA()
		return uint8(a >> 8)
	default:
		_, _, _, a := m.At(x, y).RGBA()
		return uint8(a >> 8)
	}
}

// renderGlyph renders a codepoint into the cell buffer. Returns true if glyph was rendered.
func renderGlyph(fc font.Face, cp rune, ascender int, cell []byte, cellWidth, cellHeight, bytesPerRow int) bool {
	dot := fixed.P(0, ascender)
	dr, mask, maskp, _, ok := fc.Glyph(dot, cp)
	if !ok || dr.Empty() {
		return false
	}

	// Check if the mask is fully transparent (space-like characters)
	if _, ok := mask.(*image.Uniform); ok {
		c := mask.At(0, 0)
		_, _, _, a := c.RGBA()
		if a == 0 {
			return false
		}
	}

	// Clear cell
	for i := range cell {
		cell[i] = 0
	}

	// Pack 1-bit: threshold at 128
	for y := dr.Min.Y; y < dr.Max.Y; y++ {
		if y < 0 || y >= cellHeight {
			continue
		}
		rowBase := y * bytesPerRow
		for x := dr.Min.X; x < dr.Max.X; x++ {
			if x < 0 || x >= cellWidth {
				continue
			}
			mx := maskp.X + (x - dr.Min.X)
			my := maskp.Y + (y - dr.Min.Y)
			a := glyphAlpha(mask, mx, my)
			if a >= 128 {
				cell[rowBase+(x>>3)] |= 0x80 >> uint(x&7)
			}
		}
	}
	return true
}

// glyphWidth returns the rendered width of a glyph (0 if not renderable).
func glyphWidth(fc font.Face, cp rune, ascender int) int {
	dot := fixed.P(0, ascender)
	dr, _, _, _, ok := fc.Glyph(dot, cp)
	if !ok || dr.Empty() {
		return 0
	}
	return dr.Dx()
}

func Convert(cfg Config) (*Result, error) {
	// Load primary font
	f, err := loadFont(cfg.FontPath)
	if err != nil {
		return nil, err
	}

	face, err := makeFace(f, cfg.PixelHeight, cfg.Size, cfg.DPI)
	if err != nil {
		return nil, fmt.Errorf("create face: %w", err)
	}
	defer face.Close()

	ascender := faceAscender(face)

	// Load optional Latin font
	var latinFace font.Face
	var latinAscender int
	if cfg.LatinFontPath != "" {
		lf, err := loadFont(cfg.LatinFontPath)
		if err != nil {
			return nil, err
		}
		lph := cfg.LatinSize
		if lph == 0 {
			lph = cfg.PixelHeight
		}
		latinFace, err = makeFace(lf, lph, cfg.Size, cfg.DPI)
		if err != nil {
			return nil, fmt.Errorf("create latin face: %w", err)
		}
		defer latinFace.Close()
		latinAscender = faceAscender(latinFace)
		fmt.Printf("Latin font: %s\n", cfg.LatinFontPath)
	}

	// Determine cell dimensions from sample CJK characters
	maxWidth := 0
	maxNeededHeight := 0
	for _, sample := range cjkSamples {
		dot := fixed.P(0, ascender)
		dr, _, _, _, ok := face.Glyph(dot, sample)
		if !ok || dr.Empty() {
			continue
		}
		w := dr.Dx()
		if w > maxWidth {
			maxWidth = w
		}
		h := dr.Max.Y
		if h > maxNeededHeight {
			maxNeededHeight = h
		}
	}

	if maxWidth == 0 || maxNeededHeight == 0 {
		return nil, fmt.Errorf("could not determine glyph dimensions; font may not have CJK chars")
	}

	cellWidth := maxWidth + 2
	cellHeight := maxNeededHeight + 2
	if cellWidth > 64 {
		cellWidth = 64
	}
	if cellHeight > 64 {
		cellHeight = 64
	}

	bytesPerRow := (cellWidth + 7) / 8
	bytesPerChar := bytesPerRow * cellHeight

	// Build output path — use the effective pixel size (no prefix) for firmware compatibility
	size := cfg.PixelHeight
	if size == 0 {
		size = cfg.Size
	}
	outName := fmt.Sprintf("%s_%d_%dx%d.bin", cfg.Name, size, cellWidth, cellHeight)
	outPath := filepath.Join(cfg.OutputDir, outName)

	fileSize := int64(cfg.MaxCodepoint+1) * int64(bytesPerChar)

	fmt.Printf("Font: %s\n", cfg.FontPath)
	if cfg.PixelHeight > 0 {
		fmt.Printf("Size: %dpx pixel height, ascender=%d\n", cfg.PixelHeight, ascender)
	} else {
		fmt.Printf("Size: %dpt @ %ddpi, ascender=%d\n", cfg.Size, cfg.DPI, ascender)
	}
	fmt.Printf("Cell: %dx%d (%d bytes/char, %d bytes/row)\n",
		cellWidth, cellHeight, bytesPerChar, bytesPerRow)
	fmt.Printf("Range: U+0000 - U+%04X (%d codepoints)\n", cfg.MaxCodepoint, cfg.MaxCodepoint+1)
	fmt.Printf("Output: %s (%.1f MB)\n\n", outPath, float64(fileSize)/(1024*1024))

	// Build Latin glyph ID set for filtering
	var buf sfnt.Buffer
	latinGIDs := make(map[sfnt.GlyphIndex]bool)
	for c := rune(0x20); c < 0x7F; c++ {
		gid, err := f.GlyphIndex(&buf, c)
		if err == nil && gid != 0 {
			latinGIDs[gid] = true
		}
	}

	// Create output directory
	if err := os.MkdirAll(cfg.OutputDir, 0755); err != nil {
		return nil, fmt.Errorf("create output dir: %w", err)
	}

	fp, err := os.Create(outPath)
	if err != nil {
		return nil, fmt.Errorf("create output file: %w", err)
	}
	defer fp.Close()

	cell := make([]byte, bytesPerChar)
	emptyCell := make([]byte, bytesPerChar)

	result := &Result{
		OutputPath:   outPath,
		CellWidth:    cellWidth,
		CellHeight:   cellHeight,
		BytesPerChar: bytesPerChar,
	}

	for cp := 0; cp <= cfg.MaxCodepoint; cp++ {
		// Skip Latin range in CJK-only mode
		if cfg.CJKOnly && cp <= latinEnd {
			fp.Write(emptyCell)
			result.Empty++
			continue
		}

		// Use Latin font for U+0000-U+024F if provided
		if latinFace != nil && cp <= latinEnd {
			if renderGlyph(latinFace, rune(cp), latinAscender, cell, cellWidth, cellHeight, bytesPerRow) {
				fp.Write(cell)
				result.Rendered++
				result.Latin++
				if cp > 0 && cp%5000 == 0 {
					fmt.Printf("  Progress: U+%04X (%d glyphs rendered)\n", cp, result.Rendered)
				}
				continue
			}
			// Latin font doesn't have this glyph, fall through to primary
		}

		r := rune(cp)

		// Check glyph index
		gidx, err := f.GlyphIndex(&buf, r)
		if err != nil || gidx == 0 {
			fp.Write(emptyCell)
			result.Empty++
			continue
		}

		// Filter CJK codepoints (U+2E80+)
		if cp >= 0x2E80 {
			// Check Latin glyph reuse
			if latinGIDs[gidx] {
				fp.Write(emptyCell)
				result.Filtered++
				continue
			}
		}

		// Render glyph
		if !renderGlyph(face, r, ascender, cell, cellWidth, cellHeight, bytesPerRow) {
			fp.Write(emptyCell)
			result.Empty++
			continue
		}

		// Width filter for ideograph blocks (after rendering)
		if cp >= 0x2E80 && isIdeograph(cp) {
			w := glyphWidth(face, r, ascender)
			if w*5 < cellWidth {
				fp.Write(emptyCell)
				result.Filtered++
				continue
			}
		}

		fp.Write(cell)
		result.Rendered++

		if cp > 0 && cp%5000 == 0 {
			fmt.Printf("  Progress: U+%04X (%d glyphs rendered)\n", cp, result.Rendered)
		}
	}

	return result, nil
}

