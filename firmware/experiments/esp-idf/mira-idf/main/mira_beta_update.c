// Compatibility with the beta's /api/checkupdate, GET /update, /updatemodule.
// Updates must be *explicitly* published for the native IDF hardware target.
// In particular, an Arduino OTA for the same panel is NOT an IDF release.
// The System page still downloads verified update files in the browser and
// uploads them via /doUpdateFile. No remote binary is flashed by this module.
#include "mira_beta_update.h"
#include "mira_beta_web.h"
#include "mira_jeedom.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "mbedtls/md.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

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
            if(n>999u||(n==999u&&d>9u))return false;
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
static const char *current_idf_version(void){
    const esp_app_desc_t *running=esp_app_get_description();
    if(running && compare_version(running->version,running->version)==0)
        return running->version;
    return MIRA_IDF_BASELINE;
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
    const cJSON *sha=cJSON_GetObjectItemCaseSensitive(native,"otaSha256");
    const cJSON *bytes=cJSON_GetObjectItemCaseSensitive(native,"otaBytes");
    bool valid=cJSON_IsString(project)&&!strcmp(project->valuestring,"Mira Panel")&&
               cJSON_IsString(version)&&cJSON_IsString(url)&&
               cJSON_IsString(sha)&&sha->valuestring&&
               strlen(sha->valuestring)==64&&cJSON_IsNumber(bytes)&&
               bytes->valuedouble>=65536&&bytes->valuedouble<=4194304&&
               url->valuestring&&
               !strncmp(url->valuestring,APPROVED_OTA_PREFIX,strlen(APPROVED_OTA_PREFIX))&&
               strstr(url->valuestring,"/waveshare-esp32s3-touch-lcd-4.3c-idf/ota.bin")&&
               strlen(url->valuestring)>strlen(APPROVED_OTA_PREFIX);
    char remote[32]={0};
    if(valid && strlen(version->valuestring)<sizeof(remote))
        snprintf(remote,sizeof(remote),"%s",version->valuestring);
    else valid=false;
    const int newer=valid?compare_version(remote,current_idf_version()):-2;
    cJSON_Delete(json);
    if(!valid||newer==-2)
        return json_response(req,"error","Aucun OTA ESP-IDF 4.3C officiel publié pour cette cible.",NULL);
    if(newer<=0)
        return json_response(req,"uptodate","Version IDF à jour.",remote);
    return json_response(req,"available",
        "Mise à jour IDF publiée. Utiliser Système pour vérifier et installer.",remote);
}
esp_err_t mira_beta_update_legacy_page(httpd_req_t *req){
    /* /update without a key is the maintenance page; the original beta also
     * serves authenticated GET /update?key=...&button/toggle/info/... . */
    char query[2048]={0},key[128]={0};
    if(httpd_req_get_url_query_str(req,query,sizeof(query))==ESP_OK &&
       httpd_query_key_value(query,"key",key,sizeof(key))==ESP_OK)
        return mira_jeedom_beta_legacy_get_update(req);
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    httpd_resp_set_status(req,"303 See Other");
    httpd_resp_set_hdr(req,"Location","/load?nav=system");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,"Voir Système");
}
/* The original /updatemodule GET route is kept for compatibility only.
 * Do NOT let a GET (browser prefetch, web crawler, image, etc.) flash firmware.
 * An explicit authenticated POST /updatemodule with X-Mira-OTA: 1 is required.
 */
esp_err_t mira_beta_update_start(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    httpd_resp_set_status(req,"405 Method Not Allowed");
    httpd_resp_set_hdr(req,"Allow","POST");
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    return httpd_resp_sendstr(req,"Utiliser POST /updatemodule depuis Système Mira");
}

