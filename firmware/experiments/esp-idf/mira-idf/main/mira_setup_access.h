#pragma once
// Prototype setup/OTA is only accessible via the Waveshare's recovery AP.
// Do not expose configuration writes or unsigned OTA on the LAN STA interface.
// This is NOT a replacement for authenticated production OTA.
#include <stdbool.h>
#include <stdint.h>
#include "esp_http_server.h"
#include "lwip/sockets.h"

static inline bool mira_setup_ap_client(httpd_req_t *req) {
    int fd=httpd_req_to_sockfd(req);
    struct sockaddr_storage peer={0},local={0};
    socklen_t peer_len=sizeof(peer),local_len=sizeof(local);
    if(fd<0 || getpeername(fd,(struct sockaddr*)&peer,&peer_len)!=0 ||
       getsockname(fd,(struct sockaddr*)&local,&local_len)!=0 ||
       peer.ss_family!=AF_INET || local.ss_family!=AF_INET) return false;
    const uint32_t peer_ip=ntohl(((struct sockaddr_in*)&peer)->sin_addr.s_addr);
    const uint32_t local_ip=ntohl(((struct sockaddr_in*)&local)->sin_addr.s_addr);
    // ESP-IDF standard softAP default 192.168.4.1 / 24.
    return local_ip==0xC0A80401u && (peer_ip&0xFFFFFF00u)==0xC0A80400u;
}
static inline bool mira_setup_require_ap(httpd_req_t *req) {
    if(mira_setup_ap_client(req))return true;
    httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Configuration et OTA uniquement via l'AP Mira");
    return false;
}
