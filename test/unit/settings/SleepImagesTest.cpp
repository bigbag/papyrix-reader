#include <SDCardManager.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/SleepImages.h"
#include "test_utils.h"

using namespace papyrix::sleep_images;

namespace {

std::string bitmap(char pixel = 0) {
  std::string data(58, '\0');
  data[0] = 'B';
  data[1] = 'M';
  data[2] = 58;
  data[10] = 54;
  data[14] = 40;
  data[18] = 1;
  data[22] = 1;
  data[26] = 1;
  data[28] = 24;
  data[34] = 4;
  data[54] = pixel;
  return data;
}

std::string imageName(int index) {
  char name[20];
  snprintf(name, sizeof(name), "image%03d.bmp", index);
  return name;
}

void addImage(std::vector<MockDirectoryEntry>& entries, const std::string& name, char pixel = 0) {
  entries.push_back({name, false});
  SdMan.registerFile("/sleep/" + name, bitmap(pixel));
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("SleepImages");
  Page page;
  FsFile file;
  SdMan.reset();
  loadPage(page, 999);
  runner.expectEq(size_t(0), page.offset, "missing directory clamps to first page");
  runner.expectEq(size_t(1), page.total, "missing directory has Random");
  runner.expectEq(size_t(1), page.count, "Random is one row");
  runner.expectEq(std::string(), std::string(page.names[0]), "Random has empty name");
  runner.expectFalse(openCustomImage("", file), "no image uses default");
  runner.expectFalse(bool(file), "failed lookup closes file");

  std::vector<MockDirectoryEntry> entries;
  for (int i = 0; i < 263; ++i) addImage(entries, imageName(i));
  entries.push_back({".hidden.bmp", false});
  entries.push_back({"nested.bmp", true});
  entries.push_back({"notes.txt", false});
  entries.push_back({"bad.bmp", false});
  entries.push_back({"truncated.bmp", false});
  SdMan.registerFile("/sleep/.hidden.bmp", bitmap());
  SdMan.registerFile("/sleep/notes.txt", bitmap());
  SdMan.registerFile("/sleep/bad.bmp", "not a bitmap");
  SdMan.registerFile("/sleep/truncated.bmp", bitmap().substr(0, 57));
  SdMan.registerDirectory("/sleep", entries);
  loadPage(page, 0);
  runner.expectEq(size_t(264), page.total, "counts more than 255 valid rows");
  runner.expectEq(size_t(8), page.count, "first page is full");
  runner.expectEq(std::string(), std::string(page.names[0]), "first row is Random");
  runner.expectEq(imageName(0), std::string(page.names[1]), "first image follows Random");
  loadPage(page, 999);
  runner.expectEq(size_t(256), page.offset, "clamps to full last page");
  runner.expectEq(size_t(8), page.count, "full last page keeps eight rows");
  runner.expectEq(imageName(262), std::string(page.names[7]), "last image is accessible");

  addImage(entries, imageName(263));
  addImage(entries, imageName(264));
  SdMan.registerDirectory("/sleep", entries);
  loadPage(page, 264);
  runner.expectEq(size_t(266), page.total, "new entries are counted");
  runner.expectEq(size_t(2), page.count, "partial last page has two rows");
  runner.expectEq(imageName(264), std::string(page.names[1]), "partial last row is accessible");
  entries.resize(10);
  SdMan.registerDirectory("/sleep", entries);
  loadPage(page, 264);
  runner.expectEq(size_t(8), page.offset, "removal clamps to new last page");
  runner.expectEq(size_t(3), page.count, "clamped page includes remaining rows");

  runner.expectTrue(openImage("image003.bmp", file), "opens direct valid image");
  runner.expectEq(size_t(0), file.position(), "successful open rewinds");
  file.close();
  runner.expectFalse(openImage(".hidden.bmp", file), "rejects hidden image");
  runner.expectFalse(openImage("../sleep.bmp", file), "rejects parent traversal");
  runner.expectFalse(openImage("/sleep/image003.bmp", file), "rejects absolute path");
  runner.expectFalse(openImage("dir\\image.bmp", file), "rejects backslash path");
  runner.expectFalse(openImage("notes.txt", file), "rejects non-bmp");
  runner.expectFalse(openImage("bad.bmp", file), "rejects invalid bitmap");
  runner.expectFalse(openImage("truncated.bmp", file), "rejects incomplete pixel data");
  runner.expectFalse(openImage(nullptr, file), "rejects null name");
  const std::string oversizedName = std::string(NAME_SIZE - 4, 'a') + ".bmp";
  SdMan.registerFile("/sleep/" + oversizedName, bitmap());
  runner.expectFalse(openImage(oversizedName.c_str(), file), "rejects oversized name");

  SdMan.reset();
  const std::string longestName = std::string(251, 'a') + ".bmp";
  SdMan.registerFile("/sleep/" + longestName, bitmap());
  runner.expectTrue(openImage(longestName.c_str(), file), "accepts a 255-byte basename");
  file.close();
  for (const auto* character : {u8"Ж", u8"ก"}) {
    std::string unicodeName;
    const int letters = strlen(character) == 2 ? 126 : 251;
    for (int i = 0; i < letters; ++i) unicodeName += character;
    unicodeName += ".bmp";
    entries.clear();
    addImage(entries, unicodeName, 'u');
    SdMan.registerDirectory("/sleep", entries);
    loadPage(page, 0);
    runner.expectTrue(page.count == 2 && std::string(page.names[1]) == unicodeName,
                      "lists complete UTF-8 filename beyond 255 bytes");
    runner.expectTrue(openCustomImage(unicodeName.c_str(), file), "opens saved long UTF-8 filename");
    runner.expectTrue(file.seek(54) && file.read() == 'u', "long UTF-8 choice resolves to its image");
    file.close();
  }
  entries.clear();
  addImage(entries, "other.bmp", 'o');
  addImage(entries, "chosen.BMP", 'c');
  SdMan.registerDirectory("/sleep", entries);
  runner.expectTrue(openCustomImage("chosen.BMP", file), "preferred image opens");
  runner.expectTrue(file.seek(54), "preferred pixel is readable");
  runner.expectEq(int('c'), file.read(), "preferred image wins over random");
  file.close();
  SdMan.registerDirectory("/sleep", {{"chosen.BMP", false}, {"other.bmp", false}});
  runner.expectTrue(openCustomImage("chosen.BMP", file), "preferred survives reordering");
  runner.expectTrue(file.seek(54), "reordered preferred pixel is readable");
  runner.expectEq(int('c'), file.read(), "reordering keeps chosen image");
  file.close();
  SdMan.registerDirectory("/sleep", {{"other.bmp", false}});
  SdMan.registerFile("/sleep/chosen.BMP", "corrupt");
  runner.expectTrue(openCustomImage("chosen.BMP", file), "corrupt preferred falls back to random");
  runner.expectTrue(file.seek(54), "random pixel is readable");
  runner.expectEq(int('o'), file.read(), "random fallback selects valid image");
  file.close();
  SdMan.registerFile("/sleep/chosen.BMP", bitmap().substr(0, 57));
  runner.expectTrue(openCustomImage("chosen.BMP", file), "truncated preferred falls back to random");
  runner.expectTrue(file.seek(54), "truncated fallback pixel is readable");
  runner.expectEq(int('o'), file.read(), "truncated preferred does not bypass valid fallback");
  file.close();
  runner.expectTrue(openCustomImage("missing.bmp", file), "missing preferred falls back to random");
  file.close();
  SdMan.registerDirectory("/sleep", {});
  SdMan.registerFile("/sleep.bmp", bitmap('r'));
  runner.expectTrue(openCustomImage("missing.bmp", file), "root bitmap is fallback");
  runner.expectEq(size_t(0), file.position(), "root fallback rewinds");
  runner.expectTrue(file.seek(54), "root pixel is readable");
  runner.expectEq(int('r'), file.read(), "root fallback opens root image");
  file.close();
  SdMan.registerFile("/sleep.bmp", "corrupt");
  runner.expectFalse(openCustomImage("missing.bmp", file), "invalid root uses default");
  runner.expectFalse(bool(file), "invalid root is closed");

  return runner.allPassed() ? 0 : 1;
}
