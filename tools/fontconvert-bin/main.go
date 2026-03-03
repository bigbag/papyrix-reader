package main

import (
	"fmt"
	"os"
	"strconv"
	"strings"
)

func usage() {
	fmt.Fprintf(os.Stderr,
		"Usage: fontconvert-bin <font.ttf> [options]\n"+
			"\n"+
			"Options:\n"+
			"  --size N             Font size in points (default: 20)\n"+
			"  --pixel-height N     Pixel height (overrides --size/--dpi)\n"+
			"  --name NAME          Output font name (default: derived from filename)\n"+
			"  --latin-font FILE    Separate font for Latin (U+0000-U+024F)\n"+
			"  --latin-size N       Pixel height for Latin font\n"+
			"  --cjk-only           Skip Latin range (U+0000-U+024F)\n"+
			"  -o, --output DIR     Output directory (default: .)\n"+
			"  --dpi N              Rendering DPI (default: 150)\n"+
			"  --max-codepoint N    Highest codepoint, hex or decimal (default: 0xFFEF)\n"+
			"  -h, --help           Show this help\n")
}

func parseHexOrDec(s string) (int, error) {
	if strings.HasPrefix(s, "0x") || strings.HasPrefix(s, "0X") {
		v, err := strconv.ParseInt(s[2:], 16, 64)
		return int(v), err
	}
	v, err := strconv.Atoi(s)
	return v, err
}

func mustNextArg(args []string, i int, flag string) string {
	if i+1 >= len(args) {
		fmt.Fprintf(os.Stderr, "Error: %s requires an argument\n", flag)
		os.Exit(1)
	}
	return args[i+1]
}

func main() {
	cfg := Config{
		Size:         20,
		DPI:          150,
		MaxCodepoint: 0xFFEF,
		OutputDir:    ".",
	}

	args := os.Args[1:]
	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "-h", "--help":
			usage()
			os.Exit(0)
		case "--size":
			v, err := strconv.Atoi(mustNextArg(args, i, "--size"))
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: invalid --size: %s\n", args[i+1])
				os.Exit(1)
			}
			cfg.Size = v
			i++
		case "--pixel-height":
			v, err := strconv.Atoi(mustNextArg(args, i, "--pixel-height"))
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: invalid --pixel-height: %s\n", args[i+1])
				os.Exit(1)
			}
			cfg.PixelHeight = v
			i++
		case "--name":
			cfg.Name = mustNextArg(args, i, "--name")
			i++
		case "--latin-font":
			cfg.LatinFontPath = mustNextArg(args, i, "--latin-font")
			i++
		case "--latin-size":
			v, err := strconv.Atoi(mustNextArg(args, i, "--latin-size"))
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: invalid --latin-size: %s\n", args[i+1])
				os.Exit(1)
			}
			cfg.LatinSize = v
			i++
		case "--cjk-only":
			cfg.CJKOnly = true
		case "-o", "--output":
			cfg.OutputDir = mustNextArg(args, i, args[i])
			i++
		case "--dpi":
			v, err := strconv.Atoi(mustNextArg(args, i, "--dpi"))
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: invalid --dpi: %s\n", args[i+1])
				os.Exit(1)
			}
			cfg.DPI = v
			i++
		case "--max-codepoint":
			v, err := parseHexOrDec(mustNextArg(args, i, "--max-codepoint"))
			if err != nil {
				fmt.Fprintf(os.Stderr, "Error: invalid --max-codepoint: %s\n", args[i+1])
				os.Exit(1)
			}
			cfg.MaxCodepoint = v
			i++
		default:
			if strings.HasPrefix(args[i], "-") {
				fmt.Fprintf(os.Stderr, "Unknown option: %s\n", args[i])
				usage()
				os.Exit(1)
			}
			if cfg.FontPath == "" {
				cfg.FontPath = args[i]
			} else {
				fmt.Fprintf(os.Stderr, "Unexpected argument: %s\n", args[i])
				usage()
				os.Exit(1)
			}
		}
	}

	if cfg.FontPath == "" {
		fmt.Fprintln(os.Stderr, "Error: No font file specified")
		usage()
		os.Exit(1)
	}

	if cfg.CJKOnly && cfg.LatinFontPath != "" {
		fmt.Fprintln(os.Stderr, "Error: --cjk-only and --latin-font are mutually exclusive")
		os.Exit(1)
	}

	if cfg.Name == "" {
		cfg.Name = deriveName(cfg.FontPath)
	}

	result, err := Convert(cfg)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	// Print summary
	fmt.Printf("\nDone! %d glyphs rendered", result.Rendered)
	if result.Latin > 0 {
		fmt.Printf(" (%d from Latin font)", result.Latin)
	}
	if cfg.CJKOnly {
		fmt.Print(" (CJK-only mode, Latin range skipped)")
	}
	fmt.Printf(", %d empty slots, %d filtered (Latin reuse)\n", result.Empty, result.Filtered)

	fi, err := os.Stat(result.OutputPath)
	if err == nil {
		fmt.Printf("Output: %s (%.1f MB)\n", result.OutputPath, float64(fi.Size())/(1024*1024))
	}

	outBase := result.OutputPath
	if idx := strings.LastIndex(outBase, "/"); idx >= 0 {
		outBase = outBase[idx+1:]
	}
	fmt.Printf("\nTo use on your device:\n")
	fmt.Printf("  1. Copy %s to /config/fonts/ on your SD card\n", outBase)
	fmt.Printf("  2. In your theme file, set: reader_font_medium = %s\n", outBase)
}
