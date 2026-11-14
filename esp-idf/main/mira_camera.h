#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Camera snapshots are downloaded/decoded on a dedicated FreeRTOS worker.
// LVGL may only use mira_camera_take() from its own render task.
typedef struct {
    uint16_t *pixels; // RGB565 pixels owned by caller; free with heap_caps_free()
    uint16_t width;
    uint16_t height;
} mira_camera_frame_t;

esp_err_t mira_camera_init(void);
bool mira_camera_start(const char *widget_name); // false if no saved URL
void mira_camera_stop(void);
bool mira_camera_take(mira_camera_frame_t *out);
bool mira_camera_save_url(const char *name,const char *url);
bool mira_camera_url_valid(const char *url);

// Internal artwork helper: fetch/decode a single bounded HTTPS JPEG off the LVGL task.
// Caller takes ownership of out->pixels. No global camera state is modified.
bool mira_camera_fetch_url(const char *url,mira_camera_frame_t *out);

 // One-shot media image diagnostic. Phase: 0=OK, 1=init, 2=TLS/connect,
 // 3=HTTP status, 4=short header, 5=JPEG, 6=format, 7=other.
 typedef struct {
     int32_t phase, http_status, connect_error;
     int32_t tls_error, tls_flags, socket_errno;
 } mira_camera_fetch_diag_t;
 bool mira_camera_fetch_url_diag(const char *url, mira_camera_frame_t *out,
                                 mira_camera_fetch_diag_t *diag);
