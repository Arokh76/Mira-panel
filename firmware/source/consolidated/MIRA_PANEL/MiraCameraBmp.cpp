#include "MiraCameraBmp.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>

namespace {
uint16_t read16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t read32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
bool receiveBytes(Stream& stream, uint8_t* out, size_t count) {
  size_t done = 0;
  uint32_t deadline = millis() + 5000UL;
  while (done < count && (int32_t)(millis() - deadline) < 0) {
    const size_t got = stream.readBytes(out + done, count - done);
    if (got) {
      done += got;
      deadline = millis() + 5000UL;
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  return done == count;
}
}

void MiraCameraBmp::release(Frame& frame) {
  if (frame.pixels) heap_caps_free(frame.pixels);
  frame = {};
}

bool MiraCameraBmp::start(const String& bmpUrl, uint16_t maxWidth, uint16_t maxHeight) {
  if (!bmpUrl.startsWith("http://") || bmpUrl.length() > 768 ||
      maxWidth < 40 || maxHeight < 40) return false;
  if (!_mutex) _mutex = xSemaphoreCreateMutex();
  if (!_mutex) return false;
  xSemaphoreTake(_mutex, portMAX_DELAY);
  _active = false;
  ++_generation;
  release(_ready);
  _url = bmpUrl;
  _maxW = maxWidth;
  _maxH = maxHeight;
  _lastRequestAt = 0;
  _active = true;
  xSemaphoreGive(_mutex);
  if (!_task) {
    if (xTaskCreatePinnedToCore(taskThunk, "MiraCamBmp", 8192, this, 1, &_task, 0) != pdPASS) {
      xSemaphoreTake(_mutex, portMAX_DELAY);
      _active = false;
      xSemaphoreGive(_mutex);
      return false;
    }
  }
  return true;
}

void MiraCameraBmp::stop() {
  if (!_mutex) return;
  xSemaphoreTake(_mutex, portMAX_DELAY);
  _active = false;
  ++_generation;
  release(_ready);
  xSemaphoreGive(_mutex);
}

bool MiraCameraBmp::take(Frame& destination) {
  if (!_mutex) return false;
  xSemaphoreTake(_mutex, portMAX_DELAY);
  bool available = _active && _ready.pixels;
  if (available) {
    destination = _ready;
    _ready = {};
  }
  xSemaphoreGive(_mutex);
  return available;
}

void MiraCameraBmp::taskThunk(void* argument) {
  static_cast<MiraCameraBmp*>(argument)->run();
}

void MiraCameraBmp::run() {
  for (;;) {
    String url;
    uint16_t w = 0, h = 0;
    uint32_t generation = 0;
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
      if (_active && WiFi.status() == WL_CONNECTED &&
          (!_lastRequestAt || (uint32_t)(millis() - _lastRequestAt) >= 4000UL)) {
        _lastRequestAt = millis();
        url = _url;
        w = _maxW;
        h = _maxH;
        generation = _generation;
      }
      xSemaphoreGive(_mutex);
    }
    if (url.length()) {
      // A camera proxy may resize dynamically. An ordinary BMP URL can ignore
      // these parameters, provided its bitmap fits the display limits.
      url += (url.indexOf('?') >= 0 ? "&" : "?");
      url += "w=" + String(w) + "&h=" + String(h);
      Frame frame;
      if (downloadBmp(url, w, h, frame)) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        if (_active && generation == _generation) {
          release(_ready);
          _ready = frame;
          frame = {};
        }
        xSemaphoreGive(_mutex);
      }
      release(frame);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

bool MiraCameraBmp::downloadBmp(const String& url, uint16_t maxW,
                                uint16_t maxH, Frame& out) {
  WiFiClient client;
  HTTPClient http;
  http.useHTTP10(true); // prevent chunked transfer in the BMP decoder
  http.setConnectTimeout(3500);
  http.setTimeout(5000);
  if (!http.begin(client, url)) return false;
  const int response = http.GET();
  if (response != HTTP_CODE_OK) {
    Serial.printf("[MIRA][CAM] Snapshot HTTP %d\n", response);
    http.end();
    return false;
  }
  Stream& stream = http.getStream();
  stream.setTimeout(2000);
  uint8_t header[54];
  if (!receiveBytes(stream, header, sizeof(header)) ||
      header[0] != 'B' || header[1] != 'M' ||
      read32(header + 14) < 40 ||
      read16(header + 26) != 1 ||
      read16(header + 28) != 24 ||
      read32(header + 30) != 0) {
    Serial.println("[MIRA][CAM] Format requis: BMP RGB24 non compresse");
    http.end();
    return false;
  }
  const uint32_t offset = read32(header + 10);
  const int32_t width = int32_t(read32(header + 18));
  const int32_t signedHeight = int32_t(read32(header + 22));
  const uint32_t height = signedHeight < 0 ? uint32_t(-int64_t(signedHeight)) : uint32_t(signedHeight);
  if (offset < 54 || offset > 1024 || width < 1 || width > maxW ||
      height < 1 || height > maxH ||
      uint64_t(width) * height * 2 > 1024UL * 1024UL) {
    Serial.println("[MIRA][CAM] Dimensions BMP non prises en charge");
    http.end();
    return false;
  }
  const size_t stride = (size_t(width) * 3 + 3) & ~size_t(3);
  auto* pixels = static_cast<uint16_t*>(heap_caps_malloc(size_t(width) * height * 2,
                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* row = static_cast<uint8_t*>(malloc(stride));
  if (!pixels || !row) {
    Serial.println("[MIRA][CAM] PSRAM insuffisante");
    if (pixels) heap_caps_free(pixels);
    if (row) free(row);
    http.end();
    return false;
  }
  bool valid = true;
  uint8_t skipped[64];
  for (uint32_t remaining = offset - 54; remaining;) {
    size_t len = remaining > sizeof(skipped) ? sizeof(skipped) : remaining;
    if (!receiveBytes(stream, skipped, len)) { valid = false; break; }
    remaining -= len;
  }
  for (uint32_t y = 0; valid && y < height; ++y) {
    if (!receiveBytes(stream, row, stride)) { valid = false; break; }
    const size_t targetY = signedHeight > 0 ? height - 1 - y : y;
    uint16_t* dest = pixels + targetY * width;
    for (int32_t x = 0; x < width; ++x) {
      const uint8_t b = row[3 * x];
      const uint8_t g = row[3 * x + 1];
      const uint8_t r = row[3 * x + 2];
      dest[x] = uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
  }
  free(row);
  http.end();
  if (!valid) {
    Serial.println("[MIRA][CAM] Transfert BMP interrompu");
    heap_caps_free(pixels);
    return false;
  }
  out.pixels = pixels;
  out.width = uint16_t(width);
  out.height = uint16_t(height);
  return true;
}
