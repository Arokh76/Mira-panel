#pragma once

// Mira hardware profile. UI/business logic must never depend on one panel.
#if defined(MIRA_TARGET_WAVESHARE_43C)
  #error "Waveshare 4.3C Arduino retired: use ESP-IDF"
#endif

#if defined(MIRA_TARGET_NEXTION_28)
  #define MIRA_DISPLAY_PROFILE_ID "onx2432g028"
  #define MIRA_HARDWARE_NAME "OPEN-NEXTION-GENIUS-2.8"
  #define MIRA_DISPLAY_WIDTH 240
  #define MIRA_DISPLAY_HEIGHT 320
  #define MIRA_NAV_HEIGHT 42
  #define MIRA_CONTENT_HEIGHT (MIRA_DISPLAY_HEIGHT - MIRA_NAV_HEIGHT)
  #define MIRA_DISPLAY_ROTATION 0
  #define MIRA_DRAW_BUFFER_LINES 20
  #define MIRA_RGB_DIRECT_FRAMEBUFFER 0
#else
  #define MIRA_DISPLAY_PROFILE_ID "wt32-sc01-plus"
  #define MIRA_HARDWARE_NAME "WT32-SC01-PLUS"
  #define MIRA_DISPLAY_WIDTH 320
  #define MIRA_DISPLAY_HEIGHT 480
  #define MIRA_NAV_HEIGHT 46
  #define MIRA_CONTENT_HEIGHT (MIRA_DISPLAY_HEIGHT - MIRA_NAV_HEIGHT)
  #define MIRA_DISPLAY_ROTATION 0
  // Hardware validated on WT32: 32 lines causes Wi-Fi instability; keep 20.
  #define MIRA_DRAW_BUFFER_LINES 20
  #define MIRA_RGB_DIRECT_FRAMEBUFFER 0
#endif

#define MIRA_LAYOUT_LOGICAL_WIDTH 1000
#define MIRA_LAYOUT_LOGICAL_HEIGHT 1000
