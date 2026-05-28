#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace html5 {

struct SplitResult {
  int sectionCount = 0;
};

struct SectionEntry {
  uint32_t bodyOffset = 0;
  uint32_t bodyLength = 0;
  std::vector<std::string> tagStack;
};

struct SectionIndex {
  std::string headHtml;
  std::vector<SectionEntry> sections;
};

// Scan a body file and produce a section index (split points + tag stacks).
// Does NOT create section files — just writes a compact .idx file.
// Returns section count (0 on failure).
int scanSectionIndex(const std::string& bodyPath, const std::string& indexPath, size_t bodyStartOffset,
                     size_t bodyEndOffset, size_t maxSectionSize, const std::string& headHtml,
                     uint8_t* ioBuf = nullptr, size_t ioBufSize = 0);

// Read a section index file. Returns true if valid.
bool readSectionIndex(const std::string& indexPath, SectionIndex& index);

// Scan an HTML file for <head>...</head> content and <body...> start offset.
// headHtml receives the raw bytes from <head...> to </head> (inclusive).
// bodyContentStart is set to the byte after the '>' of <body...>.
// bodyContentEnd is set to the byte of '</body>' start (0 = not found, use EOF).
// Returns false if <body> not found (headHtml may still be empty if no <head>).
bool findHtmlHeadAndBody(const std::string& htmlPath, std::string& headHtml, size_t& bodyContentStart,
                         size_t& bodyContentEnd);

// Split an HTML file's body content into sections of at most maxSectionSize bytes.
// Each section is written as: prologue + [reopened tags] + content + [closing tags] + epilogue.
// Splits only at whitespace or tag boundaries — never mid-word.
// Section files are written to outputDir/{filePrefix}_{index}{fileSuffix}, each normalized.
// Returns the number of sections created (0 if file couldn't be opened).
SplitResult splitByByteOffset(const std::string& inputPath, const std::string& outputDir,
                              const std::string& filePrefix, const std::string& fileSuffix,
                              const std::string& prologue, const std::string& epilogue,
                              size_t bodyStartOffset, size_t bodyEndOffset, size_t maxSectionSize,
                              uint8_t* ioBuf = nullptr, size_t ioBufSize = 0,
                              bool skipNormalize = false);

}  // namespace html5
