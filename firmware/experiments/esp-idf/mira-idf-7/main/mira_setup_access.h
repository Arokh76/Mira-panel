#pragma once
// Prototype setup/OTA is only accessible via the Waveshare's recovery AP.
// Do not expose configuration writes or unsigned OTA on the LAN STA interface.
// This is NOT a replacement for authenticated production OTA.
#include <stdbool.h>
#include <stdint.h>
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "lwip/sockets.h"

/* Restrict sensitive setup and unsigned recovery OTA to clients connected to
 * the local Mira softAP. Accepted HTTP sockets may report INADDR_ANY as their
 * local address on ESP-IDF; checking solely for 192.168.4.1 rejected actual
 * AP clients during the first-install POST /save?config=key.
 * Never grant access based only on URL/Host: validate source subnet AND the
 * destination interface, and derive the actual AP IP/netmask (configurable).
 */
static inline bool mira_setup_ap_client(httpd_req_t *req) {
    const char *tag="mira-setup-access";
    const int fd=httpd_req_to_sockfd(req);
    struct sockaddr_storage peer={0},local={0};
    socklen_t peer_len=sizeof(peer),local_len=sizeof(local);
    if(fd<0 || getpeername(fd,(struct sockaddr*)&peer,&peer_len)!=0 ||
       getsockname(fd,(struct sockaddr*)&local,&local_len)!=0 ||
       peer.ss_family!=AF_INET || local.ss_family!=AF_INET){
        ESP_LOGW(tag,"AP access denied: socket address unavailable or non-IPv4");
        return false;
    }
    const uint32_t peer_ip=ntohl(((const struct sockaddr_in*)&peer)->sin_addr.s_addr);
    const uint32_t local_ip=ntohl(((const struct sockaddr_in*)&local)->sin_addr.s_addr);
    esp_netif_t *ap=esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ap_info={0};
    if(!ap || esp_netif_get_ip_info(ap,&ap_info)!=ESP_OK){
        ESP_LOGW(tag,"AP access denied: softAP interface unavailable");
        return false;
    }
    const uint32_t ap_ip=ntohl(ap_info.ip.addr);
    const uint32_t mask=ntohl(ap_info.netmask.addr);
    const bool same_ap_subnet=(mask!=0 && ap_ip!=0 &&
                               (peer_ip&mask)==(ap_ip&mask));
    /* Some accepted ESP-IDF HTTP sockets inherit a 0.0.0.0 bound address.
     * Accept this only when the peer is inside the actual softAP subnet.
     * Any normal STA socket bound to its LAN IP remains denied.
     */
    const bool local_is_ap=(local_ip==ap_ip || local_ip==INADDR_ANY);
    if(!same_ap_subnet || !local_is_ap){
        ESP_LOGW(tag,"AP access denied: peer=%08lx local=%08lx ap=%08lx mask=%08lx",
                 (unsigned long)peer_ip,(unsigned long)local_ip,
                 (unsigned long)ap_ip,(unsigned long)mask);
        return false;
    }
    return true;
}
static inline bool mira_setup_require_ap(httpd_req_t *req) {
    if(mira_setup_ap_client(req))return true;
    httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Configuration et OTA uniquement via l'AP Mira");
    return false;
}
