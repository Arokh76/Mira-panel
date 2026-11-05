// Preserve the beta Data page's reset scopes, while requiring an actual
// authenticated POST confirmation before any destructive operation.
// GET /load?nav=rst* never deletes user data or formats SPIFFS.
#include "mira_beta_data.h"
#include "mira_beta_web.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
static const char *TAG="mira-beta-data";
static uint32_t reset_token;
static int64_t reset_issued_us;
static char reset_scope[16];

static const char *reset_label(const char *operation){
    if(!strcmp(operation,"rstnetwork"))return "Effacer les réseaux Wi-Fi enregistrés";
    if(!strcmp(operation,"rstparams"))return "Effacer les paramètres et la programmation Mira";
    if(!strcmp(operation,"rstall"))return "Réinitialiser complètement Mira Panel";
    return NULL;
}
static esp_err_t reject(httpd_req_t *req,httpd_err_code_t code,const char *msg){
    return httpd_resp_send_err(req,code,msg);
}
esp_err_t mira_beta_data_confirm(httpd_req_t *req,const char *operation){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    const char *label=reset_label(operation);
    if(!label)return reject(req,HTTPD_404_NOT_FOUND,"Action de reset inconnue");
    uint32_t token=esp_random();
    if(!token)token=1;
    reset_token=token;
    reset_issued_us=esp_timer_get_time();
    snprintf(reset_scope,sizeof(reset_scope),"%s",operation);
    char content[1600]={0};
    const int n=snprintf(content,sizeof(content),
       "<!doctype html><html lang=\"fr\"><meta charset=\"utf-8\">"
       "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
       "<title>Mira Panel - Confirmation</title>"
       "<body style=\"font:16px Arial,sans-serif;max-width:640px;margin:5vh auto;padding:20px\">"
       "<h1>Confirmer l'effacement</h1>"
       "<p><strong>%s</strong></p>"
       "<p>Cette opération efface des données locales. Elle ne peut pas être annulée."
       " Les pages de configuration et les identifiants du panneau peuvent être perdus.</p>"
       "<form method=\"POST\" action=\"/mira/reset\">"
       "<input type=\"hidden\" name=\"scope\" value=\"%s\">"
       "<input type=\"hidden\" name=\"nonce\" value=\"%08lx\">"
       "<button type=\"submit\" style=\"background:#a34d4d;color:white;padding:12px\">"
       "Confirmer la suppression</button> "
       "<a href=\"/load?nav=rst\">Annuler</a></form></body></html>",
       label,operation,(unsigned long)token);
    if(n<=0||n>=(int)sizeof(content))
        return reject(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Confirmation indisponible");
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,content);
}
static esp_err_t clear_namespace(const char *name){
    nvs_handle_t nv;
    esp_err_t err=nvs_open(name,NVS_READWRITE,&nv);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK; // Already empty
    if(err!=ESP_OK)return err;
    err=nvs_erase_all(nv);
    if(err==ESP_OK)err=nvs_commit(nv);
    if(err!=ESP_OK)ESP_LOGE(TAG,"Could not erase namespace %s: %s",name,esp_err_to_name(err));
    nvs_close(nv);
    return err;
}
static bool erase_named_file(const char *path){
    if(remove(path)==0)return true;
    FILE *f=fopen(path,"rb");
    if(!f)return true; // No file to remove.
    fclose(f);
    ESP_LOGE(TAG,"Unable to remove %s",path);
    return false;
}
static esp_err_t do_scope(const char *scope){
    if(!strcmp(scope,"rstnetwork")){
        // Never restore the old imported Wi-Fi credentials after reset.
        if(!erase_named_file("/spiffs/reseau.json"))return ESP_FAIL;
        // The beta's network reset retains its separate AP configuration.
        // Do not erase mira_idf_net wholesale (it also stores ap_ip/ap_enabled).
        nvs_handle_t net;
        if(nvs_open("mira_idf_net",NVS_READWRITE,&net)==ESP_OK){
            static const char *const keys[]={
                "legacy_wifi","web_mode","web_ip","web_mask","web_gw",
                "web_dns1","web_dns2","web_profile","setup_ip",
                "wifi_profiles"
            };
            esp_err_t e=ESP_OK;
            for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++){
                esp_err_t one=nvs_erase_key(net,keys[i]);
                if(one!=ESP_OK&&one!=ESP_ERR_NVS_NOT_FOUND){e=one;break;}
            }
            if(e==ESP_OK)e=nvs_commit(net);
            nvs_close(net);
            if(e!=ESP_OK)return e;
        }else return ESP_FAIL;
        esp_err_t wiped=clear_namespace("mira_idf_pw");
        if(wiped!=ESP_OK)return wiped;
        wifi_config_t empty={0};
        esp_err_t e=esp_wifi_disconnect();
        if(e!=ESP_OK && e!=ESP_ERR_WIFI_NOT_CONNECT)return e;
        return esp_wifi_set_config(WIFI_IF_STA,&empty);
    }
    if(!strcmp(scope,"rstparams")){
        // Match beta's reset of parameters and LVGL configuration,
        // but preserve network, setup key and OTA boot partitions.
        if(!erase_named_file("/spiffs/params.json")||
           !erase_named_file("/spiffs/miralvgl.json"))return ESP_FAIL;
        // The Arduino beta recreates BOTH files after a parameters reset.
        // Match that schema instead of leaving missing files behind.
        FILE *params=fopen("/spiffs/params.json","wb");
        if(!params)return ESP_FAIL;
        bool saved=fwrite("{\"params\":[]}",1,13,params)==13;
        if(fclose(params)!=0)saved=false;
        if(!saved)return ESP_FAIL;
        FILE *lvgl=fopen("/spiffs/miralvgl.json","wb");
        if(!lvgl)return ESP_FAIL;
        saved=fwrite("{\"lvgl\":[]}",1,11,lvgl)==11;
        if(fclose(lvgl)!=0)saved=false;
        if(!saved)return ESP_FAIL;
        esp_err_t e=clear_namespace("mira_idf_ui");
        if(e!=ESP_OK)return e;
        e=clear_namespace("mira_idf_mqtt");
        if(e!=ESP_OK)return e;
        e=clear_namespace("mira_idf_jeed");
        return e;
    }
    if(!strcmp(scope,"rstall")){
        // Only after an authenticated, fresh POST confirmation.
        // The factory/OTA app partitions are intentionally never touched.
        esp_err_t e=esp_spiffs_format(NULL);
        if(e!=ESP_OK)return e;
        (void)esp_wifi_stop();
        e=nvs_flash_deinit();
        if(e!=ESP_OK)return e;
        return nvs_flash_erase();
    }
    return ESP_ERR_INVALID_ARG;
}
esp_err_t mira_beta_data_reset_post(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    if(req->content_len<15||req->content_len>128)
        return reject(req,HTTPD_400_BAD_REQUEST,"Formulaire de confirmation invalide");
    char form[129]={0};
    size_t done=0;
    while(done<req->content_len){
        int n=httpd_req_recv(req,form+done,req->content_len-done);
        if(n<=0)return reject(req,HTTPD_408_REQ_TIMEOUT,"Formulaire incomplet");
        done+=(size_t)n;
    }
    form[done]=0;
    char *scope=NULL,*nonce=NULL,*save=NULL;
    for(char *part=strtok_r(form,"&",&save);part;part=strtok_r(NULL,"&",&save)){
        if(!strncmp(part,"scope=",6))scope=part+6;
        if(!strncmp(part,"nonce=",6))nonce=part+6;
    }
    unsigned long entered=0;
    char trailing=0;
    bool good=nonce&&scope&&reset_label(scope)!=NULL&&
        sscanf(nonce,"%lx%c",&entered,&trailing)==1&&entered>0&&entered<=UINT32_MAX&&
        (uint32_t)entered==reset_token&&
        !strcmp(scope,reset_scope)&&reset_issued_us>0&&
        esp_timer_get_time()-reset_issued_us<180000000LL;
    if(!good)return reject(req,HTTPD_403_FORBIDDEN,"Confirmation expirée ou invalide");
    // Single-use token, consumed BEFORE erasure to prevent replay.
    reset_token=0;
    reset_issued_us=0;
    char operation[16];
    snprintf(operation,sizeof(operation),"%s",scope);
    esp_err_t result=do_scope(operation);
    if(result!=ESP_OK){
        ESP_LOGE(TAG,"Reset %s failed: %s",operation,esp_err_to_name(result));
        return reject(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Effacement non terminé");
    }
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t sent=httpd_resp_sendstr(req,
       "<!doctype html><meta charset=\"utf-8\"><h2>Suppression effectuée</h2>"
       "<p>Mira Panel redémarre.</p>");
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return sent;
}
