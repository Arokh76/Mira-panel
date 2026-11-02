// Mira 0.7.37 first-install workflow: identical beta HTML, native IDF backend.
// The Arduino AsyncWebServer library cannot run in ESP-IDF; only the handlers
// and storage are adapted. No new user-facing setup design.
#include "mira_beta_setup.h"
#include "mira_beta_setup_pages.h"
#include "mira_beta_web.h"
#include "mira_beta_ota.h"
#include "mira_setup_access.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include "nvs.h"
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
static esp_err_t send_html_escaped(httpd_req_t *req,const char *value){
    for(const unsigned char *p=(const unsigned char *)value;*p;p++){
        const char *entity=NULL;
        if(*p=='&')entity="&amp;";
        else if(*p=='<')entity="&lt;";
        else if(*p=='>')entity="&gt;";
        else if(*p=='"')entity="&quot;";
        else if(*p==39)entity="&#39;";
        if(entity){
            if(httpd_resp_send_chunk(req,entity,HTTPD_RESP_USE_STRLEN)!=ESP_OK)
                return ESP_FAIL;
        }else if(httpd_resp_send_chunk(req,(const char *)p,1)!=ESP_OK)
            return ESP_FAIL;
    }
    return ESP_OK;
}
static esp_err_t serve_wifi_page(httpd_req_t *req){
    const char *token="MIRA_SCAN_SLOT";
    const char *slot=strstr(mira_beta_wifi_page,token);
    if(!slot)return serve_page(req,mira_beta_wifi_page);
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    if(httpd_resp_send_chunk(req,mira_beta_wifi_page,
                            (ssize_t)(slot-mira_beta_wifi_page))!=ESP_OK)
        return ESP_FAIL;
    const char *intro="<label for=\"RS\">Réseaux détectés</label>"
        "<select id=\"RS\" name=\"RS\" class=\"box\" onchange=\"Click(this.value)\">"
        "<option value=\"\">Saisir le nom du réseau manuellement</option>";
    if(httpd_resp_send_chunk(req,intro,HTTPD_RESP_USE_STRLEN)!=ESP_OK)
        return ESP_FAIL;
    wifi_scan_config_t scan={0};
    if(esp_wifi_scan_start(&scan,true)==ESP_OK){
        wifi_ap_record_t records[10]={0};
        uint16_t count=10;
        if(esp_wifi_scan_get_ap_records(&count,records)==ESP_OK){
            for(uint16_t i=0;i<count;i++){
                char ssid[33]={0};
                memcpy(ssid,records[i].ssid,32);
                if(!ssid[0])continue;
                if(httpd_resp_send_chunk(req,"<option value=\"",HTTPD_RESP_USE_STRLEN)!=ESP_OK ||
                   send_html_escaped(req,ssid)!=ESP_OK ||
                   httpd_resp_send_chunk(req,"\">",2)!=ESP_OK ||
                   send_html_escaped(req,ssid)!=ESP_OK ||
                   httpd_resp_send_chunk(req,"</option>",HTTPD_RESP_USE_STRLEN)!=ESP_OK)
                    return ESP_FAIL;
            }
        }
    }else ESP_LOGW(TAG,"Wi-Fi scan failed; manual SSID setup remains available");
    if(httpd_resp_send_chunk(req,"</select>",HTTPD_RESP_USE_STRLEN)!=ESP_OK ||
       httpd_resp_send_chunk(req,slot+strlen(token),HTTPD_RESP_USE_STRLEN)!=ESP_OK)
        return ESP_FAIL;
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
static esp_err_t load_get(httpd_req_t *req){
    if(!mira_beta_setup_unconfigured())return mira_beta_web_load(req);
    if(!mira_setup_require_ap(req))return ESP_FAIL;
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
        // The beta permits remaining in AP mode without a station.
        return serve_wifi_page(req);
    }
    httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Configuration Mira indisponible");
    return ESP_FAIL;
}
static esp_err_t save_post(httpd_req_t *req){
    if(!mira_beta_setup_unconfigured())return mira_beta_web_save(req);
    if(!mira_setup_require_ap(req))return ESP_FAIL;
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
        return serve_wifi_page(req);
    }
    if(!strcmp(config,"wifi")){
        if(!has_local_key()||has_wifi_station()){
            httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Etape Wi-Fi initiale indisponible");
            return ESP_FAIL;
        }
        char ssid[33]={0},password[65]={0},ip[48]={0};
        const bool valid=parse_form_param(body,"ssid",ssid,sizeof(ssid))&&
                         parse_form_param(body,"pass",password,sizeof(password));
        (void)parse_form_param(body,"ip",ip,sizeof(ip));
        if(!valid||!ssid[0]||strlen(password)>63){
            httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"SSID ou mot de passe invalide");
            return ESP_FAIL;
        }
        // Match beta first-run "Auto" or requested IPv4. As on the beta,
        // a requested IP uses a DHCP lease to discover mask/gateway/DNS,
        // then replaces only the client address on that same subnet.
        const bool auto_ip=!ip[0]||!strcasecmp(ip,"Auto")||!strcasecmp(ip,"DHCP");
        if(!auto_ip){
            ip4_addr_t requested={0};
            const bool parsed=ip4addr_aton(ip,&requested)!=0;
            const uint32_t addr=ntohl(requested.addr);
            const uint8_t first=(uint8_t)(addr>>24);
            if(!parsed||!addr||addr==0xffffffffu||
               first==0||first==127||first>=224){
                httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
                                    "Adresse IP fixe invalide");
                return ESP_FAIL;
            }
        }
        wifi_config_t sta={0};
        memcpy(sta.sta.ssid,ssid,strlen(ssid));
        memcpy(sta.sta.password,password,strlen(password));
        // Persist the optional beta first-boot IP before joining Wi-Fi.
        // Recovery validates the DHCP subnet before applying this address.
        nvs_handle_t nv;
        esp_err_t err=nvs_open("mira_idf_net",NVS_READWRITE,&nv);
        if(err==ESP_OK){
            err=auto_ip?nvs_erase_key(nv,"setup_ip"):nvs_set_str(nv,"setup_ip",ip);
            if(auto_ip&&err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
            if(err==ESP_OK)err=nvs_commit(nv);
            nvs_close(nv);
        }
        if(err==ESP_OK)err=esp_wifi_set_config(WIFI_IF_STA,&sta);
        memset(password,0,sizeof(password));
        if(err!=ESP_OK){
            ESP_LOGE(TAG,"Wi-Fi profile save failed: %s",esp_err_to_name(err));
            httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Enregistrement Wi-Fi impossible");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG,"Mira beta setup step 2/2: Wi-Fi saved, restart requested");
        esp_err_t response=serve_page(req,mira_beta_saved_page);
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
esp_err_t mira_beta_setup_register(httpd_handle_t server){
    const httpd_uri_t load={.uri="/load",.method=HTTP_GET,.handler=load_get};
    const httpd_uri_t save={.uri="/save",.method=HTTP_POST,.handler=save_post};
    esp_err_t err=httpd_register_uri_handler(server,&load);
    if(err!=ESP_OK)return err;
    err=httpd_register_uri_handler(server,&save);
    if(err!=ESP_OK)return err;
    const httpd_uri_t display={.uri="/display-save",.method=HTTP_POST,
                               .handler=mira_beta_web_display_save};
    err=httpd_register_uri_handler(server,&display);
    if(err!=ESP_OK)return err;
    const httpd_uri_t upload={.uri="/doUpdateFile",.method=HTTP_POST,
                              .handler=mira_beta_ota_upload};
    return httpd_register_uri_handler(server,&upload);
}
