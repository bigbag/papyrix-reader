#pragma once

#include <cstdint>
#include <cstring>

enum {
  PJPG_NO_MORE_BLOCKS = 1,
  PJPG_STREAM_READ_ERROR = 45,
};

typedef enum { PJPG_GRAYSCALE, PJPG_YH1V1, PJPG_YH2V1, PJPG_YH1V2, PJPG_YH2V2 } pjpeg_scan_type_t;

typedef struct {
  int m_width;
  int m_height;
  int m_comps;
  int m_MCUSPerRow;
  int m_MCUSPerCol;
  pjpeg_scan_type_t m_scanType;
  int m_MCUWidth;
  int m_MCUHeight;
  unsigned char* m_pMCUBufR;
  unsigned char* m_pMCUBufG;
  unsigned char* m_pMCUBufB;
} pjpeg_image_info_t;

typedef unsigned char (*pjpeg_need_bytes_callback_t)(unsigned char*, unsigned char, unsigned char*, void*);

#define PJPG_GRAYSCALE_ONLY 2

namespace fake_picojpeg {
inline pjpeg_need_bytes_callback_t callback = nullptr;
inline void* callbackData = nullptr;
inline unsigned char pixels[64] = {};
inline int mcu = 0;
}  // namespace fake_picojpeg

inline unsigned char pjpeg_decode_init(pjpeg_image_info_t* info, pjpeg_need_bytes_callback_t callback,
                                       void* callbackData, unsigned char) {
  fake_picojpeg::callback = callback;
  fake_picojpeg::callbackData = callbackData;
  fake_picojpeg::mcu = 0;
  unsigned char input[8] = {};
  unsigned char read = 0;
  const unsigned char status = callback(input, sizeof(input), &read, callbackData);
  if (status != 0) return status;

  info->m_width = 8;
  info->m_height = 16;
  info->m_comps = 1;
  info->m_MCUSPerRow = 1;
  info->m_MCUSPerCol = 2;
  info->m_scanType = PJPG_GRAYSCALE;
  info->m_MCUWidth = 8;
  info->m_MCUHeight = 8;
  info->m_pMCUBufR = fake_picojpeg::pixels;
  info->m_pMCUBufG = nullptr;
  info->m_pMCUBufB = nullptr;
  return 0;
}

inline unsigned char pjpeg_decode_mcu() {
  if (fake_picojpeg::mcu >= 2) return PJPG_NO_MORE_BLOCKS;
  unsigned char input[32] = {};
  unsigned char read = 0;
  const unsigned char status = fake_picojpeg::callback(input, sizeof(input), &read, fake_picojpeg::callbackData);
  if (status != 0) return status;
  for (int i = 0; i < 64; ++i) {
    fake_picojpeg::pixels[i] = static_cast<unsigned char>((i * 4 + fake_picojpeg::mcu * 32) & 0xFF);
  }
  ++fake_picojpeg::mcu;
  return 0;
}
