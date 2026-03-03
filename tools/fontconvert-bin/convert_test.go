package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestDeriveName(t *testing.T) {
	tests := []struct {
		input string
		want  string
	}{
		{"NotoSansSC-Regular.ttf", "notosanssc"},
		{"My_Font-Bold.otf", "my-font-bold"},
		{"CJK Font-Medium.ttf", "cjk-font"},
		{"Simple.ttf", "simple"},
		{"/path/to/Font-Normal.otf", "font"},
		{"Already-Book.ttf", "already"},
		{"NoExtension", "noextension"},
	}
	for _, tt := range tests {
		got := deriveName(tt.input)
		if got != tt.want {
			t.Errorf("deriveName(%q) = %q, want %q", tt.input, got, tt.want)
		}
	}
}

func TestIsIdeograph(t *testing.T) {
	tests := []struct {
		cp   int
		want bool
	}{
		{0x4E00, true},  // CJK Unified start
		{0x9FFF, true},  // CJK Unified end
		{0x3400, true},  // Extension A start
		{0x4DBF, true},  // Extension A end
		{0xF900, true},  // Compat Ideographs start
		{0xFAFF, true},  // Compat Ideographs end
		{0x20000, true}, // Extension B start
		{0x2E80, false}, // CJK Radicals (not ideograph block)
		{0x3000, false}, // CJK Symbols
		{0x0041, false}, // Latin 'A'
		{0xFF00, false}, // Fullwidth Forms
	}
	for _, tt := range tests {
		got := isIdeograph(tt.cp)
		if got != tt.want {
			t.Errorf("isIdeograph(0x%04X) = %v, want %v", tt.cp, got, tt.want)
		}
	}
}

func TestParseHexOrDec(t *testing.T) {
	tests := []struct {
		input string
		want  int
	}{
		{"0xFFEF", 0xFFEF},
		{"0X1234", 0x1234},
		{"65519", 65519},
		{"100", 100},
	}
	for _, tt := range tests {
		got, err := parseHexOrDec(tt.input)
		if err != nil {
			t.Errorf("parseHexOrDec(%q) error: %v", tt.input, err)
			continue
		}
		if got != tt.want {
			t.Errorf("parseHexOrDec(%q) = %d, want %d", tt.input, got, tt.want)
		}
	}
}

// TestConvertIntegration tests the full conversion pipeline with a real font.
// Skipped if no CJK font is available at the expected path.
func TestConvertIntegration(t *testing.T) {
	repoRoot := filepath.Join("..", "..")
	candidates := []string{
		filepath.Join(repoRoot, "fonts", "Noto_Sans_SC", "static", "NotoSansSC-Regular.ttf"),
		filepath.Join(repoRoot, "fonts", "Noto_Sans_SC", "NotoSansSC-Regular.ttf"),
	}

	var fontPath string
	for _, p := range candidates {
		if _, err := os.Stat(p); err == nil {
			fontPath = p
			break
		}
	}
	if fontPath == "" {
		t.Skip("No CJK font available for integration test")
	}

	tmpDir := t.TempDir()
	cfg := Config{
		FontPath:     fontPath,
		OutputDir:    tmpDir,
		Name:         "test-font",
		PixelHeight:  34,
		DPI:          150,
		MaxCodepoint: 0xFF, // Small range for fast test
		CJKOnly:      true,
	}

	result, err := Convert(cfg)
	if err != nil {
		t.Fatalf("Convert() error: %v", err)
	}

	fi, err := os.Stat(result.OutputPath)
	if err != nil {
		t.Fatalf("output file not found: %v", err)
	}

	// Expected size: codepoints * bytesPerChar (no header, legacy format)
	expectedSize := int64(cfg.MaxCodepoint+1) * int64(result.BytesPerChar)
	if fi.Size() != expectedSize {
		t.Errorf("file size = %d, want %d", fi.Size(), expectedSize)
	}

	// In CJK-only mode with max codepoint 0xFF, all slots should be empty
	if result.Rendered != 0 {
		t.Errorf("rendered = %d, want 0 (CJK-only mode, Latin range)", result.Rendered)
	}
	if result.Empty != cfg.MaxCodepoint+1 {
		t.Errorf("empty = %d, want %d", result.Empty, cfg.MaxCodepoint+1)
	}
}
