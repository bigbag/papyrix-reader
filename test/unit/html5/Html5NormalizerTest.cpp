#include "test_utils.h"

#include <Html5Normalizer.h>
#include <SDCardManager.h>

#include <string>

static std::string normalize(const std::string& input) {
  SdMan.reset();
  SdMan.registerFile("/in.html", input);
  html5::normalizeHtmlForXml("/in.html", "/out.html");
  return SdMan.getWrittenData("/out.html");
}

int main() {
  TestUtils::TestRunner runner("Html5Normalizer");

  // ============================================
  // Existing behavior: void element self-closing
  // ============================================

  {
    auto out = normalize("<img src=\"x\">");
    runner.expectEqual("<img src=\"x\" />", out, "void: img self-closed");
  }

  {
    auto out = normalize("<br>");
    runner.expectEqual("<br />", out, "void: br self-closed");
  }

  {
    auto out = normalize("<hr>");
    runner.expectEqual("<hr />", out, "void: hr self-closed");
  }

  {
    auto out = normalize("<img src=\"x\" />");
    runner.expectEqual("<img src=\"x\" />", out, "void: already self-closed unchanged");
  }

  {
    auto out = normalize("<meta charset=\"utf-8\">");
    runner.expectEqual("<meta charset=\"utf-8\" />", out, "void: meta self-closed");
  }

  {
    auto out = normalize("<link rel=\"stylesheet\" href=\"s.css\">");
    runner.expectEqual("<link rel=\"stylesheet\" href=\"s.css\" />", out, "void: link self-closed");
  }

  {
    auto out = normalize("<input type=\"text\" value=\"hello\">");
    runner.expectEqual("<input type=\"text\" value=\"hello\" />", out, "void: input self-closed");
  }

  // ============================================
  // Existing behavior: void closing tags removed
  // ============================================

  {
    auto out = normalize("text</br>more");
    runner.expectEqual("textmore", out, "void: closing </br> removed");
  }

  {
    auto out = normalize("text</img>more");
    runner.expectEqual("textmore", out, "void: closing </img> removed");
  }

  // ============================================
  // Existing behavior: non-void tags preserved
  // ============================================

  {
    auto out = normalize("<div class=\"x\">text</div>");
    runner.expectEqual("<div class=\"x\">text</div>", out, "non-void: div preserved");
  }

  {
    auto out = normalize("<p>hello</p>");
    runner.expectEqual("<p>hello</p>", out, "non-void: p preserved");
  }

  {
    auto out = normalize("<a href=\"url\">link</a>");
    runner.expectEqual("<a href=\"url\">link</a>", out, "non-void: a preserved");
  }

  // ============================================
  // New: escape < inside quoted attribute values
  // ============================================

  {
    auto out = normalize("<div class=\"a<b\">text</div>");
    runner.expectEqual("<div class=\"a&lt;b\">text</div>", out, "quote: < escaped to &lt;");
  }

  {
    auto out = normalize("<div class=\"a<b<c\">text</div>");
    runner.expectEqual("<div class=\"a&lt;b&lt;c\">text</div>", out, "quote: multiple < escaped");
  }

  {
    auto out = normalize("<div class='a<b'>text</div>");
    runner.expectEqual("<div class='a&lt;b'>text</div>", out, "quote: single quotes < escaped");
  }

  {
    auto out = normalize("<div class=\"no angle\">text</div>");
    runner.expectEqual("<div class=\"no angle\">text</div>", out, "quote: no < unchanged");
  }

  // ============================================
  // New: bare/boolean attributes normalized
  // ============================================

  {
    auto out = normalize("<script defer></script>");
    runner.expectEqual("<script defer=\"\"></script>", out, "bool: defer normalized");
  }

  {
    auto out = normalize("<script async defer></script>");
    runner.expectEqual("<script async=\"\" defer=\"\"></script>", out, "bool: multiple booleans");
  }

  {
    auto out = normalize("<input disabled type=\"text\">");
    runner.expectEqual("<input disabled=\"\" type=\"text\" />", out, "bool: mixed with valued attr");
  }

  {
    auto out = normalize("<div hidden>text</div>");
    runner.expectEqual("<div hidden=\"\">text</div>", out, "bool: hidden on non-void");
  }

  {
    auto out = normalize("<input type=\"text\" disabled>");
    runner.expectEqual("<input type=\"text\" disabled=\"\" />", out, "bool: trailing boolean on void");
  }

  // ============================================
  // New: < in attribute area force-closes tag
  // ============================================

  {
    auto out = normalize("<div foo <p>text</p>");
    runner.expectEqual("<div foo=\"\" ><p>text</p>", out, "attr-lt: < force-closes tag");
  }

  {
    auto out = normalize("<span x <em>text</em>");
    runner.expectEqual("<span x=\"\" ><em>text</em>", out, "attr-lt: bare attr then < closes");
  }

  // ============================================
  // New: space inserted after closing quote
  // ============================================

  {
    auto out = normalize("<div class=\"a\"id=\"b\">text</div>");
    runner.expectEqual("<div class=\"a\" id=\"b\">text</div>", out, "space: inserted between attrs");
  }

  {
    auto out = normalize("<div class=\"a\" id=\"b\">text</div>");
    runner.expectEqual("<div class=\"a\" id=\"b\">text</div>", out, "space: already spaced unchanged");
  }

  // ============================================
  // The actual Esperanto corruption pattern
  // ============================================

  {
    std::string input =
        "<body class=\"name tutateksto <body class=\"name tutateksto"
        "  <div class=\"tekstarkapo\"> <h1>Hello</h1></body>";
    auto out = normalize(input);
    // Must produce valid XML: body closes, div is a separate tag
    runner.expectTrue(out.find("&lt;body") != std::string::npos,
                      "esperanto: < inside attr value escaped");
    runner.expectTrue(out.find("<div class=\"tekstarkapo\">") != std::string::npos,
                      "esperanto: div tag preserved as separate tag");
    runner.expectTrue(out.find("<h1>Hello</h1>") != std::string::npos,
                      "esperanto: content after corruption preserved");
  }

  {
    std::string input =
        "<body class=\"aldono tutateksto <body class=\"aldono tutateksto"
        "  <div class=\"tekstarkapo\"> text</div></body>";
    auto out = normalize(input);
    runner.expectTrue(out.find("aldono=\"\"") != std::string::npos,
                      "esperanto: bare garbled attr normalized");
    runner.expectTrue(out.find("tutateksto=\"\"") != std::string::npos,
                      "esperanto: second bare attr normalized");
  }

  // ============================================
  // Edge cases
  // ============================================

  {
    auto out = normalize("");
    runner.expectEqual("", out, "edge: empty input");
  }

  {
    auto out = normalize("plain text no tags");
    runner.expectEqual("plain text no tags", out, "edge: no tags");
  }

  {
    auto out = normalize("<");
    runner.expectEqual("<", out, "edge: lone < at EOF");
  }

  {
    auto out = normalize("</");
    runner.expectEqual("</", out, "edge: lone </ at EOF");
  }

  {
    auto out = normalize("<div hidden");
    runner.expectEqual("<div hidden=\"\">", out, "edge: EOF in attrs with bare attr");
  }

  {
    auto out = normalize("<div class=\"hello");
    runner.expectEqual("<div class=\"hello\">", out, "edge: EOF in unclosed quote");
  }

  {
    auto out = normalize("<img src=\"x\"");
    runner.expectEqual("<img src=\"x\" />", out, "edge: EOF in void tag attrs");
  }

  {
    auto out = normalize("<div class");
    runner.expectEqual("<div class=\"\">", out, "edge: EOF mid attr name");
  }

  {
    auto out = normalize("<!DOCTYPE html><html><body>x</body></html>");
    runner.expectEqual("<!DOCTYPE html><html><body>x</body></html>", out,
                       "edge: DOCTYPE passthrough");
  }

  {
    auto out = normalize("<?xml version=\"1.0\"?><root/>");
    runner.expectEqual("<?xml version=\"1.0\"?><root/>", out, "edge: PI passthrough");
  }

  {
    auto out = normalize("<div class=\"a&amp;b\">text</div>");
    runner.expectEqual("<div class=\"a&amp;b\">text</div>", out, "edge: entities in attrs untouched");
  }

  {
    auto out = normalize("<p>text with <br> inline</p>");
    runner.expectEqual("<p>text with <br /> inline</p>", out, "mixed: br inside p");
  }

  {
    auto out = normalize("<div><img src=\"a.jpg\"><p>text</p></div>");
    runner.expectEqual("<div><img src=\"a.jpg\" /><p>text</p></div>", out, "mixed: img then p");
  }

  // ============================================
  // Normalizer failure paths
  // ============================================

  {
    SdMan.reset();
    SdMan.registerFile("/in.html", "<p>test</p>");
    SdMan.setOpenFileForReadFailCount(1);
    bool result = html5::normalizeHtmlForXml("/in.html", "/out.html");
    runner.expectFalse(result, "fail: returns false on read open failure");
  }

  return runner.allPassed() ? 0 : 1;
}
