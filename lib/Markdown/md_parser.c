/**
 * Lightweight Markdown Parser for ESP32
 * Implementation
 */

#include "md_parser.h"

#include <string.h>

/* Internal states */
enum {
  STATE_LINE_START = 0,
  STATE_TEXT,
  STATE_MAYBE_BOLD,
  STATE_MAYBE_ITALIC,
  STATE_MAYBE_STRIKE,
  STATE_CODE_BLOCK_FENCE,
  STATE_CODE_BLOCK,
  STATE_LINK_TEXT,
  STATE_LINK_URL,
  STATE_IMAGE_ALT,
  STATE_IMAGE_URL,
};

/* Helper: emit token via callback */
static inline bool emit(md_parser_t* p, md_token_type_t type, const char* text, uint16_t len, uint8_t data) {
  md_token_t token = {.type = type, .text = text, .length = len, .data = data};
  return p->config.callback(&token, p->config.user_data);
}

/* Helper: emit accumulated text span */
static inline bool flush_span(md_parser_t* p) {
  if (p->span_len > 0) {
    bool ok = emit(p, MD_TEXT, p->span_start, p->span_len, 0);
    p->span_len = 0;
    p->span_start = NULL;
    return ok;
  }
  return true;
}

/* Helper: check if feature is enabled */
static inline bool has_feat(md_parser_t* p, uint16_t feat) { return (p->config.features & feat) != 0; }

/* Helper: check for repeated char (e.g., ---, ***, ===) */
static size_t count_char(const char* s, size_t max, char c) {
  size_t count = 0;
  while (count < max && s[count] == c) count++;
  return count;
}

/* Helper: skip whitespace, return count */
static size_t skip_space(const char* s, size_t max) {
  size_t count = 0;
  while (count < max && (s[count] == ' ' || s[count] == '\t')) count++;
  return count;
}

/* Helper: check if line is blank (only whitespace until newline/end) */
static bool is_blank_line(const char* s, size_t max) {
  for (size_t i = 0; i < max; i++) {
    if (s[i] == '\n' || s[i] == '\0') return true;
    if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return false;
  }
  return true;
}

/* Helper: find character, return offset or -1 */
static int find_char(const char* s, size_t max, char c) {
  for (size_t i = 0; i < max; i++) {
    if (s[i] == c) return (int)i;
    if (s[i] == '\n' || s[i] == '\0') return -1;
  }
  return -1;
}

