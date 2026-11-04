#pragma once
// Logo Mira officiel (docs/mira-logo.svg), palette RGBA 35x28.
// Converti au premier affichage en LVGL 8 / RGB565 + alpha.
#include <lvgl.h>
#include <stdint.h>

namespace MiraBootLogo {
static constexpr uint16_t W = 35;
static constexpr uint16_t H = 28;
static const uint32_t palette[32] = {
  0xFAFBFCFC, 0x00000000, 0x0FEFF1F3, 0xFD00A2FD, 0xF307D8FC, 0xA4EFF2F3, 0x1100EFF3, 0x11AEB0B9,
  0xF8A0ECFD, 0xFD0071FC, 0xF75EDCFC, 0x160A6FE2, 0x0726AFB4, 0x1100A1DB, 0xA3019AF4, 0x027FFFFF,
  0x10B5B5D4, 0x62EAEBEC, 0x4B02DDEF, 0x0AA6DFEF, 0x93036DF0, 0xB518C4F7, 0x027F7F7F, 0x04007FBF,
  0x7A0260F4, 0x05666699, 0x027F7FFF, 0x047FBFBF, 0x0D75B0C4, 0xB468E6F9, 0x03AA5555, 0x03AA55AA
};
static const char* const rows[H] = {
  "250021211111111111111111111121P5002",
  "50000212111111111111111111121750000",
  "00000021211111111111111111217500000",
  "000000021211111111111111121G0000000",
  "00000000212111111111111121200000000",
  "00000000021211111111111J12000000000",
  "00000000002121111111112120000000000",
  "0000000000021G111111121G00000000000",
  "000000000000J1711111212000000000000",
  "000000000000021F1112120000000000000",
  "00000000000008S1F1M1200000000000000",
  "00000000000088AC2R22000000000000000",
  "0000000000088AALN215000000000000000",
  "0000005500088AA4LV120000000H0000000",
  "000000515088AA444EU1200000210000000",
  "000000521588A44333E0120002120000000",
  "000000512108A443333E112521270000000",
  "000000512FCTA4433333E11112170000000",
  "000000512F6C444333333K1JQ2170000000",
  "000000512161D443333339K1BM170000000",
  "00000051211616443333999O1B170000000",
  "000000512111D16443333999B1170000000",
  "000000512111161644333999B1170000000",
  "000000512111116164433333B1170000000",
  "000000512111111C16443333B1170000000",
  "000000512111111161644333D11G0000000",
  "0000005121111111161I4444D2270000000",
  "H0000022211111111161I44I1671H00000H"
};

inline const lv_img_dsc_t* image() {
  static uint8_t pixels[W * H * 3];
  static lv_img_dsc_t descriptor = {};
  static bool ready = false;
  if (!ready) {
    for (uint16_t y = 0; y < H; ++y) {
      for (uint16_t x = 0; x < W; ++x) {
        const char digit = rows[y][x];
        const uint8_t index = digit >= 'A' ? (uint8_t)(digit - 'A' + 10) : (uint8_t)(digit - '0');
        const uint32_t argb = palette[index];
        const lv_color_t color = lv_color_make((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
        const uint16_t offset = (y * W + x) * 3;
        pixels[offset] = (uint8_t)(color.full & 0xFF);
        pixels[offset + 1] = (uint8_t)(color.full >> 8);
        pixels[offset + 2] = (uint8_t)(argb >> 24);
      }
    }
    descriptor.header.always_zero = 0;
    descriptor.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    descriptor.header.w = W;
    descriptor.header.h = H;
    descriptor.data_size = sizeof(pixels);
    descriptor.data = pixels;
    ready = true;
  }
  return &descriptor;
}
} // namespace MiraBootLogo
