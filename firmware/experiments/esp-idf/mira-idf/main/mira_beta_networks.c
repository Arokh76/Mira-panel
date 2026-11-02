// Original beta network records: "reseau":[{id,ssid,pass,ip,ip_mode,
// mask,gateway,dns1,dns2}]. Read untouched from reseau.json on first use.
// Native edits are kept separately in NVS. Rollback never overwrites
// an Arduino beta file or re-encrypts a secret with a different algorithm.
#include "mira_beta_networks.h"
#include "mira_legacy_cipher.h"
#include "mira_recovery.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#define BETA_RESEAU_PATH "/spiffs/reseau.json"
#define BETA_OVERLAY_KEY "wifi_profiles"
#define BETA_NET_NS "mira_idf_net"
#define BETA_PASSWORD_NS "mira_idf_pw"
#define BETA_MAX_JSON 3800
static const char *TAG="mira-beta-networks";

static bool field(const cJSON *obj,const char *key,char *out,size_t cap){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    if(!cJSON_IsString(v)||!v->valuestring||strlen(v->valuestring)>=cap)return false;
    snprintf(out,cap,"%s",v->valuestring);
    return true;
}
static int record_id(const cJSON *obj){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,"id");
    if(cJSON_IsNumber(v))return v->valueint;
    if(cJSON_IsString(v)&&v->valuestring){
        char *end=NULL;
        long n=strtol(v->valuestring,&end,10);
        if(end!=v->valuestring&&! *end &&n>=0&&n<=999)return (int)n;
    }
    return -1;
}
static cJSON *read_legacy(void){
    FILE *f=fopen(BETA_RESEAU_PATH,"rb");
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}
    long size=ftell(f);
    if(size<2||size>BETA_MAX_JSON||fseek(f,0,SEEK_SET)!=0){fclose(f);return NULL;}
    char *buf=malloc((size_t)size+1);
    if(!buf){fclose(f);return NULL;}
    size_t got=fread(buf,1,(size_t)size,f);
    fclose(f);
    if(got!=(size_t)size){free(buf);return NULL;}
    buf[size]=0;
    cJSON *root=cJSON_ParseWithLength(buf,(size_t)size);
    free(buf);
    if(root&&!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root,"reseau"))){
        cJSON_Delete(root);return NULL;
    }
    return root;
}
static cJSON *load(void){
    nvs_handle_t nv;
    if(nvs_open(BETA_NET_NS,NVS_READONLY,&nv)==ESP_OK){
        size_t length=0;
        if(nvs_get_str(nv,BETA_OVERLAY_KEY,NULL,&length)==ESP_OK&&
           length>2&&length<=BETA_MAX_JSON){
            char *buf=malloc(length);
            if(buf&&nvs_get_str(nv,BETA_OVERLAY_KEY,buf,&length)==ESP_OK){
                cJSON *root=cJSON_Parse(buf);
                free(buf);
                if(root&&cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root,"reseau"))){
                    nvs_close(nv);
                    return root;
                }
                cJSON_Delete(root);
                ESP_LOGW(TAG,"Saved native network overlay invalid; reading beta original");
            }else free(buf);
        }
        nvs_close(nv);
    }
    cJSON *root=read_legacy();
    if(root)return root;
    root=cJSON_CreateObject();
    if(root)cJSON_AddArrayToObject(root,"reseau");
    return root;
}
static esp_err_t save(cJSON *root){
    char *json=cJSON_PrintUnformatted(root);
    if(!json)return ESP_ERR_NO_MEM;
    size_t size=strlen(json);
    if(size>=BETA_MAX_JSON){cJSON_free(json);return ESP_ERR_INVALID_SIZE;}
    nvs_handle_t nv;
    esp_err_t err=nvs_open(BETA_NET_NS,NVS_READWRITE,&nv);
    if(err==ESP_OK){
        err=nvs_set_str(nv,BETA_OVERLAY_KEY,json);
        if(err==ESP_OK)err=nvs_commit(nv);
        nvs_close(nv);
    }
    cJSON_free(json);
    return err;
}
static void copy_record(const cJSON *entry,mira_beta_network_t *out){
    memset(out,0,sizeof(*out));
    out->id=record_id(entry);
    field(entry,"ssid",out->ssid,sizeof(out->ssid));
    if(!field(entry,"ip_mode",out->ip_mode,sizeof(out->ip_mode))||
       (!strcmp(out->ip_mode,"")||!strcmp(out->ip_mode,"null"))){
        char ip[32]="";
        (void)field(entry,"ip",ip,sizeof(ip));
        snprintf(out->ip_mode,sizeof(out->ip_mode),"%s",
                 !*ip||!strcmp(ip,"Auto")||!strcmp(ip,"null")?"auto":"fixed_auto");
    }
    field(entry,"ip",out->ip,sizeof(out->ip));
    field(entry,"mask",out->mask,sizeof(out->mask));
    field(entry,"gateway",out->gateway,sizeof(out->gateway));
    field(entry,"dns1",out->dns1,sizeof(out->dns1));
    field(entry,"dns2",out->dns2,sizeof(out->dns2));
}
esp_err_t mira_beta_networks_list(mira_beta_network_t *out,size_t *count,size_t cap){
    if(!out||!count||!cap)return ESP_ERR_INVALID_ARG;
    *count=0;
    cJSON *root=load();
    if(!root)return ESP_ERR_NO_MEM;
    const cJSON *array=cJSON_GetObjectItemCaseSensitive(root,"reseau"),*entry=NULL;
    cJSON_ArrayForEach(entry,array){
        if(*count==cap)break;
        mira_beta_network_t candidate;
        copy_record(entry,&candidate);
        if(candidate.id<0||candidate.id>999||!*candidate.ssid)continue;
        out[(*count)++]=candidate;
    }
    cJSON_Delete(root);
    return ESP_OK;
}
esp_err_t mira_beta_networks_get(int id,mira_beta_network_t *out){
    if(id<0||id>999||!out)return ESP_ERR_INVALID_ARG;
    cJSON *root=load();
    if(!root)return ESP_ERR_NO_MEM;
    const cJSON *entry=NULL,*array=cJSON_GetObjectItemCaseSensitive(root,"reseau");
    esp_err_t err=ESP_ERR_NOT_FOUND;
    cJSON_ArrayForEach(entry,array){
        if(record_id(entry)==id){
            copy_record(entry,out);
            err=ESP_OK;break;
        }
    }
    cJSON_Delete(root);
    return err;
}
static bool assign(cJSON *obj,const char *key,const char *value){
    cJSON *str=cJSON_CreateString(value?value:"");
    if(!str)return false;
    if(cJSON_HasObjectItem(obj,key))
        cJSON_ReplaceItemInObjectCaseSensitive(obj,key,str);
    else cJSON_AddItemToObject(obj,key,str);
    return true;
}
static void password_key(int id,char *key,size_t len){
    snprintf(key,len,"net_%d",id);
}
static esp_err_t save_password(int id,const char *password){
    if(!password||!*password)return ESP_OK;
    char key[16];password_key(id,key,sizeof(key));
    nvs_handle_t nv;
    esp_err_t err=nvs_open(BETA_PASSWORD_NS,NVS_READWRITE,&nv);
    if(err==ESP_OK){
        err=nvs_set_str(nv,key,password);
        if(err==ESP_OK)err=nvs_commit(nv);
        nvs_close(nv);
    }
    return err;
}
esp_err_t mira_beta_networks_upsert(const mira_beta_network_t *profile,const char *password){
    if(!profile||profile->id<0||profile->id>999||!*profile->ssid||
       strlen(profile->ssid)>32||!password||strlen(password)>63||
       (password[0]&&strlen(password)<8))return ESP_ERR_INVALID_ARG;
    if(strcmp(profile->ip_mode,"auto")&&strcmp(profile->ip_mode,"fixed_auto")&&
       strcmp(profile->ip_mode,"manual"))return ESP_ERR_INVALID_ARG;
    cJSON *root=load();
    if(!root)return ESP_ERR_NO_MEM;
    cJSON *arr=cJSON_GetObjectItemCaseSensitive(root,"reseau"),*entry=NULL;
    int count=cJSON_GetArraySize(arr);
    if(count>MIRA_NET_MAX){cJSON_Delete(root);return ESP_ERR_INVALID_SIZE;}
    cJSON_ArrayForEach(entry,arr)
        if(record_id(entry)==profile->id)break;
    bool existed=entry!=NULL;
    // Renaming a saved network without a password must not reuse the old
    // network's secret for a different SSID.
    char old_ssid[33]={0};
    if(existed)(void)field(entry,"ssid",old_ssid,sizeof(old_ssid));
    if(existed&&strcmp(old_ssid,profile->ssid)!=0&&!password[0]){
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if(!entry){
        if(count==MIRA_NET_MAX){cJSON_Delete(root);return ESP_ERR_INVALID_SIZE;}
        entry=cJSON_CreateObject();
        if(!entry){cJSON_Delete(root);return ESP_ERR_NO_MEM;}
        cJSON_AddNumberToObject(entry,"id",profile->id);
        cJSON_AddItemToArray(arr,entry);
    }
    bool ok=assign(entry,"ssid",profile->ssid)&&
       assign(entry,"ip_mode",profile->ip_mode)&&
       assign(entry,"ip",profile->ip)&&
       assign(entry,"mask",profile->mask)&&
       assign(entry,"gateway",profile->gateway)&&
       assign(entry,"dns1",profile->dns1)&&
       assign(entry,"dns2",profile->dns2);
    if(!existed&&ok)ok=assign(entry,"pass","");
    esp_err_t err=ok?save_password(profile->id,password):ESP_ERR_NO_MEM;
    if(err==ESP_OK)err=save(root);
    cJSON_Delete(root);
    return err;
}
esp_err_t mira_beta_networks_remove(int id){
    if(id<0||id>999)return ESP_ERR_INVALID_ARG;
    cJSON *root=load();
    if(!root)return ESP_ERR_NO_MEM;
    cJSON *arr=cJSON_GetObjectItemCaseSensitive(root,"reseau");
    const int n=cJSON_GetArraySize(arr);
    bool found=false;
    for(int i=0;i<n;i++){
        if(record_id(cJSON_GetArrayItem(arr,i))==id){
            cJSON_DeleteItemFromArray(arr,i);
            found=true;break;
        }
    }
    if(!found){cJSON_Delete(root);return ESP_ERR_NOT_FOUND;}
    esp_err_t err=save(root);
    cJSON_Delete(root);
    if(err!=ESP_OK)return err;
    char key[16];password_key(id,key,sizeof(key));
    nvs_handle_t nv;
    if(nvs_open(BETA_PASSWORD_NS,NVS_READWRITE,&nv)==ESP_OK){
        (void)nvs_erase_key(nv,key);
        (void)nvs_commit(nv);
        nvs_close(nv);
    }
    return ESP_OK;
}
esp_err_t mira_beta_networks_activate(int id){
    if(id<0||id>999)return ESP_ERR_INVALID_ARG;
    cJSON *root=load();
    if(!root)return ESP_ERR_NO_MEM;
    const cJSON *array=cJSON_GetObjectItemCaseSensitive(root,"reseau"),*entry=NULL;
    cJSON_ArrayForEach(entry,array)if(record_id(entry)==id)break;
    if(!entry){cJSON_Delete(root);return ESP_ERR_NOT_FOUND;}
    mira_beta_network_t profile;
    copy_record(entry,&profile);
    char key[16];password_key(id,key,sizeof(key));
    char pass[128]={0};
    nvs_handle_t nv;
    bool found=false;
    if(nvs_open(BETA_PASSWORD_NS,NVS_READONLY,&nv)==ESP_OK){
        size_t n=sizeof(pass);
        found=nvs_get_str(nv,key,pass,&n)==ESP_OK;
        nvs_close(nv);
    }
    if(!found){
        const cJSON *old=cJSON_GetObjectItemCaseSensitive(entry,"pass");
        if(cJSON_IsString(old)&&old->valuestring&&*old->valuestring)
            found=mira_legacy_decrypt(old->valuestring,pass,sizeof(pass));
    }
    cJSON_Delete(root);
    if(!found){
        ESP_LOGW(TAG,"No stored secret available for network id %d",id);
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t result=mira_recovery_save_beta_wifi(profile.ssid,pass,
        profile.ip_mode,profile.ip,profile.mask,
        profile.gateway,profile.dns1,profile.dns2);
    memset(pass,0,sizeof(pass));
    return result;
}
