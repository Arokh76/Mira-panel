// Compatibility with the beta's /api/checkupdate, GET /update, /updatemodule.
// Updates must be *explicitly* published for the native IDF hardware target.
// In particular, an Arduino OTA for the same panel is NOT an IDF release.
// The System page still downloads verified update files in the browser and
// uploads them via /doUpdateFile. No remote binary is flashed by this module.
#include "mira_beta_update.h"
#include "mira_beta_web.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#define MIRA_IDF_PROFILE "waveshare-esp32s3-touch-lcd-4.3c-idf"
#define MIRA_IDF_BASELINE "0.7.37"
#define MIRA_IDF_LABEL "0.7.37-IDF-beta"
#define MANIFEST_URL "https://raw.githubusercontent.com/Arokh76/Mira-panel/main/firmware/targets.json"
#define APPROVED_OTA_PREFIX "https://raw.githubusercontent.com/Arokh76/Mira-panel/main/firmware/releases/"
#define MANIFEST_LIMIT 8192
static const char *TAG="mira-idf-update";

static bool parse_version(const char *text,uint32_t *parts){
    if(!text||!*text||!parts)return false;
    const char *p=text;
    for(int i=0;i<3;i++){
        if(*p<'0'||*p>'9')return false;
        uint32_t n=0;
        for(;*p>='0'&&*p<='9';p++){
            unsigned d=(unsigned)(*p-'0');
            if(n>10000||(n==10000&&d))return false;
            n=n*10u+d;
        }
        parts[i]=n;
        if(i<2){if(*p!='.')return false;p++;}
    }
    return !*p;
}
static int compare_version(const char *a,const char *b){
    uint32_t v[3]={0},w[3]={0};
    if(!parse_version(a,v)||!parse_version(b,w))return -2;
    for(int i=0;i<3;i++){
        if(v[i]>w[i])return 1;
        if(v[i]<w[i])return -1;
    }
    return 0;
}
static char *fetch_manifest(void){
    const esp_http_client_config_t cfg={
        .url=MANIFEST_URL,
        .timeout_ms=6500,
        .disable_auto_redirect=true,
        .crt_bundle_attach=esp_crt_bundle_attach
    };
    esp_http_client_handle_t http=esp_http_client_init(&cfg);
    if(!http)return NULL;
    char *body=NULL;
    do{
        if(esp_http_client_open(http,0)!=ESP_OK)break;
        int64_t expected=esp_http_client_fetch_headers(http);
        if(esp_http_client_get_status_code(http)!=200||expected>MANIFEST_LIMIT)break;
        body=malloc(MANIFEST_LIMIT+1);
        if(!body)break;
        size_t used=0;
        int64_t deadline=esp_timer_get_time()+12500000LL;
        while(used<MANIFEST_LIMIT&&esp_timer_get_time()<deadline){
            int n=esp_http_client_read(http,body+used,(int)(MANIFEST_LIMIT-used));
            if(n<0)break;
            if(n==0){
                if(expected>=0&&used>=(size_t)expected)break;
                // Chunked HTTP may finish without a Content-Length.
                if(esp_http_client_is_complete_data_received(http))break;
                vTaskDelay(pdMS_TO_TICKS(20));continue;
            }
            used+=(size_t)n;
            if(expected>=0&&used>=(size_t)expected)break;
        }
        if(used<2 || used==MANIFEST_LIMIT ||
           (expected>=0&&used!=(size_t)expected)){
            free(body);body=NULL;break;
        }
        body[used]=0;
    }while(0);
    esp_http_client_close(http);
    esp_http_client_cleanup(http);
    return body;
}
static esp_err_t json_response(httpd_req_t *req,const char *status,
    const char *message,const char *remote){
    cJSON *root=cJSON_CreateObject();
    if(!root)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON impossible");
    cJSON_AddStringToObject(root,"current",MIRA_IDF_LABEL);
    cJSON_AddStringToObject(root,"status",status);
    cJSON_AddStringToObject(root,"message",message);
    if(remote&&*remote)cJSON_AddStringToObject(root,"remote",remote);
    char *encoded=cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!encoded)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON impossible");
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_sendstr(req,encoded);
    cJSON_free(encoded);
    return result;
}
esp_err_t mira_beta_update_check(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    char *manifest=fetch_manifest();
    if(!manifest)
        return json_response(req,"error","Manifeste GitHub IDF inaccessible.",NULL);
    cJSON *json=cJSON_Parse(manifest);
    free(manifest);
    if(!json)return json_response(req,"error","Manifeste de versions invalide.",NULL);
    const cJSON *project=cJSON_GetObjectItemCaseSensitive(json,"project");
    const cJSON *version=cJSON_GetObjectItemCaseSensitive(json,"version");
    const cJSON *targets=cJSON_GetObjectItemCaseSensitive(json,"targets");
    const cJSON *native=cJSON_GetObjectItemCaseSensitive(targets,MIRA_IDF_PROFILE);
    const cJSON *url=cJSON_GetObjectItemCaseSensitive(native,"ota");
    bool valid=cJSON_IsString(project)&&!strcmp(project->valuestring,"Mira Panel")&&
               cJSON_IsString(version)&&cJSON_IsString(url)&&
               url->valuestring&&
               !strncmp(url->valuestring,APPROVED_OTA_PREFIX,strlen(APPROVED_OTA_PREFIX))&&
               strlen(url->valuestring)>strlen(APPROVED_OTA_PREFIX);
    char remote[32]={0};
    if(valid && strlen(version->valuestring)<sizeof(remote))
        snprintf(remote,sizeof(remote),"%s",version->valuestring);
    else valid=false;
    const int newer=valid?compare_version(remote,MIRA_IDF_BASELINE):-2;
    cJSON_Delete(json);
    if(!valid||newer==-2)
        return json_response(req,"error","Aucun OTA ESP-IDF 4.3C officiel publié pour cette cible.",NULL);
    if(newer<=0)
        return json_response(req,"uptodate","Version IDF à jour.",remote);
    return json_response(req,"available",
        "Mise à jour IDF publiée. Utiliser Système pour vérifier et installer.",remote);
}
esp_err_t mira_beta_update_legacy_page(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    httpd_resp_set_status(req,"303 See Other");
    httpd_resp_set_hdr(req,"Location","/load?nav=system");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,"Voir Système");
}
esp_err_t mira_beta_update_start(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    // In the beta, this endpoint launches an OTA from a saved URL. Do not
    // reuse the Arduino URL in native IDF. Actual IDF updates go through the
    // System page download and authenticated /doUpdateFile upload.
    httpd_resp_set_status(req,"409 Conflict");
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    return httpd_resp_sendstr(req,
        "Utiliser la page Systeme pour installer un OTA ESP-IDF publie pour ce materiel.");
}
