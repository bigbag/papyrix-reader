/**
 * Lightweight Markdown Parser for ESP32
 *
 * Memory-efficient streaming parser using callbacks.
 * No AST construction - processes text in a single pass.
 *
 * Supported syntax:
 * - Headers (# to ######)
 * - Bold (**text** or __text__)
 * - Italic (*text* or _text_)
 * - Strikethrough (~~text~~)
 * - Inline code (`code`)
 * - Code blocks (```)
 * - Links [text](url)
 * - Images ![alt](url)
 * - Unordered lists (-, *, +)
 * - Ordered lists (1. 2. etc)
 * - Blockquotes (>)
 * - Horizontal rules (---, ***, ___)
 */

#ifndef MD_PARSER_H
#define MD_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Token types */
typedef enum {
  MD_TEXT = 0,
  MD_HEADER_START, /* level in data field */
  MD_HEADER_END,
  MD_BOLD_START,
  MD_BOLD_END,
  MD_ITALIC_START,
  MD_ITALIC_END,
  MD_STRIKE_START,
  MD_STRIKE_END,
  MD_CODE_INLINE,
  MD_CODE_BLOCK_START, /* lang in text if present */
  MD_CODE_BLOCK_END,
  MD_LINK_TEXT_START,
  MD_LINK_TEXT_END,
  MD_LINK_URL,
  MD_IMAGE_ALT_START,
  MD_IMAGE_ALT_END,
  MD_IMAGE_URL,
  MD_LIST_ITEM_START, /* ordered: data=number, unordered: data=0 */
  MD_LIST_ITEM_END,
  MD_BLOCKQUOTE_START,
  MD_BLOCKQUOTE_END,
  MD_HR,
  MD_NEWLINE,
  MD_PARAGRAPH_START,
  MD_PARAGRAPH_END,
} md_token_type_t;

/* Token passed to callback */
typedef struct {
  md_token_type_t type;
  const char* text; /* pointer into source buffer (not null-terminated!) */
  uint16_t length;  /* length of text */
  uint8_t data;     /* extra data (header level, list number, etc) */
} md_token_t;

/* Callback type: return false to stop parsing */
typedef bool (*md_callback_t)(const md_token_t* token, void* user_data);

/* Parser configuration */
typedef struct {
  md_callback_t callback;
  void* user_data;

  /* Feature flags - disable unused features to save code size */
  uint16_t features;
} md_config_t;

/* Feature flags */
#define MD_FEAT_HEADERS (1 << 0)
#define MD_FEAT_BOLD (1 << 1)
#define MD_FEAT_ITALIC (1 << 2)
#define MD_FEAT_STRIKE (1 << 3)
#define MD_FEAT_CODE_INLINE (1 << 4)
#define MD_FEAT_CODE_BLOCK (1 << 5)
#define MD_FEAT_LINKS (1 << 6)
#define MD_FEAT_IMAGES (1 << 7)
#define MD_FEAT_LISTS (1 << 8)
#define MD_FEAT_BLOCKQUOTE (1 << 9)
#define MD_FEAT_HR (1 << 10)
#define MD_FEAT_ALL (0xFFFF)
#define MD_FEAT_BASIC (MD_FEAT_HEADERS | MD_FEAT_BOLD | MD_FEAT_ITALIC | MD_FEAT_CODE_INLINE)

/* Parser state (for streaming/chunked parsing) */
typedef struct {
  md_config_t config;

  /* State machine */
  uint8_t state;
  uint8_t substate;
  uint8_t header_level;
  uint8_t list_number;

  /* Flags */
  uint8_t in_bold : 1;
  uint8_t in_italic : 1;
  uint8_t in_strike : 1;
  uint8_t in_code : 1;
  uint8_t in_code_block : 1;
  uint8_t in_link : 1;
  uint8_t in_blockquote : 1;
  uint8_t line_start : 1;

  /* Small buffer for multi-char tokens (e.g., checking for ** vs *) */
  char peek_buf[4];
  uint8_t peek_len;

  /* Accumulator for text spans */
  const char* span_start;
  uint16_t span_len;
} md_parser_t;

/**
 * Initialize parser with default config (all features enabled)
 */
void md_parser_init(md_parser_t* parser, md_callback_t callback, void* user_data);

/**
 * Initialize parser with custom config
 */
void md_parser_init_ex(md_parser_t* parser, const md_config_t* config);

/**
 * Parse a complete markdown string
 * Returns: number of bytes processed, or -1 on error
 */
int md_parse(md_parser_t* parser, const char* input, size_t length);

/**
 * Parse markdown in chunks (for streaming)
 * Call md_parse_end() after last chunk
 */
int md_parse_chunk(md_parser_t* parser, const char* chunk, size_t length);
void md_parse_end(md_parser_t* parser);

/**
 * Reset parser state (reuse parser for new document)
 */
void md_parser_reset(md_parser_t* parser);

/**
 * Utility: get human-readable token type name
 */
const char* md_token_name(md_token_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* MD_PARSER_H */
