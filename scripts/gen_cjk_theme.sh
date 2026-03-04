#!/usr/bin/env bash
#
# gen_cjk_theme.sh — Generate a .bin font and .theme file for Papyrix Reader
#
# Runs fontconvert-bin at a single pixel height and creates a matching
# theme INI file. All reader font sizes point to the same .bin file.
#
# Requires: tools/fontconvert-bin (auto-downloaded or built via 'make fontconvert-bin')
#
# Usage:
#   ./scripts/gen_cjk_theme.sh --cjk CJKFont.ttf [options]
#
# Examples:
#   # CJK font renders everything (Latin + CJK)
#   ./scripts/gen_cjk_theme.sh --cjk MyCJKFont.otf --latin-mode cjk --name my-cjk-font
#
#   # Separate Latin font for ANSI, CJK font for ideographs
#   ./scripts/gen_cjk_theme.sh --cjk MyCJKFont.ttf --latin MySerifFont.ttf --name my-mixed-font
#
#   # CJK only, builtin system font handles Latin
#   ./scripts/gen_cjk_theme.sh --cjk MyCJKFont.otf --latin-mode system --name my-cjk-font
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/.."
CONVERTER="$PROJECT_DIR/tools/fontconvert-bin/build/fontconvert-bin"

die() { echo "Error: $*" >&2; exit 1; }
info() { echo "==> $*"; }
warn() { echo "WARNING: $*" >&2; }

GITHUB_REPO="bigbag/papyrix-reader"
GITHUB_RELEASE_TAG="fontconvert-bin-v0.1.0"

# Auto-download fontconvert-bin from GitHub releases if not found locally
download_converter() {
    local os arch asset_name url dest_dir
    dest_dir="$PROJECT_DIR/tools/fontconvert-bin/build"

    os="$(uname -s | tr '[:upper:]' '[:lower:]')"
    arch="$(uname -m)"
    case "$arch" in
        x86_64)  arch="amd64" ;;
        aarch64|arm64) arch="arm64" ;;
        *) die "Unsupported architecture: $arch" ;;
    esac
    case "$os" in
        linux|darwin) ;;
        *) die "Unsupported OS: $os (use 'make fontconvert-bin' to build from source)" ;;
    esac

    asset_name="fontconvert-bin-${os}-${arch}"
    url="https://github.com/${GITHUB_REPO}/releases/download/${GITHUB_RELEASE_TAG}/${asset_name}"

    info "Downloading fontconvert-bin for ${os}/${arch}..."
    mkdir -p "$dest_dir"
    if ! curl -fSL --progress-bar -o "$dest_dir/fontconvert-bin" "$url"; then
        rm -f "$dest_dir/fontconvert-bin"
        die "Failed to download fontconvert-bin from $url"
    fi
    chmod +x "$dest_dir/fontconvert-bin"
    info "Downloaded to $dest_dir/fontconvert-bin"
}

if [[ ! -x "$CONVERTER" ]]; then
    download_converter
fi

# Defaults
CJK_FONT=""
LATIN_FONT=""
LATIN_MODE=""  # include, cjk, system (determined automatically if not set)
FONT_NAME=""
THEME_NAME=""
SIZE=34
OUTPUT_DIR="./fonts/output"
DARK_MODE=false
MAX_BYTES_PER_CHAR=512
MAX_FILE_SIZE=$((32 * 1024 * 1024))  # 32MB
MAX_FONT_NAME_LEN=31
MAX_RETRY_DECREMENT=4  # max px to subtract when retrying

usage() {
    cat <<'EOF'
Usage: gen_cjk_theme.sh --cjk FONT [options]

Required:
  --cjk FONT           CJK font file (.ttf/.otf)

Options:
  --latin FONT         Latin font file (.ttf/.otf) for ANSI glyphs
  --latin-mode MODE    Latin handling: include, cjk, system (default: auto-detect)
                         include — use --latin-font in converter (requires --latin)
                         cjk    — CJK font renders Latin too (no --latin needed)
                         system — skip Latin, builtin handles it (--cjk-only)
  --name NAME          Font name for .bin files (default: derived from CJK filename)
  --theme-name NAME    Display name in theme (default: derived from --name)
  --size N             Pixel height (default: 34)
  --output DIR         Output directory (default: ./fonts/output/)
  --dark               Dark theme variant
  -h, --help           Show this help

Examples:
  gen_cjk_theme.sh --cjk MyCJKFont.otf --latin-mode cjk --name my-cjk-font
  gen_cjk_theme.sh --cjk MyCJKFont.ttf --latin MySerifFont.ttf --name my-mixed-font
  gen_cjk_theme.sh --cjk MyCJKFont.otf --latin-mode system --name my-cjk-font --dark
EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --cjk)       CJK_FONT="$2"; shift 2 ;;
        --latin)     LATIN_FONT="$2"; shift 2 ;;
        --latin-mode) LATIN_MODE="$2"; shift 2 ;;
        --name)      FONT_NAME="$2"; shift 2 ;;
        --theme-name) THEME_NAME="$2"; shift 2 ;;
        --size)      SIZE="$2"; shift 2 ;;
        --output)    OUTPUT_DIR="$2"; shift 2 ;;
        --dark)      DARK_MODE=true; shift ;;
        -h|--help)   usage; exit 0 ;;
        *)           die "Unknown option: $1" ;;
    esac
