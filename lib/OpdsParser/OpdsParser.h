#pragma once
#include <expat.h>

#include <string>
#include <vector>

enum class OpdsEntryType { NAVIGATION, BOOK };

struct OpdsEntry {
  OpdsEntryType type = OpdsEntryType::NAVIGATION;
  std::string title;
  std::string author;
  std::string href;
  std::string id;
};

using OpdsBook = OpdsEntry;

class OpdsParser {
 public:
  OpdsParser() = default;
  ~OpdsParser();

  OpdsParser(const OpdsParser&) = delete;
  OpdsParser& operator=(const OpdsParser&) = delete;

  // Original batch parsing (loads entire XML into memory)
  bool parse(const char* xmlData, size_t length);

  // Streaming parsing - call in order: startParsing() -> feedChunk()... -> finishParsing()
  bool startParsing();
  bool feedChunk(const char* data, size_t len);
  bool finishParsing();

  const std::vector<OpdsEntry>& getEntries() const { return entries; }
  std::vector<OpdsEntry> getBooks() const;
  size_t getEntryCount() const { return entries.size(); }
  const std::string& getSearchTemplate() const { return searchTemplate; }
  const std::string& getOpenSearchUrl() const { return openSearchUrl; }
  void clear();

 private:
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL endElement(void* userData, const XML_Char* name);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  static const char* findAttribute(const XML_Char** atts, const char* name);

  XML_Parser parser = nullptr;
  std::vector<OpdsEntry> entries;
  OpdsEntry currentEntry;
  std::string currentText;
  std::string searchTemplate;  // Direct search URL template (atom+xml)
  std::string openSearchUrl;   // OpenSearch description URL
  bool inEntry = false;
  bool inTitle = false;
  bool inAuthor = false;
  bool inAuthorName = false;
  bool inId = false;
};
