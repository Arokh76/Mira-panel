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
// Diagnostic only: never return artwork URLs, tokens or credentials.
typedef struct {
    uint32_t requests, rejected, started, worker_errors;
    uint32_t fetch_ok, fetch_failed, frames_taken;
    int32_t last_phase, last_http_status, last_connect_error;
    int32_t last_tls_error, last_tls_flags, last_socket_errno;
} mira_album_art_diag_t;
void mira_album_art_diagnostics(mira_album_art_diag_t *out);