done

# Validate required args
[[ -n "$CJK_FONT" ]] || die "Missing required --cjk FONT argument"
[[ -f "$CJK_FONT" ]] || die "CJK font file not found: $CJK_FONT"

# Derive font name if not provided
if [[ -z "$FONT_NAME" ]]; then
    basename=$(basename "$CJK_FONT")
    FONT_NAME="${basename%.*}"
    FONT_NAME=$(echo "$FONT_NAME" | tr '[:upper:]' '[:lower:]' | tr '_ ' '--')
    for suffix in -regular -medium -normal -book; do
        FONT_NAME="${FONT_NAME%"$suffix"}"
    done
fi

# Auto-detect latin-mode if not set
if [[ -z "$LATIN_MODE" ]]; then
    if [[ -n "$LATIN_FONT" ]]; then
        LATIN_MODE="include"
    else
        LATIN_MODE="cjk"
    fi
fi

# Validate latin-mode
case "$LATIN_MODE" in
    include)
        [[ -n "$LATIN_FONT" ]] || die "--latin-mode include requires --latin FONT"
        [[ -f "$LATIN_FONT" ]] || die "Latin font file not found: $LATIN_FONT"
        ;;
    cjk|system)
        if [[ -n "$LATIN_FONT" ]]; then
            warn "--latin FONT ignored with --latin-mode $LATIN_MODE"
            LATIN_FONT=""
        fi
        ;;
    *) die "Invalid --latin-mode: $LATIN_MODE (must be: include, cjk, system)" ;;
esac

# Derive theme display name
if [[ -z "$THEME_NAME" ]]; then
    THEME_NAME=$(echo "$FONT_NAME" | tr '-' ' ' | awk '{for(i=1;i<=NF;i++) $i=toupper(substr($i,1,1)) substr($i,2)}1')
fi

# Validate name lengths
if [[ ${#FONT_NAME} -gt $MAX_FONT_NAME_LEN ]]; then
    die "Font name too long: '${FONT_NAME}' (${#FONT_NAME} chars, max $MAX_FONT_NAME_LEN)"
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

info "Configuration:"
echo "  CJK font:    $CJK_FONT"
[[ -n "$LATIN_FONT" ]] && echo "  Latin font:   $LATIN_FONT"
echo "  Latin mode:   $LATIN_MODE"
echo "  Font name:    $FONT_NAME"
echo "  Theme name:   $THEME_NAME"
echo "  Size:         ${SIZE} px"
echo "  Output:       $OUTPUT_DIR"
echo "  Dark mode:    $DARK_MODE"
echo ""

# Build converter flags based on latin-mode
build_converter_args() {
    cmd_args=("$CONVERTER" "$CJK_FONT" --pixel-height "$1" --name "$FONT_NAME")
    case "$LATIN_MODE" in
        include) cmd_args+=(--latin-font "$LATIN_FONT") ;;
        system)  cmd_args+=(--cjk-only) ;;
        cjk)     ;;
    esac
    cmd_args+=(-o "$OUTPUT_DIR")
}

# Parse bytesPerChar from converter output
parse_bytes_per_char() {
    local output="$1"
    echo "$output" | grep -o '[0-9]* bytes/char' | head -1 | grep -o '^[0-9]*'
}

# Parse output filename from converter output
parse_output_file() {
    local output="$1"
    echo "$output" | grep '^Output:' | tail -1 | sed 's/^Output: //' | sed 's/ (.*//'
}

# Generate font with auto-retry on validation failure
BIN_FILE=""
BIN_BASENAME=""
attempt=0