/* Process line start - headers, lists, blockquotes, HR */
static size_t process_line_start(md_parser_t* p, const char* input, size_t remaining) {
  size_t consumed = 0;
  size_t spaces = skip_space(input, remaining);

  if (spaces >= remaining) return spaces;

  const char* s = input + spaces;
  size_t rem = remaining - spaces;

  /* Code block fence ``` */
  if (has_feat(p, MD_FEAT_CODE_BLOCK) && rem >= 3 && s[0] == '`' && s[1] == '`' && s[2] == '`') {
    flush_span(p);

    if (p->in_code_block) {
      emit(p, MD_CODE_BLOCK_END, NULL, 0, 0);
      p->in_code_block = 0;
      /* Skip to end of line */
      int nl = find_char(s, rem, '\n');
      return spaces + (nl >= 0 ? nl + 1 : rem);
    } else {
      /* Find language hint */
      int lang_start = 3;
      int lang_end = find_char(s + 3, rem - 3, '\n');
      if (lang_end < 0) lang_end = rem - 3;

      /* Trim whitespace from lang */
      while (lang_start < 3 + lang_end && s[lang_start] == ' ') lang_start++;
      int lang_len = lang_end - (lang_start - 3);
      while (lang_len > 0 && s[lang_start + lang_len - 1] == ' ') lang_len--;

      emit(p, MD_CODE_BLOCK_START, s + lang_start, lang_len, 0);
      p->in_code_block = 1;

      int nl = find_char(s, rem, '\n');
      return spaces + (nl >= 0 ? nl + 1 : rem);
    }
  }

  /* Inside code block - emit as-is */
  if (p->in_code_block) {
    int nl = find_char(input, remaining, '\n');
    if (nl >= 0) {
      emit(p, MD_TEXT, input, nl, 0);
      emit(p, MD_NEWLINE, NULL, 0, 0);
      return nl + 1;
    } else {
      emit(p, MD_TEXT, input, remaining, 0);
      return remaining;
    }
  }

  /* Headers # */
  if (has_feat(p, MD_FEAT_HEADERS) && s[0] == '#') {
    int level = count_char(s, rem, '#');
    if (level <= 6 && level < rem && (s[level] == ' ' || s[level] == '\t')) {
      flush_span(p);
      p->header_level = level;
      emit(p, MD_HEADER_START, NULL, 0, level);
      return spaces + level + 1; /* +1 for space after # */
    }
  }

  /* Horizontal rule --- *** ___ */
  if (has_feat(p, MD_FEAT_HR) && rem >= 3) {
    char c = s[0];
    if (c == '-' || c == '*' || c == '_') {
      int cnt = 0;
      bool valid = true;
      for (size_t i = 0; i < rem && s[i] != '\n'; i++) {
        if (s[i] == c)
          cnt++;
        else if (s[i] != ' ' && s[i] != '\t') {
          valid = false;
          break;
        }
      }
      if (valid && cnt >= 3) {
        flush_span(p);
        emit(p, MD_HR, NULL, 0, 0);
        int nl = find_char(s, rem, '\n');
        return spaces + (nl >= 0 ? nl + 1 : rem);
      }
    }
  }

  /* Blockquote > */
  if (has_feat(p, MD_FEAT_BLOCKQUOTE) && s[0] == '>') {
    flush_span(p);
    if (!p->in_blockquote) {
      emit(p, MD_BLOCKQUOTE_START, NULL, 0, 0);
      p->in_blockquote = 1;
    }
    consumed = spaces + 1;
    if (consumed < remaining && input[consumed] == ' ') consumed++;
    return consumed;
  } else if (p->in_blockquote && !is_blank_line(s, rem)) {
    /* Continue blockquote on non-blank lines */
  } else if (p->in_blockquote) {
    emit(p, MD_BLOCKQUOTE_END, NULL, 0, 0);
    p->in_blockquote = 0;
  }

  /* Unordered list - * + */
  if (has_feat(p, MD_FEAT_LISTS) && rem >= 2 && (s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') {
    flush_span(p);
    emit(p, MD_LIST_ITEM_START, NULL, 0, 0);
    return spaces + 2;
  }

  /* Ordered list 1. 2. etc */
  if (has_feat(p, MD_FEAT_LISTS) && s[0] >= '0' && s[0] <= '9') {
    int num_len = 0;
    int num = 0;
    while (num_len < rem && s[num_len] >= '0' && s[num_len] <= '9') {
      if (num <= 25) {
        num = num * 10 + (s[num_len] - '0');
      }
      num_len++;
    }
    if (num > 255) num = 255;
    if (num_len > 0 && num_len + 1 < rem && s[num_len] == '.' && s[num_len + 1] == ' ') {
      flush_span(p);
      emit(p, MD_LIST_ITEM_START, NULL, 0, (uint8_t)num);
      return spaces + num_len + 2;
    }
  }

  return spaces;
}

/* Process inline formatting */
static size_t process_inline(md_parser_t* p, const char* input, size_t remaining) {
  if (remaining == 0) return 0;

  char c = input[0];

  /* Escape character */
  if (c == '\\' && remaining > 1) {
    char next = input[1];
    if (next == '*' || next == '_' || next == '`' || next == '[' || next == ']' || next == '(' || next == ')' ||
        next == '#' || next == '~' || next == '!' || next == '\\') {
      /* Emit escaped char as text */
      flush_span(p);
      emit(p, MD_TEXT, input + 1, 1, 0);
      return 2;
    }
  }

  /* Inline code ` */
  if (has_feat(p, MD_FEAT_CODE_INLINE) && c == '`') {
    int end = find_char(input + 1, remaining - 1, '`');
    if (end >= 0) {
      flush_span(p);
      emit(p, MD_CODE_INLINE, input + 1, end, 0);
      return end + 2;
    }
  }

  /* Bold ** or __ */
  if (has_feat(p, MD_FEAT_BOLD) && remaining >= 2 && ((c == '*' && input[1] == '*') || (c == '_' && input[1] == '_'))) {
    flush_span(p);
    if (p->in_bold) {
      emit(p, MD_BOLD_END, NULL, 0, 0);
      p->in_bold = 0;
    } else {
      emit(p, MD_BOLD_START, NULL, 0, 0);
      p->in_bold = 1;
    }
    return 2;
  }

  /* Strikethrough ~~ */
  if (has_feat(p, MD_FEAT_STRIKE) && remaining >= 2 && c == '~' && input[1] == '~') {
    flush_span(p);
    if (p->in_strike) {
      emit(p, MD_STRIKE_END, NULL, 0, 0);
      p->in_strike = 0;
    } else {
      emit(p, MD_STRIKE_START, NULL, 0, 0);
      p->in_strike = 1;
    }
    return 2;
  }

  /* Italic * or _ (single) */
  if (has_feat(p, MD_FEAT_ITALIC) && (c == '*' || c == '_')) {
    /* Make sure it's not ** or __ */
    if (remaining < 2 || input[1] != c) {
      flush_span(p);
      if (p->in_italic) {
        emit(p, MD_ITALIC_END, NULL, 0, 0);
        p->in_italic = 0;
      } else {
        emit(p, MD_ITALIC_START, NULL, 0, 0);
        p->in_italic = 1;
      }
      return 1;
    }
  }

  /* Image ![alt](url) */
  if (has_feat(p, MD_FEAT_IMAGES) && c == '!' && remaining >= 2 && input[1] == '[') {
    int alt_end = find_char(input + 2, remaining - 2, ']');
    if (alt_end >= 0 && alt_end + 3 < remaining && input[alt_end + 3] == '(') {
      int url_end = find_char(input + alt_end + 4, remaining - alt_end - 4, ')');
      if (url_end >= 0) {
        flush_span(p);
        emit(p, MD_IMAGE_ALT_START, NULL, 0, 0);
        emit(p, MD_TEXT, input + 2, alt_end, 0);
        emit(p, MD_IMAGE_ALT_END, NULL, 0, 0);
        emit(p, MD_IMAGE_URL, input + alt_end + 4, url_end, 0);
        return alt_end + url_end + 5;
      }
    }
  }

  /* Link [text](url) */
  if (has_feat(p, MD_FEAT_LINKS) && c == '[') {
    int text_end = find_char(input + 1, remaining - 1, ']');
    if (text_end >= 0 && text_end + 2 < remaining && input[text_end + 2] == '(') {
      int url_end = find_char(input + text_end + 3, remaining - text_end - 3, ')');
      if (url_end >= 0) {
        flush_span(p);
        emit(p, MD_LINK_TEXT_START, NULL, 0, 0);
        emit(p, MD_TEXT, input + 1, text_end, 0);
        emit(p, MD_LINK_TEXT_END, NULL, 0, 0);
        emit(p, MD_LINK_URL, input + text_end + 3, url_end, 0);
        return text_end + url_end + 4;
      }
    }
  }

  /* Newline */
  if (c == '\n') {
    flush_span(p);

    /* End header if active */
    if (p->header_level > 0) {
      emit(p, MD_HEADER_END, NULL, 0, p->header_level);
      p->header_level = 0;
    }

    emit(p, MD_NEWLINE, NULL, 0, 0);
    p->line_start = 1;
    return 1;
  }

  /* Regular text - accumulate */
  if (p->span_start == NULL) {
    p->span_start = input;
    p->span_len = 1;
  } else if (p->span_start + p->span_len == input) {
    /* Contiguous - extend span */
    p->span_len++;
  } else {
    /* Non-contiguous - flush and start new */
    flush_span(p);
    p->span_start = input;
    p->span_len = 1;
  }

  return 1;
}

/* Public API */

void md_parser_init(md_parser_t* parser, md_callback_t callback, void* user_data) {
  md_config_t config = {.callback = callback, .user_data = user_data, .features = MD_FEAT_ALL};
  md_parser_init_ex(parser, &config);
}

void md_parser_init_ex(md_parser_t* parser, const md_config_t* config) {
  memset(parser, 0, sizeof(*parser));
  parser->config = *config;
  parser->line_start = 1;
}

void md_parser_reset(md_parser_t* parser) {
  md_config_t config = parser->config;
  memset(parser, 0, sizeof(*parser));
  parser->config = config;
  parser->line_start = 1;
}

int md_parse(md_parser_t* parser, const char* input, size_t length) {
  int result = md_parse_chunk(parser, input, length);
  if (result >= 0) {
    md_parse_end(parser);
  }
  return result;
}

int md_parse_chunk(md_parser_t* parser, const char* chunk, size_t length) {
  size_t pos = 0;

  while (pos < length) {
    size_t consumed = 0;

    if (parser->line_start) {
      consumed = process_line_start(parser, chunk + pos, length - pos);
      if (consumed > 0) {
        pos += consumed;
        parser->line_start = 0;
        continue;
      }
      parser->line_start = 0;
    }

    consumed = process_inline(parser, chunk + pos, length - pos);
    if (consumed == 0) {
      /* Should not happen, but prevent infinite loop */
      pos++;
    } else {
      pos += consumed;
    }
  }

  return (int)pos;
}

void md_parse_end(md_parser_t* parser) {
  /* Flush any remaining text */
  flush_span(parser);

  /* Close any open elements */
  if (parser->header_level > 0) {
    emit(parser, MD_HEADER_END, NULL, 0, parser->header_level);
    parser->header_level = 0;
  }
  if (parser->in_bold) {
    emit(parser, MD_BOLD_END, NULL, 0, 0);
    parser->in_bold = 0;
  }
  if (parser->in_italic) {
    emit(parser, MD_ITALIC_END, NULL, 0, 0);
    parser->in_italic = 0;
  }
  if (parser->in_strike) {
    emit(parser, MD_STRIKE_END, NULL, 0, 0);
    parser->in_strike = 0;
  }
  if (parser->in_code_block) {
    emit(parser, MD_CODE_BLOCK_END, NULL, 0, 0);
    parser->in_code_block = 0;
  }
  if (parser->in_blockquote) {
    emit(parser, MD_BLOCKQUOTE_END, NULL, 0, 0);
    parser->in_blockquote = 0;
  }
}

const char* md_token_name(md_token_type_t type) {
  static const char* names[] = {
      "TEXT",
      "HEADER_START",
      "HEADER_END",
      "BOLD_START",
      "BOLD_END",
      "ITALIC_START",
      "ITALIC_END",
      "STRIKE_START",
      "STRIKE_END",
      "CODE_INLINE",
      "CODE_BLOCK_START",
      "CODE_BLOCK_END",
      "LINK_TEXT_START",
      "LINK_TEXT_END",
      "LINK_URL",
      "IMAGE_ALT_START",
      "IMAGE_ALT_END",
      "IMAGE_URL",
      "LIST_ITEM_START",
      "LIST_ITEM_END",
      "BLOCKQUOTE_START",
      "BLOCKQUOTE_END",
      "HR",
      "NEWLINE",
      "PARAGRAPH_START",
      "PARAGRAPH_END",
  };
  if (type < sizeof(names) / sizeof(names[0])) {
    return names[type];
  }
  return "UNKNOWN";
}
