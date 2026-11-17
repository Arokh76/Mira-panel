// 7STD transport migration: prove WT32's ESPAsyncWebServer/AsyncTCP stack
// can coexist with the existing native ESP-IDF display and Wi-Fi drivers.
// DO NOT redirect Jeedom traffic until every route, SSE and OTA is migrated.
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "mira_async_web.h"

static const char *TAG = "mira7-async";
static AsyncWebServer async_server(8081);
static AsyncEventSource async_events("/events");
static bool async_started = false;

extern "C" void mira_async_web_start(void) {
    if (async_started) return;

    // Native ESP-IDF remains responsible for Wi-Fi configuration, AP/STA,
    // storage and display. Arduino adds String/AsyncTCP, not WiFi.begin().
    initArduino();

    // These endpoints intentionally expose no config, keys or credentials.
    // The existing production port 80 must remain untouched for recovery.
    async_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Mira 7 ESPAsyncWebServer transport test");
    });
    async_server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        char data[192];
        snprintf(data, sizeof(data),
                 "{\"transport\":\"ESPAsyncWebServer\",\"uptime_ms\":%lu,"
                 "\"internal_heap\":%lu,\"psram_free\":%lu}",
                 (unsigned long)millis(),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        AsyncWebServerResponse *response =
            request->beginResponse(200, "application/json", data);
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
    });
    async_events.onConnect([](AsyncEventSourceClient *client) {
        client->send("ready", "transport", millis(), 3000);
    });
    async_server.addHandler(&async_events);
    async_server.begin();
    async_started = true;
    ESP_LOGI(TAG, "ESPAsyncWebServer/AsyncTCP test ready on TCP 8081; legacy port 80 unchanged");
}
