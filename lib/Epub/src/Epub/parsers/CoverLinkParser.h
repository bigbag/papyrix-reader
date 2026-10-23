#pragma once
#include <Print.h>
#include <expat.h>

#include <cstdint>
#include <string>
#include <vector>

constexpr size_t MAX_COVER_LINKS = 32;
constexpr size_t MAX_COVER_LINK_LENGTH = 512;
constexpr uint8_t MAX_COVER_XML_DEPTH = 50;

// Use links only after complete() returns true.
class CoverLinkParser final : public Print {
  size_t remainingSize;
  bool landmarksMode;
  XML_Parser parser = nullptr;
  bool complete_ = false;
  bool insideLandmarks = false;
  uint8_t landmarksDepth = 0;
  uint8_t depth = 0;
  std::vector<std::string> links_;

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL endElement(void* userData, const XML_Char* name);
  static void XMLCALL startDoctypeDecl(void* userData, const XML_Char* doctypeName, const XML_Char* sysid,
                                       const XML_Char* pubid, int hasInternalSubset);

  void fail();
  void addLink(const XML_Char* value);

 public:
  CoverLinkParser(size_t xmlSize, bool landmarks);
  ~CoverLinkParser() override;

  bool setup();

  bool complete() const { return complete_; }
  const std::vector<std::string>& links() const { return links_; }

  size_t write(uint8_t data) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
