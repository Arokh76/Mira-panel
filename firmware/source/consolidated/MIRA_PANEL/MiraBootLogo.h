#pragma once
// Mira WT32 splash logo: crisp 200x200 bitmap derived from the original
// Mira_Panel_Icon_FINAL_V2_TRANSPARENT.png 512x512 supplied by the author.
// Source was Lanczos-downsampled to exactly 200x200, composited against
// the splash background #071421 and JPEG-encoded at quality 95.
// JPEG data is stored in FLASH; only the temporary RGB565 frame uses PSRAM.
// This replaces the former 35x28 logo enlarged 4x (pixelated).
#if !defined(MIRA_TARGET_WAVESHARE_43C) && !defined(MIRA_TARGET_NEXTION_28)
#include <Arduino.h>
#include <lvgl.h>
#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <new>
#include <stdint.h>
#include <string.h>

namespace MiraBootLogo {
static constexpr uint16_t W = 200;
static constexpr uint16_t H = 200;
static constexpr unsigned char JPEG_BASE64[] =
  "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"
  "4IdRSLaPOEqIQ//Z"
;

struct Frame {
  uint16_t* pixels = nullptr;
  uint16_t width = W;
  uint16_t height = H;
};

inline uint16_t*& storage() {
  static uint16_t* buffer = nullptr;
  return buffer;
}

inline lv_img_dsc_t& descriptor() {
  static lv_img_dsc_t d = {};
  return d;
}

inline int drawMcu(JPEGDRAW* part) {
  if (!part || !part->pUser || !part->pPixels || part->iBpp != 16) return 0;
  Frame* f = static_cast<Frame*>(part->pUser);
  if (!f->pixels) return 0;
  for (int y = 0; y < part->iHeight; ++y) {
    const int dstY = part->y + y;
    if (dstY < 0 || dstY >= f->height) continue;
    const uint16_t* source = part->pPixels + (size_t)y * part->iWidth;
    uint16_t* dest = f->pixels + (size_t)dstY * f->width;
    for (int x = 0; x < part->iWidthUsed; ++x) {
      const int dstX = part->x + x;
      if (dstX >= 0 && dstX < f->width) dest[dstX] = source[x];
    }
  }
  return 1;
}

// The splash holds this descriptor only while visible; call release()
// immediately after deleting the splash LVGL object.
inline const lv_img_dsc_t* image() {
  lv_img_dsc_t& d = descriptor();
  if (storage()) return &d;

  constexpr size_t encodedLen = sizeof(JPEG_BASE64) - 1;
  const size_t capacity = ((encodedLen + 3) / 4) * 3;
  uint8_t* compressed = (uint8_t*)heap_caps_malloc(
      capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!compressed) {
    Serial.println("[MIRA][SPLASH] Impossible d'allouer le JPEG");
    return nullptr;
  }

  size_t decodedLen = 0;
  const int base64Status = mbedtls_base64_decode(
      compressed, capacity, &decodedLen, JPEG_BASE64, encodedLen);
  if (base64Status != 0 || decodedLen == 0) {
    heap_caps_free(compressed);
    Serial.println("[MIRA][SPLASH] Erreur de decodage base64");
    return nullptr;
  }

  JPEGDEC* jpeg = new (std::nothrow) JPEGDEC();
  if (!jpeg) {
    heap_caps_free(compressed);
    return nullptr;
  }
  const bool opened = jpeg->openRAM(compressed, (int)decodedLen, drawMcu) == 1;
  const bool dimensionsOk = opened && jpeg->getWidth() == W && jpeg->getHeight() == H;
  Frame frame;
  if (dimensionsOk) {
    frame.pixels = (uint16_t*)heap_caps_calloc((size_t)W * H, sizeof(uint16_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  bool ok = dimensionsOk && frame.pixels != nullptr;
  if (ok) {
#if LV_COLOR_16_SWAP
    jpeg->setPixelType(RGB565_BIG_ENDIAN);
#else
    jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
#endif
    jpeg->setUserPointer(&frame);
    ok = jpeg->decode(0, 0, 0) == 1;
  }
  if (opened) jpeg->close();
  delete jpeg;
  heap_caps_free(compressed);
  if (!ok) {
    if (frame.pixels) heap_caps_free(frame.pixels);
    Serial.println("[MIRA][SPLASH] Erreur de decodage JPEG/PSRAM");
    return nullptr;
  }

  storage() = frame.pixels;
  d.header.always_zero = 0;
  d.header.cf = LV_IMG_CF_TRUE_COLOR;
  d.header.w = W;
  d.header.h = H;
  d.data_size = (uint32_t)W * H * sizeof(uint16_t);
  d.data = (const uint8_t*)storage();
  return &d;
}

inline void release() {
  if (storage()) {
    heap_caps_free(storage());
    storage() = nullptr;
  }
  descriptor().data = nullptr;
}
} // namespace MiraBootLogo
#endif