static portMUX_TYPE ota_mux=portMUX_INITIALIZER_UNLOCKED;
static bool ota_active;
static char ota_step[32]="inactive";
static char ota_message[120]="Aucune installation en cours";
static uint32_t ota_done,ota_total;
static void ota_update_status(const char *step,const char *message,
                              uint32_t done,uint32_t total){
    portENTER_CRITICAL(&ota_mux);
    snprintf(ota_step,sizeof(ota_step),"%s",step);
    snprintf(ota_message,sizeof(ota_message),"%s",message);
    ota_done=done;ota_total=total;
    portEXIT_CRITICAL(&ota_mux);
}
static bool ota_valid_hex(const char *hex){
    if(!hex||strlen(hex)!=64)return false;
    for(const unsigned char *p=(const unsigned char*)hex;*p;p++)
        if(!isxdigit(*p))return false;
    return true;
}
static bool ota_manifest_entry(char url[420],char expected_hash[65],
                               size_t *bytes,char version_out[32]){
    char *raw=fetch_manifest();
    if(!raw)return false;
    cJSON *manifest=cJSON_Parse(raw);
    free(raw);
    if(!manifest)return false;
    const cJSON *project=cJSON_GetObjectItemCaseSensitive(manifest,"project");
    const cJSON *version=cJSON_GetObjectItemCaseSensitive(manifest,"version");
    const cJSON *targets=cJSON_GetObjectItemCaseSensitive(manifest,"targets");
    const cJSON *native=cJSON_GetObjectItemCaseSensitive(targets,MIRA_IDF_PROFILE);
    const cJSON *ota=cJSON_GetObjectItemCaseSensitive(native,"ota");
    const cJSON *sha=cJSON_GetObjectItemCaseSensitive(native,"otaSha256");
    const cJSON *size=cJSON_GetObjectItemCaseSensitive(native,"otaBytes");
    const esp_partition_t *partition=esp_ota_get_next_update_partition(NULL);
    bool accepted=cJSON_IsString(project)&&!strcmp(project->valuestring,"Mira Panel")&&
                  cJSON_IsString(version)&&
                  compare_version(version->valuestring,current_idf_version())>0&&
                  strlen(version->valuestring)<32&&
                  cJSON_IsString(ota)&&cJSON_IsString(sha)&&
                  ota_valid_hex(sha->valuestring)&&cJSON_IsNumber(size)&&
                  partition&&size->valuedouble>=65536&&
                  size->valuedouble<=partition->size&&
                  (double)(size_t)size->valuedouble==size->valuedouble;
    if(accepted){
        const size_t n=strlen(ota->valuestring);
        const char *suffix="/waveshare-esp32s3-touch-lcd-4.3c-idf/ota.bin";
        const size_t end=strlen(suffix);
        /* Only the project-owned HTTPS release path, ending in this exact
         * native target. URL parameters, redirects, other repos and Arduino
         * .bin paths are all forbidden.
         */
        accepted=n<420&&n>strlen(APPROVED_OTA_PREFIX)+end&&
            !strncmp(ota->valuestring,APPROVED_OTA_PREFIX,
                     strlen(APPROVED_OTA_PREFIX))&&
            !strcmp(ota->valuestring+n-end,suffix);
    }
    if(accepted){
        snprintf(url,420,"%s",ota->valuestring);
        snprintf(expected_hash,65,"%s",sha->valuestring);
        *bytes=(size_t)size->valuedouble;
        snprintf(version_out,32,"%s",version->valuestring);
    }
    cJSON_Delete(manifest);
    return accepted;
}
/* Firmware download runs only on its dedicated FreeRTOS worker. The web
 * server and the LVGL task are never blocked while downloading/flashing.
 * Exact SHA-256, image size, ESP-IDF target project and ESP image validation
 * are ALL required before changing the boot partition.
 */
