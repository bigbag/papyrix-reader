#pragma once

#include <expat.h>

#include <cctype>

#include "EncodingTables.h"

inline bool encodingNameEquals(const char* name, const char* target) {
  while (*name && *target) {
    char a = static_cast<char>(tolower(static_cast<unsigned char>(*name)));
    char b = *target;
    if (a == '-' || a == '_') {
      name++;
      continue;
    }
    if (b == '-' || b == '_') {
      target++;
      continue;
    }
    if (a != b) return false;
    name++;
    target++;
  }
  while (*name == '-' || *name == '_') name++;
  while (*target == '-' || *target == '_') target++;
  return *name == '\0' && *target == '\0';
}

inline void fillEncodingMap(XML_Encoding* info, const int* highTable) {
  for (int i = 0; i < 128; i++) info->map[i] = i;
  for (int i = 0; i < 128; i++) info->map[128 + i] = highTable[i];
  info->data = nullptr;
  info->convert = nullptr;
  info->release = nullptr;
}

inline int XMLCALL expatUnknownEncodingHandler(void*, const XML_Char* name, XML_Encoding* info) {
  if (encodingNameEquals(name, "windows-1251") || encodingNameEquals(name, "cp1251")) {
    fillEncodingMap(info, kWindows1251High);
    return XML_STATUS_OK;
  }
  if (encodingNameEquals(name, "koi8-r") || encodingNameEquals(name, "koi8r")) {
    fillEncodingMap(info, kKoi8RHigh);
    return XML_STATUS_OK;
  }
  if (encodingNameEquals(name, "iso-8859-1") || encodingNameEquals(name, "latin1") ||
      encodingNameEquals(name, "latin-1")) {
    fillEncodingMap(info, kIso8859_1High);
    return XML_STATUS_OK;
  }
  if (encodingNameEquals(name, "windows-1252") || encodingNameEquals(name, "cp1252")) {
    fillEncodingMap(info, kCp1252High);
    return XML_STATUS_OK;
  }
  return XML_STATUS_ERROR;
}
