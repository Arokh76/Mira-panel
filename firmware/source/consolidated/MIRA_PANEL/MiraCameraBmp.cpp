#include "MiraCameraBmp.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <new>
#include <JPEGDEC.h>   // Larry Bank JPEGDEC: RGB565 decoder with 1/2, 1/4, 1/8 scaling
#include <lvgl.h>       // LV_COLOR_16_SWAP defines the native LVGL image byte order

#if LV_COLOR_DEPTH != 16
#error "Mira camera framebuffer requires 16-bit LVGL RGB565"
#endif
#include <esp_heap_caps.h>

namespace {
static inline bool miraOldImageRequest(const std::atomic<uint32_t>* current,
                                       uint32_t requestGeneration) {
  return current && current->load(std::memory_order_acquire) != requestGeneration;
}
// LV_IMG_CF_TRUE_COLOR consumes exactly the pixel layout configured by
// LV_COLOR_16_SWAP. WT32/Nextion use swapped RGB565 (1), while Waveshare
// uses native RGB565 (0). The original JPEG decoder always generated LE,
// causing false/pseudo colors on the WT32 despite a valid JPEG source.
static inline uint16_t miraNativeImage565(uint16_t rgb565) {
#if LV_COLOR_16_SWAP
  return __builtin_bswap16(rgb565);
#else
  return rgb565;
#endif
}
uint16_t read16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t read32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
bool receiveBytes(Stream& stream, uint8_t* out, size_t count,
                  uint32_t idleTimeoutMs = 5000UL,
                  const std::atomic<uint32_t>* current = nullptr,
                  uint32_t requestGeneration = 0) {
  size_t done = 0;
  uint32_t deadline = millis() + idleTimeoutMs;
  while (done < count && (int32_t)(millis() - deadline) < 0) {
    if (miraOldImageRequest(current, requestGeneration)) return false;
    const size_t got = stream.readBytes(out + done, count - done);
    if (got) {
      done += got;
      deadline = millis() + idleTimeoutMs;
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  return done == count;
}

static constexpr size_t MIRA_JPEG_MAX_BYTES = 900UL * 1024UL;

// RGB565 pixels are decoded into a PSRAM frame, never drawn from the worker.
// JPEGDEC invokes this once per MCU (usually 16x16 pixels).
static int miraJpegDraw(JPEGDRAW* draw) {
  if (!draw || !draw->pUser || !draw->pPixels || draw->iBpp != 16) return 0;
  auto* f = static_cast<MiraCameraBmp::Frame*>(draw->pUser);
  if (!f->pixels) return 0;
  for (int yy = 0; yy < draw->iHeight; ++yy) {
    const int ty = draw->y + yy;
    if (ty < 0 || ty >= f->height) continue;
    const uint16_t* row = draw->pPixels + (size_t)yy * draw->iWidth;
    uint16_t* dest = f->pixels + (size_t)ty * f->width;
    for (int xx = 0; xx < draw->iWidthUsed; ++xx) {
      const int tx = draw->x + xx;
      if (tx >= 0 && tx < f->width) dest[tx] = row[xx];
    }
  }
  return 1;
}

static bool miraDownloadJpeg(Stream& stream, int contentLength,
                             const uint8_t* first, size_t firstCount,
                             uint16_t maxW, uint16_t maxH,
                             MiraCameraBmp::Frame& output,
                             uint32_t jpegIdleTimeoutMs,
                             const std::atomic<uint32_t>* current,
                             uint32_t requestGeneration) {
  if (contentLength > (int)MIRA_JPEG_MAX_BYTES) {
    Serial.println("[MIRA][CAM] JPEG trop volumineux"); return false;
  }
  // JPEG itself and decompressed RGB565 framebuffer stay in PSRAM.
  const size_t capacity = contentLength > 0 ? (size_t)contentLength : MIRA_JPEG_MAX_BYTES;
  if (capacity < firstCount) return false;
  uint8_t* compressed = (uint8_t*)heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!compressed) { Serial.println("[MIRA][CAM] Memoire JPEG insuffisante"); return false; }
  memcpy(compressed, first, firstCount);
  size_t used = firstCount;
  uint32_t lastData = millis();
  bool complete = false;
  while (used < capacity && (uint32_t)(millis() - lastData) < jpegIdleTimeoutMs) {
    if (miraOldImageRequest(current, requestGeneration)) {
      heap_caps_free(compressed);
      return false; // the new track's JPEG should start immediately
    }
    // An HTTP/1.0 snapshot normally provides Content-Length. If not, the
    // JPEG EOI marker terminates the stream without waiting for keep-alive.
    if (contentLength < 0 && used >= 2 &&
        compressed[used - 2] == 0xff && compressed[used - 1] == 0xd9) {
      complete = true;
      break;
    }
    int available = stream.available();
    if (available > 0) {
      size_t count = (size_t)available;
      if (count > capacity - used) count = capacity - used;
      const size_t got = stream.readBytes(compressed + used, count);
      if (got) { used += got; lastData = millis(); }
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  if (miraOldImageRequest(current, requestGeneration)) {
    heap_caps_free(compressed);
    return false;
  }
  if (contentLength >= 0) complete = used == (size_t)contentLength;
  else if (used >= 2 && compressed[used-2] == 0xff && compressed[used-1] == 0xd9)
    complete = true;
  if (!complete || used < 60 || compressed[0] != 0xff || compressed[1] != 0xd8) {
    heap_caps_free(compressed);
    Serial.println("[MIRA][CAM] JPEG incomplet ou trop grand");
    return false;
  }

  JPEGDEC* jpeg = new (std::nothrow) JPEGDEC();
  if (!jpeg) { heap_caps_free(compressed); return false; }
  bool valid = jpeg->openRAM(compressed, (int)used, miraJpegDraw) == 1;
  int options = 0, divisor = 1;
  int width = valid ? jpeg->getWidth() : 0;
  int height = valid ? jpeg->getHeight() : 0;
  if (valid) {
    while ((width + divisor - 1) / divisor > maxW ||
           (height + divisor - 1) / divisor > maxH) {
      divisor *= 2;
      if (divisor > 8) { valid = false; break; }
    }
    if (divisor == 2) options = JPEG_SCALE_HALF;
    else if (divisor == 4) options = JPEG_SCALE_QUARTER;
    else if (divisor == 8) options = JPEG_SCALE_EIGHTH;
  }
  if (valid) {
    output.width = (width + divisor - 1) / divisor;
    output.height = (height + divisor - 1) / divisor;
    const size_t bytes = size_t(output.width) * output.height * 2;
    output.pixels = (uint16_t*)heap_caps_calloc(bytes / 2, 2,
                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    valid = output.pixels != nullptr;
  }
  if (valid) {
#if LV_COLOR_16_SWAP
    jpeg->setPixelType(RGB565_BIG_ENDIAN);
#else
    jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
#endif
    jpeg->setUserPointer(&output);
    valid = jpeg->decode(0, 0, options) == 1;
    if (!valid) Serial.printf("[MIRA][CAM] Erreur decode JPEG: %d\n", jpeg->getLastError());
  }
  jpeg->close();
  delete jpeg;
  heap_caps_free(compressed);
  if (!valid) {
    if (output.pixels) heap_caps_free(output.pixels);
    output = {};
  }
  return valid;
}
}

void MiraCameraBmp::release(Frame& frame) {
  if (frame.pixels) heap_caps_free(frame.pixels);
  frame = {};
}

bool MiraCameraBmp::start(const String& bmpUrl, uint16_t maxWidth,
                          uint16_t maxHeight, bool oneShot) {
  if (!(bmpUrl.startsWith("http://") || bmpUrl.startsWith("https://")) ||
      bmpUrl.length() > 768 ||
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
  _refreshIntervalMs = 250;
  _oneShot = oneShot;
  _oneShotDone = false;
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
    bool oneShotArtwork = false;
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
      if (_active && !_oneShotDone && WiFi.status() == WL_CONNECTED &&
          (!_lastRequestAt || (uint32_t)(millis() - _lastRequestAt) >= _refreshIntervalMs)) {
        _lastRequestAt = millis();
        url = _url;
        w = _maxW;
        h = _maxH;
        generation = _generation;
        oneShotArtwork = _oneShot;
      }
      xSemaphoreGive(_mutex);
    }
    if (url.length()) {
      // Target up to 4 snapshots/s: JPEG download + decode run sequentially.
      // When a camera is offline, wait 2s before retrying instead of spamming.
      // The same FreeRTOS worker finishes HTTP download + JPEG decoding before
      // fetching again: no growing request queue, no LVGL/network blocking.
      // A camera proxy may resize dynamically. An ordinary BMP URL can ignore
      // these parameters, provided its bitmap fits the display limits.
      if (url.indexOf(".bmp") >= 0) {
        url += (url.indexOf('?') >= 0 ? "&" : "?");
        url += "w=" + String(w) + "&h=" + String(h);
      }
      Frame frame;
      const uint32_t beganAt = millis();
      const bool ok = downloadBmp(url, w, h, frame, oneShotArtwork,
                                   &_generation, generation);
      const uint32_t elapsedMs = millis() - beganAt;
      xSemaphoreTake(_mutex, portMAX_DELAY);
      if (oneShotArtwork)
        Serial.printf("[MIRA][ART] %s en %lu ms%s\n",
          generation != _generation ? "Requete annulee (nouvelle piste)" :
          (ok ? "Pochette chargee et decodee" : "Echec pochette"),
          (unsigned long)elapsedMs,
          generation == _generation ? "" : " (obsolete)");
      if (_active && generation == _generation) {
        _refreshIntervalMs = ok ? 250 : 2000;
        if (ok) {
          if (_oneShot) _oneShotDone = true;
          release(_ready);
          _ready = frame;
          frame = {};
        } else {
          // Error cooldown starts after the failed request completes.
          _lastRequestAt = millis();
        }
      }
      xSemaphoreGive(_mutex);
      release(frame);
    }
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

bool MiraCameraBmp::downloadBmp(const String& url, uint16_t maxW,
                                uint16_t maxH, Frame& out,
                                bool oneShotArtwork,
                                const std::atomic<uint32_t>* current,
                                uint32_t requestGeneration) {
  WiFiClient client;
  WiFiClientSecure secureClient;
  // Artwork is a public JPEG: never send Spotify credentials over TLS.
  if (url.startsWith("https://")) secureClient.setInsecure();
  WiFiClient& net = url.startsWith("https://") ?
                    static_cast<WiFiClient&>(secureClient) : client;
  HTTPClient http;
  http.useHTTP10(true); // prevent chunked transfer in the BMP decoder
  // Album artwork is a one-shot Spotify CDN request. Avoid keeping the
  // sole artwork worker busy on an obsolete HTTPS fetch for many seconds
  // after the user changes tracks. Camera streams retain their timeouts.
  http.setConnectTimeout(oneShotArtwork ? 2500 : 3500);
  http.setTimeout(oneShotArtwork ? 3500 : 5000);
  if (!http.begin(net, url)) return false;
  const uint32_t requestAt = millis();
  const int response = http.GET();
  if (miraOldImageRequest(current, requestGeneration)) {
    http.end();
    return false;
  }
  if (oneShotArtwork)
    Serial.printf("[MIRA][ART] Reponse HTTPS %d en %lu ms\n",
      response, (unsigned long)(millis() - requestAt));
  if (response != HTTP_CODE_OK) {
    Serial.printf("[MIRA][CAM] Snapshot HTTP %d\n", response);
    http.end();
    return false;
  }
  Stream& stream = http.getStream();
  stream.setTimeout(oneShotArtwork ? 1600 : 2000);
  uint8_t header[54];
  if (!receiveBytes(stream, header, sizeof(header),
                    oneShotArtwork ? 2500UL : 5000UL,
                    current, requestGeneration)) {
    http.end();
    return false;
  }
  if (header[0] == 0xff && header[1] == 0xd8) {
    const uint32_t jpegAt = millis();
    const bool ok = miraDownloadJpeg(stream, http.getSize(), header,
                        sizeof(header), maxW, maxH, out,
                        oneShotArtwork ? 2500UL : 4500UL,
                        current, requestGeneration);
    if (oneShotArtwork)
      Serial.printf("[MIRA][ART] Flux JPEG + decodage %s en %lu ms\n",
        ok ? "OK" : "ECHEC", (unsigned long)(millis() - jpegAt));
    http.end();
    return ok;
  }
  if (header[0] != 'B' || header[1] != 'M' ||
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
      const uint16_t rgb565 = uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
      dest[x] = miraNativeImage565(rgb565);
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