while [[ $attempt -le $MAX_RETRY_DECREMENT ]]; do
    current_px=$((SIZE - attempt))
    [[ $current_px -lt 10 ]] && die "Pixel height too small after retries"

    info "Generating ${FONT_NAME} at ${current_px}px..."

    build_converter_args "$current_px"
    info "Running: ${cmd_args[*]}"

    if ! output=$("${cmd_args[@]}" 2>&1); then
        echo "$output"
        die "Converter failed for ${current_px}px"
    fi

    echo "$output"

    # Parse and validate bytesPerChar
    bpc=$(parse_bytes_per_char "$output")
    if [[ -z "$bpc" ]]; then
        warn "Could not parse bytesPerChar from converter output"
    elif [[ $bpc -gt $MAX_BYTES_PER_CHAR ]]; then
        warn "bytesPerChar=$bpc exceeds limit of $MAX_BYTES_PER_CHAR at ${current_px}px"
        if [[ $attempt -lt $MAX_RETRY_DECREMENT ]]; then
            warn "Retrying with $((current_px - 1))px..."
            attempt=$((attempt + 1))
            continue
        else
            die "Could not find valid size after $MAX_RETRY_DECREMENT retries"
        fi
    fi

    # Find the output file
    BIN_FILE=$(parse_output_file "$output")
    if [[ -z "$BIN_FILE" || ! -f "$BIN_FILE" ]]; then
        BIN_FILE=$(ls "$OUTPUT_DIR"/${FONT_NAME}_${current_px}_*.bin 2>/dev/null | head -1)
    fi

    if [[ -z "$BIN_FILE" || ! -f "$BIN_FILE" ]]; then
        die "Output .bin file not found for ${current_px}px"
    fi

    # Validate file size (cross-platform stat)
    file_size=$(stat -f%z "$BIN_FILE" 2>/dev/null || stat --printf="%s" "$BIN_FILE" 2>/dev/null)
    if [[ $file_size -gt $MAX_FILE_SIZE ]]; then
        warn "File size ${file_size} bytes exceeds 32MB limit"
        if [[ $attempt -lt $MAX_RETRY_DECREMENT ]]; then
            warn "Retrying with $((current_px - 1))px..."
            rm -f "$BIN_FILE"
            attempt=$((attempt + 1))
            continue
        else
            die "Could not find valid size after $MAX_RETRY_DECREMENT retries"
        fi
    fi

    BIN_BASENAME=$(basename "$BIN_FILE")

    if [[ ${#BIN_BASENAME} -gt $MAX_FONT_NAME_LEN ]]; then
        warn "Filename '${BIN_BASENAME}' is ${#BIN_BASENAME} chars (max $MAX_FONT_NAME_LEN for theme)"
    fi

    size_mb=$(echo "scale=1; $file_size / 1048576" | bc)
    info "Generated: $BIN_BASENAME (${size_mb} MB)"
    echo ""
    break
done

# Generate theme file
if $DARK_MODE; then
    THEME_FILE="$OUTPUT_DIR/dark-$(echo "$FONT_NAME" | tr '[:upper:]' '[:lower:]').theme"
    THEME_DISPLAY="Dark $THEME_NAME"
    BG_COLOR="black"
    TEXT_COLOR="white"
    SEL_FILL="white"
    SEL_TEXT="black"
    INVERTED="true"
else
    THEME_FILE="$OUTPUT_DIR/light-$(echo "$FONT_NAME" | tr '[:upper:]' '[:lower:]').theme"
    THEME_DISPLAY="Light $THEME_NAME"
    BG_COLOR="white"
    TEXT_COLOR="black"
    SEL_FILL="black"
    SEL_TEXT="white"
    INVERTED="false"
fi

# Build header comment based on latin-mode
latin_comment=""
case "$LATIN_MODE" in
    include) latin_comment="# Latin glyphs from $(basename "$LATIN_FONT"), CJK from $(basename "$CJK_FONT")." ;;
    cjk)     latin_comment="# All glyphs (Latin + CJK) rendered from $(basename "$CJK_FONT")." ;;
    system)  latin_comment="# CJK glyphs from $(basename "$CJK_FONT"). Latin uses builtin system font." ;;
esac

file_size=$(stat -f%z "$BIN_FILE" 2>/dev/null || stat --printf="%s" "$BIN_FILE" 2>/dev/null)
size_mb=$(echo "scale=0; ($file_size + 524288) / 1048576" | bc)

cat > "$THEME_FILE" <<EOF
# Papyrix Theme: $THEME_DISPLAY
# Copy this file to /config/themes/ on your SD card.
# Copy $BIN_BASENAME to /config/fonts/ on your SD card.
#
$latin_comment
# Font file: $BIN_BASENAME  ($size_mb MB)

[theme]
name = $THEME_DISPLAY

[colors]
inverted_mode = $INVERTED
background = $BG_COLOR

[selection]
fill_color = $SEL_FILL
text_color = $SEL_TEXT

[text]
primary_color = $TEXT_COLOR
secondary_color = $TEXT_COLOR

[layout]
margin_top = 9
margin_side = 3
item_height = 30
item_spacing = 0

[fonts]
reader_font_xsmall = $BIN_BASENAME
reader_font_small = $BIN_BASENAME
reader_font_medium = $BIN_BASENAME
reader_font_large = $BIN_BASENAME
EOF

info "Generated theme: $THEME_FILE"
echo ""

# Summary
echo "========================================"
echo "  Theme generation complete!"
echo "========================================"
echo ""
echo "Files generated:"
echo "  $BIN_BASENAME"
echo "  $(basename "$THEME_FILE")"
echo ""
echo "To install on your device:"
echo "  1. Copy $BIN_BASENAME to /config/fonts/ on SD card"
echo "  2. Copy $(basename "$THEME_FILE") to /config/themes/ on SD card"
echo "  3. Restart device and select '${THEME_DISPLAY}' in Settings"
echo ""
