#pragma once
#include <string>

namespace html5 {

// Normalize HTML for Expat (XML) parsing:
//  - Self-close void elements: <img src="x"> → <img src="x" />
//  - Escape '<' inside quoted attribute values → &lt;
//  - Normalize bare boolean attributes: defer → defer=""
//  - Force-close tags when '<' appears in unquoted attribute area
bool normalizeHtmlForXml(const std::string& inputPath, const std::string& outputPath);

}  // namespace html5
