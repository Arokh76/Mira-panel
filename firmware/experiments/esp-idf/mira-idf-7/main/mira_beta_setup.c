// Mira 0.7.37 first-install workflow: identical beta HTML, native IDF backend.
// The Arduino AsyncWebServer library cannot run in ESP-IDF; only the handlers
// and storage are adapted. No new user-facing setup design.
#include "mira_beta_setup.h"
#include "mira_beta_setup_pages.h"
#include "mira_beta_web.h"
#include "mira_beta_networks.h"
#include "mira_beta_ota.h"
#include "mira_beta_update.h"
#include "mira_beta_data.h"
#include "mira_setup_access.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include "nvs.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG="mira-beta-setup";
static bool has_local_key(void){
    nvs_handle_t nv;
    if(nvs_open("mira_idf_auth",NVS_READONLY,&nv)!=ESP_OK)return false;
    size_t len=0;
    const esp_err_t err=nvs_get_str(nv,"jeedom_key",NULL,&len);
    nvs_close(nv);
    return err==ESP_OK&&len>1;
}
static bool has_wifi_station(void){
    wifi_config_t sta={0};
    return esp_wifi_get_config(WIFI_IF_STA,&sta)==ESP_OK&&sta.sta.ssid[0]!=0;
}
static esp_err_t serve_page(httpd_req_t *req,const char *page){
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    return httpd_resp_sendstr(req,page);
}
// The beta fills SCANRESEAUX with a 10-network select box that copies the
// choice to the SSID text field. Reuse that original behavior/markup.
// Scanning failure must never block manual SSID entry.
static esp_err_t serve_wifi_page(httpd_req_t *req){
    const char *token="MIRA_SCAN_SLOT";
    const char *slot=strstr(mira_beta_wifi_page,token);
    if(!slot)return serve_page(req,mira_beta_wifi_page);
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    if(httpd_resp_send_chunk(req,mira_beta_wifi_page,
                            (ssize_t)(slot-mira_beta_wifi_page))!=ESP_OK)
        return ESP_FAIL;
    // Reuse the beta selector from the configured UI. The Wi-Fi scan is
    // executed in a worker, never synchronously in this HTTP response.
    if(mira_beta_web_wifi_choices(req)!=ESP_OK)return ESP_FAIL;
    if(httpd_resp_send_chunk(req,slot+strlen(token),HTTPD_RESP_USE_STRLEN)!=ESP_OK)
        return ESP_FAIL;
    return httpd_resp_send_chunk(req,NULL,0);
}
// First-run IPv4 follows the same three modes as the beta network editor.
static bool setup_ipv4(const char *s,ip4_addr_t *out){
    if(!s||!*s||!ip4addr_aton(s,out))return false;
    const uint32_t value=ntohl(out->addr);
    const uint8_t first=(uint8_t)(value>>24);
    return value!=0&&value!=0xffffffffu&&first!=0&&first!=127&&first<224;
}
static bool setup_manual_ip_valid(const char *ip,const char *mask,
        const char *gateway,const char *dns1,const char *dns2){
    ip4_addr_t addr={0},nm={0},gw={0},dn={0},backup={0};
    if(!setup_ipv4(ip,&addr)||!mask||!ip4addr_aton(mask,&nm)||
       !setup_ipv4(gateway,&gw)||!setup_ipv4(dns1,&dn)||
       (dns2[0]&&!setup_ipv4(dns2,&backup)))return false;
    const uint32_t m=ntohl(nm.addr),inv=~m;
    const uint32_t host=addr.addr&~nm.addr;
    return m!=0&&(inv&(inv+1u))==0&&
           (addr.addr&nm.addr)==(gw.addr&nm.addr)&&
           addr.addr!=gw.addr&&host!=0&&host!=~nm.addr;
}
static esp_err_t serve_saved_wifi_page(httpd_req_t *req,const char *mode,const char *ip){
    // Original confirmation page hardcodes Auto regardless of saved choice.
    const char *needle="<strong id=\"ipValue\">Auto</strong>";
    const char *slot=strstr(mira_beta_saved_page,needle);
    if(!slot||!strcmp(mode,"auto"))return serve_page(req,mira_beta_saved_page);
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    if(httpd_resp_send_chunk(req,mira_beta_saved_page,
            (ssize_t)(slot-mira_beta_saved_page))!=ESP_OK)return ESP_FAIL;
    char label[96];
    snprintf(label,sizeof(label),"<strong id=\"ipValue\">%s</strong>",ip);
    if(httpd_resp_send_chunk(req,label,HTTPD_RESP_USE_STRLEN)!=ESP_OK)return ESP_FAIL;
    if(httpd_resp_send_chunk(req,slot+strlen(needle),
            HTTPD_RESP_USE_STRLEN)!=ESP_OK)return ESP_FAIL;
    return httpd_resp_send_chunk(req,NULL,0);
}
bool mira_beta_setup_unconfigured(void){
    return !has_local_key()||!has_wifi_station();
}
bool mira_beta_setup_root(httpd_req_t *req){
    if(!has_local_key()){
        (void)serve_page(req,mira_beta_key_page);
        return true;
    }
    if(!has_wifi_station()){
        (void)serve_wifi_page(req);
        return true;
    }
    return false;
}
static int hexdigit(char c){
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static bool parse_form_param(const char *body,const char *key,char *dest,size_t cap){
    char temp[256];
    if(!body||!dest||cap==0||
       httpd_query_key_value(body,key,temp,sizeof(temp))!=ESP_OK)return false;
    size_t out=0;
    for(size_t pos=0;temp[pos];pos++){
        unsigned char c=(unsigned char)temp[pos];
        if(c=='%'){
            int hi=hexdigit(temp[pos+1]);
            int lo=temp[pos+1]?hexdigit(temp[pos+2]):-1;
            if(hi<0||lo<0)return false;
            c=(unsigned char)((hi<<4)|lo);
            pos+=2;
        }else if(c=='+')c=' ';
        if(c<0x20||c==0x7f||out+1>=cap)return false;
        dest[out++]=(char)c;
    }
    dest[out]=0;
    return true;
}
/* The original firmware-beta first-run server was exposed only by the
 * dedicated Mira setup AP. In ESP-IDF 5.5 some HTTP client socket addresses
 * are not reported on the interface used for reception; strict socket-only
 * checks wrongly reject /save?config=key (403) even in first setup.
 *
 * During first-run ONLY, accept setup routes if the softAP is enabled and
 * the station interface has no assigned IP. No routed STA/LAN interface is
 * reachable in this state. Keep the normal AP authorization for every other
 * route; production and OTA authorization are unchanged.
 */
static bool first_run_ap_access(httpd_req_t *req){
    if(mira_setup_ap_client(req))return true;
    wifi_mode_t mode=WIFI_MODE_NULL;
    esp_netif_t *sta_netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t sta_ip={0};
    if(esp_wifi_get_mode(&mode)==ESP_OK &&
       (mode==WIFI_MODE_AP||mode==WIFI_MODE_APSTA) &&
       sta_netif &&
       esp_netif_get_ip_info(sta_netif,&sta_ip)==ESP_OK &&
       sta_ip.ip.addr==0){
        /* A previously stored STA SSID does not mean LAN access is live.
         * The original beta resets STA when opening its captive portal.
         */
        ESP_LOGW(TAG,"First-run HTTP allowed via isolated Mira AP (STA has no IP)");
        return true;
    }
    return mira_setup_require_ap(req);
}
static esp_err_t wifi_scan_get(httpd_req_t *req){
    if(mira_beta_setup_unconfigured()){
        if(!first_run_ap_access(req))return ESP_FAIL;
    }else if(!mira_beta_web_authorized(req))return ESP_FAIL;
    return mira_beta_web_wifi_scan_get(req);
}
static esp_err_t load_get(httpd_req_t *req){
    if(!mira_beta_setup_unconfigured())return mira_beta_web_load(req);
    if(!first_run_ap_access(req))return ESP_FAIL;
    char query[96],nav[24];
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK||
       httpd_query_key_value(query,"nav",nav,sizeof(nav))!=ESP_OK){
        return has_local_key()?serve_wifi_page(req):serve_page(req,mira_beta_key_page);
    }
    if(!strcmp(nav,"key")&&!has_wifi_station())
        return serve_page(req,mira_beta_key_page);
    if(!strcmp(nav,"wifisr")&&has_local_key())
        return serve_wifi_page(req);
    if(!strcmp(nav,"reboot")&&has_local_key()&&!has_wifi_station()){
        /* Original firmware-beta /load?nav=reboot sets ap[0].actif=1,
         * shows the confirmation and restarts in persistent AP mode.
         * Simply returning the Wi-Fi form was a porting regression.
         */
        nvs_handle_t nv;
        esp_err_t err=nvs_open("mira_idf_net",NVS_READWRITE,&nv);
        if(err==ESP_OK){
            err=nvs_set_u8(nv,"ap_enabled",1);
            if(err==ESP_OK)err=nvs_commit(nv);
            nvs_close(nv);
        }
        if(err!=ESP_OK){
            httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Impossible de conserver le point d'acces Mira");
            return ESP_FAIL;
        }
        const char *ap_restart_html=
            "<!doctype html><html lang=\"fr\"><meta charset=\"UTF-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>Mira Panel - Enregistrement terminé</title>"
            "<body style=\"font-family:Arial,sans-serif;background:#7193A0;"
            "color:#263238;padding:32px\"><main style=\"max-width:760px;"
            "background:white;padding:28px;border-radius:20px;margin:auto\">"
            "<h1>Enregistrement terminé</h1>"
            "<p>Redémarrage de l'appareil en point d'accès Mira...</p>"
            "</main></body></html>";
        esp_err_t response=serve_page(req,ap_restart_html);
        vTaskDelay(pdMS_TO_TICKS(1200));
        esp_restart();
        return response;
    }
    httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Configuration Mira indisponible");
    return ESP_FAIL;
}
static esp_err_t save_post(httpd_req_t *req){
    if(!mira_beta_setup_unconfigured())return mira_beta_web_save(req);
    if(!first_run_ap_access(req))return ESP_FAIL;
    char query[64],config[12];
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK||
       httpd_query_key_value(query,"config",config,sizeof(config))!=ESP_OK){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Type de configuration absent");
        return ESP_FAIL;
    }
    if(req->content_len<=0||req->content_len>1024){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Formulaire trop volumineux");
        return ESP_FAIL;
    }
    char body[1025];
    size_t got=0,len=(size_t)req->content_len;
    while(got<len){
        int n=httpd_req_recv(req,body+got,len-got);
        if(n<=0){
            httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Formulaire incomplet");
            return ESP_FAIL;
        }
        got+=(size_t)n;
    }
    body[got]=0;
    if(!strcmp(config,"key")){
        if(has_wifi_station()){
            httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Configuration initiale deja terminee");
            return ESP_FAIL;
        }
        char key[97]={0},confirm[97]={0};
        if(!parse_form_param(body,"key",key,sizeof(key))||
           !parse_form_param(body,"ver",confirm,sizeof(confirm))||
           strlen(key)<8||strcmp(key,confirm)){
            httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
                                "Cle Mira invalide ou confirmation differente");
            return ESP_FAIL;
        }
        nvs_handle_t nv;
        esp_err_t err=nvs_open("mira_idf_auth",NVS_READWRITE,&nv);
        if(err==ESP_OK){
            err=nvs_set_str(nv,"jeedom_key",key);
            if(err==ESP_OK)err=nvs_commit(nv);
            nvs_close(nv);
        }
        memset(key,0,sizeof(key));
        memset(confirm,0,sizeof(confirm));
        if(err!=ESP_OK){
            httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Sauvegarde de la cle Mira impossible");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG,"Mira beta setup step 1/2: local screen key saved");
        /* Match firmware-beta's MiraPanelServer.cpp first-run /save
         * flow: after persisting the local key, render the exact original
         * Page_ManageurWiFiSansR in the same HTTP POST response.
         * A 303 redirect was an unnecessary IDF-port divergence.
         */
        return serve_wifi_page(req);
    }
    if(!strcmp(config,"wifi")){
        if(!has_local_key()||has_wifi_station()){
            httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Etape Wi-Fi initiale indisponible");
            return ESP_FAIL;
        }
        char ssid[33]={0},password[65]={0},mode[16]={0};
        char ip[32]={0},mask[32]={0},gateway[32]={0},dns1[32]={0},dns2[32]={0};
        const bool valid=parse_form_param(body,"ssid",ssid,sizeof(ssid))&&
                         parse_form_param(body,"pass",password,sizeof(password));
        const bool mode_present=parse_form_param(body,"ip_mode",mode,sizeof(mode));
        (void)parse_form_param(body,"ip",ip,sizeof(ip));
        if(!valid||!ssid[0]||strlen(password)>63){
            httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"SSID ou mot de passe invalide");
            return ESP_FAIL;
        }
        // Support previously cached first-install forms with only an IP.
        if(!mode_present)snprintf(mode,sizeof(mode),"%s",
             !ip[0]||!strcasecmp(ip,"Auto")||!strcasecmp(ip,"DHCP")?
             "auto":"fixed_auto");
        if(strcmp(mode,"auto")&&strcmp(mode,"fixed_auto")&&strcmp(mode,"manual")){
            httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Mode IPv4 invalide");
            return ESP_FAIL;
        }
        if(!strcmp(mode,"manual")){
            if(!parse_form_param(body,"mask",mask,sizeof(mask))||
               !parse_form_param(body,"gateway",gateway,sizeof(gateway))||
               !parse_form_param(body,"dns1",dns1,sizeof(dns1))||
               !parse_form_param(body,"dns2",dns2,sizeof(dns2))||
               !setup_manual_ip_valid(ip,mask,gateway,dns1,dns2)){
                httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
                                    "Adresse, masque, passerelle ou DNS invalides");
                return ESP_FAIL;
            }
        }else if(!strcmp(mode,"fixed_auto")){
            ip4_addr_t requested={0};
            if(!setup_ipv4(ip,&requested)){
                httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
                                    "Adresse IP fixe invalide");
                return ESP_FAIL;
            }
        }else ip[0]=0;
        wifi_config_t sta={0};
        memcpy(sta.sta.ssid,ssid,strlen(ssid));
        memcpy(sta.sta.password,password,strlen(password));
        // Persist IPv4 mode and all related fields together in one NVS value.
        cJSON *json=cJSON_CreateObject();
        bool built=json&&cJSON_AddStringToObject(json,"mode",mode)&&
            cJSON_AddStringToObject(json,"ip",ip)&&
            cJSON_AddStringToObject(json,"mask",mask)&&
            cJSON_AddStringToObject(json,"gateway",gateway)&&
            cJSON_AddStringToObject(json,"dns1",dns1)&&
            cJSON_AddStringToObject(json,"dns2",dns2);
        char *encoded=built?cJSON_PrintUnformatted(json):NULL;
        if(json)cJSON_Delete(json);
        if(!encoded||strlen(encoded)>511){
            if(encoded)cJSON_free(encoded);
            httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Profil IPv4 non disponible");
            return ESP_FAIL;
        }
        nvs_handle_t nv;
        esp_err_t err=nvs_open("mira_idf_net",NVS_READWRITE,&nv);
        if(err==ESP_OK){
            err=nvs_set_str(nv,"web_profile",encoded);
            if(err==ESP_OK){
                err=!strcmp(mode,"fixed_auto")?
                    nvs_set_str(nv,"setup_ip",ip):nvs_erase_key(nv,"setup_ip");
                if(strcmp(mode,"fixed_auto")&&err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
            }
            if(err==ESP_OK)err=nvs_commit(nv);
            nvs_close(nv);
        }
        cJSON_free(encoded);
        if(err==ESP_OK)err=esp_wifi_set_config(WIFI_IF_STA,&sta);
        /* The initial wizard must populate the SAME saved-network directory
         * used by the beta Web manager, not only ESP-IDF's STA NVS settings. */
        if(err==ESP_OK){
            mira_beta_network_t *profiles=calloc(MIRA_NET_MAX,sizeof(*profiles));
            if(!profiles)err=ESP_ERR_NO_MEM;
            else {
                size_t count=0;
                err=mira_beta_networks_list(profiles,&count,MIRA_NET_MAX);
                int id=-1;
                if(err==ESP_OK){
                    for(size_t i=0;i<count;i++)
                        if(!strcmp(profiles[i].ssid,ssid)){id=profiles[i].id;break;}
                    if(id<0&&count>=MIRA_NET_MAX)err=ESP_ERR_INVALID_SIZE;
                    if(err==ESP_OK&&id<0){
                        for(int candidate=0;candidate<=999;candidate++){
                            bool used=false;
                            for(size_t i=0;i<count;i++)
                                if(profiles[i].id==candidate){used=true;break;}
                            if(!used){id=candidate;break;}
                        }
                        if(id<0)err=ESP_ERR_INVALID_SIZE;
                    }
                }
                free(profiles);
                if(err==ESP_OK){
                    mira_beta_network_t profile={0};
                    profile.id=id;
                    snprintf(profile.ssid,sizeof(profile.ssid),"%s",ssid);
                    snprintf(profile.ip_mode,sizeof(profile.ip_mode),"%s",mode);
                    snprintf(profile.ip,sizeof(profile.ip),"%s",ip);
                    snprintf(profile.mask,sizeof(profile.mask),"%s",mask);
                    snprintf(profile.gateway,sizeof(profile.gateway),"%s",gateway);
                    snprintf(profile.dns1,sizeof(profile.dns1),"%s",dns1);
                    snprintf(profile.dns2,sizeof(profile.dns2),"%s",dns2);
                    err=mira_beta_networks_upsert(&profile,password);
                }
            }
        }
        memset(password,0,sizeof(password));
        if(err!=ESP_OK){
            ESP_LOGE(TAG,"Wi-Fi profile save failed: %s",esp_err_to_name(err));
            httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Enregistrement Wi-Fi impossible");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG,"Mira beta setup step 2/2: Wi-Fi saved, restart requested");
        esp_err_t response=serve_saved_wifi_page(req,mode,ip);
        vTaskDelay(pdMS_TO_TICKS(1200));
        esp_restart();
        return response;
    }
    httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Configuration Mira inconnue");
    return ESP_FAIL;
}
static esp_err_t captive_probe(httpd_req_t *req){
    if(mira_setup_ap_client(req)&&mira_beta_setup_unconfigured()){
        httpd_resp_set_status(req,"302 Found");
        httpd_resp_set_hdr(req,"Location","http://192.168.4.1/");
        return httpd_resp_sendstr(req,"");
    }
    httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Page introuvable");
    return ESP_FAIL;
}
esp_err_t mira_beta_setup_register_captive(httpd_handle_t server){
    // The wildcard is registered AFTER all explicit HTTP/Jeedom routes.
    const httpd_uri_t fallback={
        .uri="/*",.method=HTTP_GET,.handler=captive_probe
    };
    return httpd_register_uri_handler(server,&fallback);
}
esp_err_t mira_beta_setup_register_minimal(httpd_handle_t server){
    const httpd_uri_t scan={.uri="/mira/wifi/scan",.method=HTTP_GET,.handler=wifi_scan_get};
    const httpd_uri_t load={.uri="/load",.method=HTTP_GET,.handler=load_get};
    const httpd_uri_t save={.uri="/save",.method=HTTP_POST,.handler=save_post};
    esp_err_t err=httpd_register_uri_handler(server,&scan);
    if(err!=ESP_OK)return err;
    err=httpd_register_uri_handler(server,&load);
    if(err!=ESP_OK)return err;
    return httpd_register_uri_handler(server,&save);
}
esp_err_t mira_beta_setup_register(httpd_handle_t server){
    const httpd_uri_t scan={.uri="/mira/wifi/scan",.method=HTTP_GET,.handler=wifi_scan_get};
    esp_err_t err=httpd_register_uri_handler(server,&scan);
    if(err!=ESP_OK)return err;
    const httpd_uri_t load={.uri="/load",.method=HTTP_GET,.handler=load_get};
    const httpd_uri_t save={.uri="/save",.method=HTTP_POST,.handler=save_post};
    err=httpd_register_uri_handler(server,&load);
    if(err!=ESP_OK)return err;
    err=httpd_register_uri_handler(server,&save);
    if(err!=ESP_OK)return err;
    const httpd_uri_t display={.uri="/display-save",.method=HTTP_POST,
                               .handler=mira_beta_web_display_save};
    err=httpd_register_uri_handler(server,&display);
    if(err!=ESP_OK)return err;
    const httpd_uri_t upload={.uri="/doUpdateFile",.method=HTTP_POST,
                              .handler=mira_beta_ota_upload};
    err=httpd_register_uri_handler(server,&upload);
    if(err!=ESP_OK)return err;
    const httpd_uri_t reset={.uri="/mira/reset",.method=HTTP_POST,
                              .handler=mira_beta_data_reset_post};
    err=httpd_register_uri_handler(server,&reset);
    if(err!=ESP_OK)return err;
    const httpd_uri_t network={.uri="/mira/network",.method=HTTP_POST,
                                .handler=mira_beta_web_network_post};
    err=httpd_register_uri_handler(server,&network);
    if(err!=ESP_OK)return err;
    const httpd_uri_t gpio_state={.uri="/gpio/state",.method=HTTP_GET,
                                  .handler=mira_beta_web_gpio_state};
    err=httpd_register_uri_handler(server,&gpio_state);
    if(err!=ESP_OK)return err;
    const httpd_uri_t gpio_set={.uri="/gpio/set",.method=HTTP_POST,
                                .handler=mira_beta_web_gpio_set};
    err=httpd_register_uri_handler(server,&gpio_set);
    if(err!=ESP_OK)return err;
    /* Historical beta GPIO contracts, mapped to DI0/DI1 + DO0/DO1. */
    const httpd_uri_t gpio_raw={.uri="/gpio/raw",.method=HTTP_POST,
                                .handler=mira_beta_web_gpio_raw};
    err=httpd_register_uri_handler(server,&gpio_raw);
    if(err!=ESP_OK)return err;
    const httpd_uri_t gpio_config={.uri="/gpio/config",.method=HTTP_POST,
                                   .handler=mira_beta_web_gpio_config};
    err=httpd_register_uri_handler(server,&gpio_config);
    if(err!=ESP_OK)return err;
    const httpd_uri_t gpio_link={.uri="/gpio/link",.method=HTTP_POST,
                                 .handler=mira_beta_web_gpio_link};
    err=httpd_register_uri_handler(server,&gpio_link);
    if(err!=ESP_OK)return err;
    const httpd_uri_t gpio_multi={.uri="/gpio/multi/config",.method=HTTP_POST,
                                  .handler=mira_beta_web_gpio_multi};
    err=httpd_register_uri_handler(server,&gpio_multi);
    if(err!=ESP_OK)return err;
    const httpd_uri_t check={.uri="/api/checkupdate",.method=HTTP_GET,
                             .handler=mira_beta_update_check};
    err=httpd_register_uri_handler(server,&check);
    if(err!=ESP_OK)return err;
    const httpd_uri_t older={.uri="/update",.method=HTTP_GET,
                             .handler=mira_beta_update_legacy_page};
    err=httpd_register_uri_handler(server,&older);
    if(err!=ESP_OK)return err;
    const httpd_uri_t start={.uri="/updatemodule",.method=HTTP_GET,
                             .handler=mira_beta_update_start};
    err=httpd_register_uri_handler(server,&start);
    if(err!=ESP_OK)return err;
    const httpd_uri_t install={.uri="/updatemodule",.method=HTTP_POST,
                               .handler=mira_beta_update_trigger};
    err=httpd_register_uri_handler(server,&install);
    if(err!=ESP_OK)return err;
    const httpd_uri_t status={.uri="/api/otastatus",.method=HTTP_GET,
                              .handler=mira_beta_update_status};
    return httpd_register_uri_handler(server,&status);
}
