#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "mira_camera.h"
// Only approved HTTPS media-cover hosts; worker never touches LVGL.
esp_err_t mira_album_art_init(void);
bool mira_album_art_request(uint8_t widget_index,const char *url);
bool mira_album_art_take(uint8_t *widget_index,mira_camera_frame_t *frame);
void mira_album_art_stop(void);
