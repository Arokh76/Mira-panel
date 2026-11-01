#pragma once
// LVGL 9.6 bridge for the ORIGINAL MiraPanelUI / MiraPanelServer / MiraMqtt.
// This is not the synthetic Mira preview.  Native driver is copied from the
// hardware-tested ESP-IDF 5.5.6 branch (RGB 12 MHz + fixed GT911 0x814F).
#include <Arduino.h>
#include "src/MiraNative43C/mira_display.h"

class MiraNative43CDisplay {
public:
    bool begin() {
        if(_initialized) return true;
        esp_err_t err=mira_display_init();
        if(err!=ESP_OK) {
            Serial.printf("[MIRA][RGB9] native init error=%d\n",(int)err);
            return false;
        }
        _initialized=true;
        Serial.println("[MIRA][RGB9] native 12MHz PSRAM + GT911 ready / VSYNC DMA touch recovery");
        return true;
    }
    bool miraLvglBegin() { return _initialized; }
    // With the Arduino loop driving LVGL, no separate LVGL server thread exists.
    bool miraLvglLock(int timeoutMs=-1) { (void)timeoutMs; return _initialized; }
    void miraLvglUnlock() {}
    void setRotation(uint8_t r) { (void)r; }
    void setBrightness(uint8_t raw) {
        mira_display_set_brightness(raw);
    }
    bool miraGetTouch(uint16_t *x,uint16_t *y) {
        // LVGL9's native independent 10ms touch task owns the indev.
        (void)x; (void)y;
        return false;
    }
    bool miraGetFrameBuffers(void **fb1,void **fb2) {
        // LVGL9 native driver already owns its PSRAM framebuffers.
        (void)fb1; (void)fb2;
        return false;
    }
    void miraFlush(int32_t x,int32_t y,int32_t w,int32_t h,const uint16_t *pixels) {
        (void)x;(void)y;(void)w;(void)h;(void)pixels;
        // Native LVGL9 flush callback is registered in mira_display_init().
    }
private:
    bool _initialized=false;
};
