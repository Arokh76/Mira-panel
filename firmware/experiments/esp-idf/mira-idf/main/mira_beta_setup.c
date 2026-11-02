// Mira 0.7.37 first-install workflow: identical beta HTML, native IDF backend.
// The Arduino AsyncWebServer library cannot run in ESP-IDF; only the handlers
// and storage are adapted. No new user-facing setup design.
#include "mira_beta_setup.h"
#include "mira_beta_setup_pages.h"
#include "mira_setup_access.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "nvs.h"
#include "esp_wifi.h"
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
bool mira_beta_setup_root(httpd_req_t *req){
    if(!has_local_key()){
        (void)serve_page(req,mira_beta_key_page);
        return true;
    }
    if(!has_wifi_station()){
        (void)serve_page(req,mira_beta_wifi_page);
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
    if(!mira_setup_require_ap(req))return ESP_FAIL;
    char query[96],nav[24];
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK||
       httpd_query_key_value(query,"nav",nav,sizeof(nav))!=ESP_OK){
        return serve_page(req,has_local_key()?mira_beta_wifi_page:mira_beta_key_page);
    }
    if(!strcmp(nav,"key")&&!has_wifi_station())
        return serve_page(req,mira_beta_key_page);
    if(!strcmp(nav,"wifisr")&&has_local_key())
        return serve_page(req,mira_beta_wifi_page);
    if(!strcmp(nav,"reboot")&&has_local_key()&&!has_wifi_station()){
        // The beta permits remaining in AP mode without a station.
        return serve_page(req,mira_beta_wifi_page);
    }
    httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Configuration Mira indisponible");
    return ESP_FAIL;
}
static esp_err_t save_post(httpd_req_t *req){
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
        return serve_page(req,mira_beta_wifi_page);
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
        // The original wizard allows an IP; never quietly ignore a requested
        // static address while the IDF static-IP editor is not ported.
        if(ip[0]&&strcmp(ip,"Auto")&&strcmp(ip,"auto")&&strcmp(ip,"DHCP")){
            httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
                "Pour ce premier test, utiliser Auto (DHCP). Configuration IP fixe encore en portage.");
            return ESP_FAIL;
        }
        wifi_config_t sta={0};
        memcpy(sta.sta.ssid,ssid,strlen(ssid));
        memcpy(sta.sta.password,password,strlen(password));
        esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&sta);
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
esp_err_t mira_beta_setup_register(httpd_handle_t server){
    const httpd_uri_t load={.uri="/load",.method=HTTP_GET,.handler=load_get};
    const httpd_uri_t save={.uri="/save",.method=HTTP_POST,.handler=save_post};
    esp_err_t err=httpd_register_uri_handler(server,&load);
    if(err!=ESP_OK)return err;
    return httpd_register_uri_handler(server,&save);
}