static void ota_install_worker(void *unused){
    (void)unused;
    char url[420]={0},digest[65]={0},remote_version[32]={0};
    size_t expected=0;
    ota_update_status("checking","Vérification du manifeste ESP-IDF",0,0);
    if(!ota_manifest_entry(url,digest,&expected,remote_version)){
        ota_update_status("error","Aucune version IDF 4.3C signalee par un manifeste valide",0,0);
        goto finish;
    }
    const esp_partition_t *partition=esp_ota_get_next_update_partition(NULL);
    if(!partition||expected>partition->size){
        ota_update_status("error","Partition OTA insuffisante",0,0);
        goto finish;
    }
    ota_update_status("downloading","Téléchargement HTTPS du firmware IDF",0,(uint32_t)expected);
    esp_http_client_config_t cfg={
        .url=url,
        .timeout_ms=12000,
        .disable_auto_redirect=true,
        .crt_bundle_attach=esp_crt_bundle_attach
    };
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client){
        ota_update_status("error","Initialisation HTTP impossible",0,0);
        goto finish;
    }
    esp_ota_handle_t handle=0;
    bool begun=false,ended=false;
    uint8_t *buffer=NULL;
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    size_t got=0;
    esp_err_t err=ESP_FAIL;
    do{
        if(esp_http_client_open(client,0)!=ESP_OK)break;
        int64_t length=esp_http_client_fetch_headers(client);
        if(esp_http_client_get_status_code(client)!=200||
           (length>=0&&(size_t)length!=expected))break;
        const mbedtls_md_info_t *info=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if(!info||mbedtls_md_setup(&md,info,0)!=0||
           mbedtls_md_starts(&md)!=0)break;
        buffer=malloc(4096);
        if(!buffer)break;
        if(esp_ota_begin(partition,expected,&handle)!=ESP_OK)break;
        begun=true;
        const int64_t deadline=esp_timer_get_time()+180000000LL;
        while(got<expected&&esp_timer_get_time()<deadline){
            const size_t remain=expected-got;
            int n=esp_http_client_read(client,(char*)buffer,
                                       (int)(remain<4096?remain:4096));
            if(n<0)break;
            if(n==0){
                if(esp_http_client_is_complete_data_received(client))break;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if(mbedtls_md_update(&md,buffer,(size_t)n)!=0||
               esp_ota_write(handle,buffer,(size_t)n)!=ESP_OK)break;
            got+=(size_t)n;
            if((got&0xffff)<4096 || got==expected)
                ota_update_status("downloading","Téléchargement en cours",
                                  (uint32_t)got,(uint32_t)expected);
        }
        if(got!=expected||!esp_http_client_is_complete_data_received(client))
            break;
        uint8_t raw_hash[32]={0};
        char hex[65];
        if(mbedtls_md_finish(&md,raw_hash)!=0)break;
        for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",raw_hash[i]);
        hex[64]=0;
        if(strcasecmp(hex,digest)!=0){
            ota_update_status("error","Empreinte SHA-256 invalide",0,0);
            break;
        }
        ota_update_status("verifying","Vérification de l'image ESP-IDF",got,expected);
        if(esp_ota_end(handle)!=ESP_OK)break;
        ended=true;
        esp_app_desc_t candidate={0};
        const esp_app_desc_t *running=esp_app_get_description();
        if(!running||esp_ota_get_partition_description(partition,&candidate)!=ESP_OK||
           strcmp(candidate.project_name,running->project_name)!=0 ||
           strcmp(candidate.project_name,"mira_idf_scroll")!=0||
           strcmp(candidate.version,remote_version)!=0){
            ota_update_status("error","Image refusée : projet ESP-IDF 4.3C incompatible",0,0);
            break;
        }
        if(esp_ota_set_boot_partition(partition)!=ESP_OK)break;
        ota_update_status("reboot","Mise à jour IDF validée. Redémarrage...",got,expected);
        err=ESP_OK;
    }while(0);
    if(begun&&!ended)esp_ota_abort(handle);
    free(buffer);
    mbedtls_md_free(&md);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if(err!=ESP_OK){
        if(strcmp(ota_step,"error"))
            ota_update_status("error","Téléchargement ou image OTA refusée",0,0);
        goto finish;
    }
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
finish:
    portENTER_CRITICAL(&ota_mux);
    ota_active=false;
    portEXIT_CRITICAL(&ota_mux);
    vTaskDelete(NULL);
}
esp_err_t mira_beta_update_trigger(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    char header[8]={0};
    if(httpd_req_get_hdr_value_str(req,"X-Mira-OTA",header,sizeof(header))!=ESP_OK||
       strcmp(header,"1")){
        httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"X-Mira-OTA: 1 obligatoire");
        return ESP_FAIL;
    }
    portENTER_CRITICAL(&ota_mux);
    bool busy=ota_active;
    if(!busy)ota_active=true;
    portEXIT_CRITICAL(&ota_mux);
    if(busy){
        httpd_resp_set_status(req,"409 Conflict");
        return httpd_resp_sendstr(req,"Une installation OTA est deja en cours");
    }
    ota_update_status("checking","Demande de mise à jour IDF acceptée",0,0);
    BaseType_t ok=xTaskCreate(ota_install_worker,"mira_idf_ota",8192,NULL,3,NULL);
    if(ok!=pdPASS){
        portENTER_CRITICAL(&ota_mux);
        ota_active=false;
        portEXIT_CRITICAL(&ota_mux);
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Tâche OTA impossible");
        return ESP_FAIL;
    }
    httpd_resp_set_status(req,"202 Accepted");
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    return httpd_resp_sendstr(req,"{\"started\":true,\"hardware\":\"waveshare-43c-idf\"}");
}
esp_err_t mira_beta_update_status(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    char step[32],message[120];uint32_t done=0,total=0;bool active;
    portENTER_CRITICAL(&ota_mux);
    snprintf(step,sizeof(step),"%s",ota_step);
    snprintf(message,sizeof(message),"%s",ota_message);
    done=ota_done;total=ota_total;active=ota_active;
    portEXIT_CRITICAL(&ota_mux);
    cJSON *root=cJSON_CreateObject();
    if(!root){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON impossible");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(root,"step",step);
    cJSON_AddStringToObject(root,"message",message);
    cJSON_AddNumberToObject(root,"received",done);
    cJSON_AddNumberToObject(root,"total",total);
    cJSON_AddBoolToObject(root,"active",active);
    char *reply=cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!reply){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON impossible");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_sendstr(req,reply);
    cJSON_free(reply);
    return result;
}
