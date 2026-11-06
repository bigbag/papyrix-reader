#pragma once

namespace miniapps {

template <typename File, typename Storage, typename Writer>
bool publishCheckedFile(Storage& storage, const char* finalPath, const char* tempPath, const char* backupPath,
                        bool hasFinal, Writer write) {
  File file;
  if (!storage.openWrite(tempPath, file).ok()) return false;
  const bool written = write(file);
  const bool synced = written && file.sync();
  const bool closed = file.close();
  if (!synced || !closed) {
    storage.remove(tempPath);
    return false;
  }
  if (hasFinal && !storage.rename(finalPath, backupPath).ok()) {
    storage.remove(tempPath);
    return false;
  }
  if (!storage.rename(tempPath, finalPath).ok()) {
    storage.remove(finalPath);
    if (hasFinal) storage.rename(backupPath, finalPath);
    storage.remove(tempPath);
    return false;
  }
  if (hasFinal) storage.remove(backupPath);
  return true;
}

}  // namespace miniapps
