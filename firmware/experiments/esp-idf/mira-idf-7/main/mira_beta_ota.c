// Port of the beta /doUpdateFile form: multipart/form-data POST "update".
// Stream directly to the inactive OTA partition. Never touch NVS/SPIFFS.
// ESP-IDF validates the image and target chip via esp_ota_end().
#include "mira_beta_ota.h"
#include "mira_beta_web.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define HEADER_CAP 3072
#define BUFFER_CAP 4096
static const char *TAG="mira-beta-ota";

static const uint8_t *locate(const uint8_t *buf,size_t len,
                             const uint8_t *needle,size_t n){
    if(!buf||!needle||!n||n>len)return NULL;
    for(size_t i=0;i+n<=len;i++)
        if(buf[i]==needle[0]&&!memcmp(buf+i,needle,n))return buf+i;
    return NULL;
}
static esp_err_t fail(httpd_req_t *req,httpd_err_code_t code,const char *reason){
    return httpd_resp_send_err(req,code,reason);
}
static bool ota_multipart_boundary(httpd_req_t *req,char *boundary,size_t cap){
    size_t n=httpd_req_get_hdr_value_len(req,"Content-Type");
    if(!n||n>=180)return false;
    char type[180]={0};
    if(httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||
       strncmp(type,"multipart/form-data;",20))return false;
    char *p=strstr(type,"boundary=");
    if(!p)return false;
    p+=9;
    char quote=0;
    if(*p=='"'){quote='"';p++;}
    size_t count=0;
    while(*p&&*p!=';'&&*p!=' '&&*p!=quote){
        unsigned char c=(unsigned char)*p++;
        if(!(c>='A'&&c<='Z')&&!(c>='a'&&c<='z')&&
           !(c>='0'&&c<='9')&&c!='-'&&c!='_')return false;
        if(count+1>=cap)return false;
        boundary[count++]=(char)c;
    }
    boundary[count]=0;
    return count>=8&&count<=70&&(!quote||*p==quote);
}
static int receive(httpd_req_t *req,void *buffer,size_t amount){
    int attempts=0;
    for(;;){
        int n=httpd_req_recv(req,buffer,amount);
        if(n==HTTPD_SOCK_ERR_TIMEOUT&&++attempts<3)continue;
        return n;
    }
}
// Keep last boundary-length bytes across socket reads so a closing marker
// split across TCP chunks can never leak into esp_ota_write().
static esp_err_t append_ota(esp_ota_handle_t ota,uint8_t *pending,size_t *used,
    const uint8_t *src,size_t count,const uint8_t *marker,size_t marker_len,
    bool *done,size_t *written){
    if(*done||*used+count>BUFFER_CAP)return ESP_ERR_INVALID_SIZE;
    memcpy(pending+*used,src,count);
    *used+=count;
    for(size_t i=0;i+marker_len+2<=*used;i++){
        if(pending[i]==marker[0]&&
           !memcmp(pending+i,marker,marker_len)&&
           pending[i+marker_len]=='-'&&pending[i+marker_len+1]=='-'){
            if(i){
                esp_err_t err=esp_ota_write(ota,pending,i);
                if(err!=ESP_OK)return err;
                *written+=i;
            }
            *done=true;
            return ESP_OK;
        }
    }
    const size_t retain=marker_len+1;
    if(*used>retain){
        size_t flush=*used-retain;
        esp_err_t err=esp_ota_write(ota,pending,flush);
        if(err!=ESP_OK)return err;
        *written+=flush;
        memmove(pending,pending+flush,retain);
        *used=retain;
    }
    return ESP_OK;
}
esp_err_t mira_beta_ota_upload(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    const esp_partition_t *target=esp_ota_get_next_update_partition(NULL);
    if(!target||req->content_len<1024 ||
       (size_t)req->content_len>target->size+HEADER_CAP)
        return fail(req,HTTPD_400_BAD_REQUEST,"Taille OTA ou partition invalide");
    char boundary[80]={0};
    if(!ota_multipart_boundary(req,boundary,sizeof(boundary)))
        return fail(req,HTTPD_400_BAD_REQUEST,"Formulaire multipart beta attendu");
    char prefix[85],marker[88];
    int p=snprintf(prefix,sizeof(prefix),"--%s\r\n",boundary);
    int m=snprintf(marker,sizeof(marker),"\r\n--%s",boundary);
    if(p<=0||m<=0||p>=(int)sizeof(prefix)||m>=(int)sizeof(marker))
        return fail(req,HTTPD_400_BAD_REQUEST,"Boundary multipart invalide");
    uint8_t *header=malloc(HEADER_CAP);
    uint8_t *pending=malloc(BUFFER_CAP);
    if(!header||!pending){
        free(header);free(pending);
        return fail(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Mémoire insuffisante");
    }
    size_t head_count=0,remaining=(size_t)req->content_len;
    const uint8_t *body=NULL;
    const uint8_t end_headers[]="\r\n\r\n";
    while(!body&&remaining&&head_count<HEADER_CAP){
        size_t amount=remaining;
        if(amount>512)amount=512;
        if(amount>HEADER_CAP-head_count)amount=HEADER_CAP-head_count;
        int n=receive(req,header+head_count,amount);
        if(n<=0)break;
        head_count+=(size_t)n;
        remaining-=(size_t)n;
        const uint8_t *split=locate(header,head_count,end_headers,4);
        if(split)body=split+4;
    }
    const size_t metadata_size=body?(size_t)(body-header):0;
    // First part must be exactly the firmware file from the original beta
    // form; do not treat an arbitrary HTML field as flash content.
    bool valid=body&&metadata_size>=strlen(prefix)+32&&
        !memcmp(header,prefix,strlen(prefix));
    if(valid){
        header[metadata_size-4]=0;
        const char *meta=(const char *)header;
        valid=strstr(meta,"Content-Disposition: form-data;")!=NULL &&
            strstr(meta,"name=\"update\"")!=NULL&&strstr(meta,"filename=\"")!=NULL;
    }
    if(!valid||!body||head_count==metadata_size){
        free(header);free(pending);
        return fail(req,HTTPD_400_BAD_REQUEST,"Fichier OTA manquant ou multipart invalide");
    }
    // Raw ESP image magic, not a factory image or an HTML error page.
    if(body[0]!=0xe9){
        free(header);free(pending);
        return fail(req,HTTPD_400_BAD_REQUEST,"Le fichier ne contient pas une image OTA ESP");
    }
    esp_ota_handle_t ota=0;
    esp_err_t err=esp_ota_begin(target,OTA_SIZE_UNKNOWN,&ota);
    if(err!=ESP_OK){
        free(header);free(pending);
        return fail(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Initialisation OTA impossible");
    }
    size_t used=0,written=0;
    bool done=false;
    err=append_ota(ota,pending,&used,body,head_count-metadata_size,
                   (const uint8_t*)marker,(size_t)m,&done,&written);
    free(header);
    while(err==ESP_OK&&!done&&remaining){
        uint8_t socket[2048];
        size_t amount=remaining<sizeof(socket)?remaining:sizeof(socket);
        int n=receive(req,socket,amount);
        if(n<=0){err=ESP_FAIL;break;}
        remaining-=(size_t)n;
        err=append_ota(ota,pending,&used,socket,(size_t)n,
                       (const uint8_t*)marker,(size_t)m,&done,&written);
    }
    free(pending);
    if(err==ESP_OK&&(!done||written<65536||written>target->size))
        err=ESP_ERR_INVALID_SIZE;
    if(err==ESP_OK)err=esp_ota_end(ota);
    else esp_ota_abort(ota);
    if(err!=ESP_OK){
        ESP_LOGW(TAG,"Beta browser OTA rejected: %s",esp_err_to_name(err));
        return fail(req,HTTPD_400_BAD_REQUEST,"Firmware OTA invalide, actif inchangé");
    }
    /* ESP32-S3 chip validation alone does NOT distinguish Arduino builds
     * from this native ESP-IDF Waveshare 7 STD port. Inspect the downloaded app image
     * descriptor before selecting the next boot partition.
     */
    esp_app_desc_t candidate={0};
    const esp_app_desc_t *running=esp_app_get_description();
    if(!running||
       esp_ota_get_partition_description(target,&candidate)!=ESP_OK||
       strcmp(candidate.project_name,running->project_name)!=0||
       strcmp(candidate.project_name,"mira_idf_7std")!=0){
        ESP_LOGW(TAG,"Manual OTA rejected: not a native Mira 7 STD app");
        return fail(req,HTTPD_400_BAD_REQUEST,
                    "Image ESP-IDF Waveshare 7 STD requise (Arduino interdit)");
    }
    err=esp_ota_set_boot_partition(target);
    if(err!=ESP_OK)
        return fail(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Selection de partition OTA impossible");
    ESP_LOGI(TAG,"Beta firmware uploaded: %u bytes verified, restarting",(unsigned)written);
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    esp_err_t reply=httpd_resp_sendstr(req,"OK");
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
    return reply;
}
