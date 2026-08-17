#pragma once

#include <cstddef>
#include <cstdint>

struct pngle_t;
using pngle_init_callback_t = void (*)(pngle_t*, uint32_t, uint32_t);
using pngle_draw_callback_t = void (*)(pngle_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint8_t[4]);

struct pngle_t {
  void* userData = nullptr;
  pngle_init_callback_t init = nullptr;
  pngle_draw_callback_t draw = nullptr;
  bool emitted = false;
};

inline pngle_t* pngle_new() { return new pngle_t(); }
inline void pngle_destroy(pngle_t* pngle) { delete pngle; }
inline void pngle_set_user_data(pngle_t* pngle, void* data) { pngle->userData = data; }
inline void* pngle_get_user_data(pngle_t* pngle) { return pngle->userData; }
inline void pngle_set_init_callback(pngle_t* pngle, pngle_init_callback_t callback) { pngle->init = callback; }
inline void pngle_set_draw_callback(pngle_t* pngle, pngle_draw_callback_t callback) { pngle->draw = callback; }
inline const char* pngle_error(pngle_t*) { return "fake pngle error"; }

inline int pngle_feed(pngle_t* pngle, const void*, size_t length) {
  if (pngle->emitted) return static_cast<int>(length);
  pngle->emitted = true;
  pngle->init(pngle, 2, 2);

  uint8_t black[4] = {0, 0, 0, 255};
  uint8_t gray[4] = {128, 128, 128, 255};
  uint8_t transparentBlack[4] = {0, 0, 0, 0};
  uint8_t white[4] = {255, 255, 255, 255};
  pngle->draw(pngle, 0, 0, 1, 1, black);
  pngle->draw(pngle, 1, 0, 1, 1, gray);
  pngle->draw(pngle, 0, 1, 1, 1, transparentBlack);
  pngle->draw(pngle, 1, 1, 1, 1, white);
  return static_cast<int>(length);
}
