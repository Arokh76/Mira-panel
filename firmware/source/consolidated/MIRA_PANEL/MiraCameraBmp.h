#pragma once
// Mira camera beta: fetch RGB24 BMP snapshots without blocking LVGL.
// UI never runs network operations; producer only writes PSRAM pixels.
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

class MiraCameraBmp {
public:
  struct Frame {
    uint16_t* pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
  };
  bool start(const String& bmpUrl, uint16_t maxWidth, uint16_t maxHeight,
             bool oneShot = false);
  void stop();
  bool take(Frame& destination);
private:
  static void taskThunk(void* arg);
  void run();
  static bool downloadBmp(const String& url, uint16_t maxW, uint16_t maxH,
                          Frame& out, bool oneShotArtwork);
  static void release(Frame& frame);
  SemaphoreHandle_t _mutex = nullptr;
  TaskHandle_t _task = nullptr;
  String _url;
  uint16_t _maxW = 0;
  uint16_t _maxH = 0;
  uint32_t _generation = 0;
  uint32_t _lastRequestAt = 0;
  uint32_t _refreshIntervalMs = 250; // fast snapshots; slow down on fetch failures
  bool _active = false;
  bool _oneShot = false;
  bool _oneShotDone = false;
  Frame _ready;
};
