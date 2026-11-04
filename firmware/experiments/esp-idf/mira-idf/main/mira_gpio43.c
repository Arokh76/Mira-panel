/* Mira Panel 4.3C native isolated GPIO; API parity with the WT32
 * controller where the hardware permits it. All physical access goes through
 * mira_display_io_* (the only owner of the CH32 output register).
 *
 * IMPORTANT: Waveshare's DI0/DI1 are isolated optocoupler inputs, not
 * generic ESP32 GPIO pins. They cannot provide INPUT_PULLUP/PULLDOWN.
 * The boot default for both outputs is LOW unless the user explicitly saves
 * a different policy; no Jeedom state update can change this configuration.
 */
#include "mira_gpio43.h"
#include "mira_display.h"
#include "mira_full.h"
#include "mira_jeedom.h"
#include "mira_mqtt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MIRA_LINKS 24
#define MIRA_INPUTS 2
#define MIRA_OUTPUTS 2
#define MIRA_DEBOUNCE_MS 35u
#define MIRA_MULTICLICK_MS 370u
#define MIRA_HOLD_MS 900u
static const char *TAG="mira-gpio43";
static SemaphoreHandle_t gpio_lock;
static mira_gpio43_output_t outputs[MIRA_OUTPUTS];
static mira_gpio43_link_t links[MIRA_LINKS];
static mira_gpio43_multi_t multi[MIRA_INPUTS];
static uint32_t revision=1;

typedef struct {
    bool started,raw,stable,long_sent;
    uint8_t count;
    uint32_t raw_changed,pressed,released;
} gesture_t;
static gesture_t gesture[MIRA_INPUTS];

static bool safe_name(const char *s,size_t max_len){
    if(!s||!*s||strlen(s)>max_len)return false;
    for(const unsigned char *p=(const unsigned char*)s;*p;p++)
        if(*p<32||*p==127)return false;
    return true;
}
static bool take(void){
    return gpio_lock && xSemaphoreTake(gpio_lock,pdMS_TO_TICKS(200))==pdTRUE;
}
static void drop(void){xSemaphoreGive(gpio_lock);}
static void key(char *out,size_t cap,const char *name,unsigned i){
    (void)snprintf(out,cap,"%s%u",name,i);
}
static int find_link(const char *name){
    for(unsigned i=0;i<MIRA_LINKS;i++)
        if(links[i].name[0]&&!strcmp(links[i].name,name))return (int)i;
    return -1;
}
static bool link_valid(const mira_gpio43_link_t *l){
    return safe_name(l->name,62)&&l->output<2&&l->kind<3;
}
static void load_settings(void){
    nvs_handle_t nv;
    if(nvs_open("mira43gpio",NVS_READONLY,&nv)==ESP_OK){
        for(unsigned i=0;i<MIRA_OUTPUTS;i++){
            char k[16];uint8_t val=0;
            key(k,sizeof(k),"mode",i);
            if(nvs_get_u8(nv,k,&val)==ESP_OK)outputs[i].enabled=val==1;
            key(k,sizeof(k),"inv",i);val=0;
            if(nvs_get_u8(nv,k,&val)==ESP_OK)outputs[i].inverted=val==1;
            key(k,sizeof(k),"boot",i);val=0;
            if(nvs_get_u8(nv,k,&val)==ESP_OK&&val<=2)outputs[i].boot=val;
            key(k,sizeof(k),"last",i);val=0;
            if(nvs_get_u8(nv,k,&val)==ESP_OK)outputs[i].logical_on=val==1;
        }
        nvs_close(nv);
    }
    if(nvs_open("mira43links",NVS_READONLY,&nv)==ESP_OK){
        for(unsigned i=0;i<MIRA_LINKS;i++){
            char k[16];size_t len=sizeof(links[i].name);
            key(k,sizeof(k),"name",i);
            if(nvs_get_str(nv,k,links[i].name,&len)!=ESP_OK)continue;
            uint8_t value=255;
            key(k,sizeof(k),"out",i);
            if(nvs_get_u8(nv,k,&value)==ESP_OK)links[i].output=value;
            value=255;
            key(k,sizeof(k),"kind",i);
            if(nvs_get_u8(nv,k,&value)==ESP_OK)links[i].kind=value;
            if(!link_valid(&links[i]))memset(&links[i],0,sizeof(links[i]));
        }
        nvs_close(nv);
    }
    if(nvs_open("mira43multi",NVS_READONLY,&nv)==ESP_OK){
        for(unsigned i=0;i<MIRA_INPUTS;i++){
            char k[16];size_t len=sizeof(multi[i].name);uint8_t visible=0;
            key(k,sizeof(k),"name",i);
            if(nvs_get_str(nv,k,multi[i].name,&len)!=ESP_OK||
               !safe_name(multi[i].name,45)){
                multi[i].name[0]=0;
            }
            key(k,sizeof(k),"vis",i);
            if(nvs_get_u8(nv,k,&visible)==ESP_OK)multi[i].visible=visible==1;
        }
        nvs_close(nv);
    }
}
static void dispatch_gesture(const char *name,unsigned kind){
    static const char *const prefix[]={
        "Multi_Clic_","Multi_Double_","Multi_Triple_","Multi_Long_"
    };
    if(!name||!*name||kind>=4)return;
    char action[96];
    const int n=snprintf(action,sizeof(action),"%s%s",prefix[kind],name);
    if(n<0||n>=(int)sizeof(action))return;
    bool queued;
    if(mira_jeedom_actions_enabled())
        queued=mira_jeedom_queue_local_action(action,"button","1");
    else
        queued=mira_mqtt_publish_action(action,"1")||
               mira_generic_queue_local_action(action,"button","1");
    if(!queued)ESP_LOGW(TAG,"Multi-gesture %s not dispatched: transport unavailable",action);
}
/* Poll separately from LVGL: never block rendering to debounce contacts.
 * All external commands leave the poll task as queued Jeedom/MQTT events.
 */
static void poll_inputs(void *unused){
    (void)unused;
    for(;;){
        bool di0=0,di1=0,do0=0,do1=0;
        if(mira_display_io_read(&di0,&di1,&do0,&do1)==ESP_OK&&take()){
            const uint32_t now=(uint32_t)(esp_timer_get_time()/1000u);
            const bool inputs[]={di0,di1};
            char fire_name[2][48]={{0}};
            int fire_kind[2]={-1,-1};
            for(unsigned i=0;i<MIRA_INPUTS;i++){
                if(!multi[i].name[0]){
                    gesture[i].started=false;
                    continue;
                }
                /* Original beta uses active LOW dry-contact gesture events.
                 * Already pressed on boot/configure is NOT a new press.
                 */
                const bool down=!inputs[i];
                gesture_t *g=&gesture[i];
                if(!g->started){
                    g->started=true;g->raw=down;g->stable=down;
                    g->raw_changed=now;g->pressed=now;
                    g->count=0;g->long_sent=down;
                    continue;
                }
                if(down!=g->raw){g->raw=down;g->raw_changed=now;}
                if(down!=g->stable && now-g->raw_changed>=MIRA_DEBOUNCE_MS){
                    g->stable=down;
                    if(down){g->pressed=now;g->long_sent=false;}
                    else if(!g->long_sent){
                        if(g->count<3)g->count++;
                        g->released=now;
                    }else g->count=0;
                }
                if(g->stable&&!g->long_sent&&now-g->pressed>=MIRA_HOLD_MS){
                    g->long_sent=true;g->count=0;
                    fire_kind[i]=3;
                }else if(!g->stable&&g->count&&
                         now-g->released>=MIRA_MULTICLICK_MS){
                    fire_kind[i]=g->count-1;g->count=0;
                }
                if(fire_kind[i]>=0){
                    memcpy(fire_name[i],multi[i].name,sizeof(fire_name[i]));
                    fire_name[i][sizeof(fire_name[i])-1]=0;
                }
            }
            drop();
            for(unsigned i=0;i<MIRA_INPUTS;i++)
                if(fire_kind[i]>=0)
                    dispatch_gesture(fire_name[i],(unsigned)fire_kind[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
esp_err_t mira_gpio43_init(void){
    if(gpio_lock)return ESP_OK;
    gpio_lock=xSemaphoreCreateMutex();
    if(!gpio_lock)return ESP_ERR_NO_MEM;
    load_settings();
    for(unsigned i=0;i<MIRA_OUTPUTS;i++){
        mira_gpio43_output_t *o=&outputs[i];
        bool logical=false;
        if(o->enabled){
            logical=o->boot==2||(o->boot==1&&o->logical_on);
        }
        o->logical_on=logical;
        const bool physical=o->enabled?(logical!=o->inverted):false;
        esp_err_t err=mira_display_io_write(i,physical);
        if(err!=ESP_OK){
            ESP_LOGE(TAG,"Unable to set DO%u safe boot state: %s",
                     i,esp_err_to_name(err));
            return err;
        }
    }
    if(xTaskCreate(poll_inputs,"mira43_io",4096,NULL,2,NULL)!=pdPASS){
        ESP_LOGW(TAG,"GPIO multiaction polling unavailable");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,"2 isolated DI + 2 DO initialized, no WT32 pin mapping");
    return ESP_OK;
}
esp_err_t mira_gpio43_state(mira_gpio43_state_t *out){
    if(!out)return ESP_ERR_INVALID_ARG;
    if(!take())return ESP_ERR_INVALID_STATE;
    bool di0=0,di1=0,do0=0,do1=0;
    esp_err_t err=mira_display_io_read(&di0,&di1,&do0,&do1);
    if(err==ESP_OK){
        out->input[0]=di0;out->input[1]=di1;
        out->output[0]=do0;out->output[1]=do1;
        for(unsigned i=0;i<MIRA_OUTPUTS;i++)out->config[i]=outputs[i];
        out->revision=revision;
    }
    drop();
    return err;
}
static void notify_links(unsigned output,bool logical,bool physical){
    char names[MIRA_LINKS][64]={{0}};
    unsigned count=0;
    if(!take())return;
    for(unsigned j=0;j<MIRA_LINKS;j++){
        if(links[j].name[0]&&links[j].output==output&&links[j].kind==2){
            memcpy(names[count],links[j].name,sizeof(names[count]));
            names[count][sizeof(names[count])-1]=0;
            count++;
        }
    }
    drop();
    for(unsigned j=0;j<count;j++)
        (void)mira_full_enqueue_state(names[j],physical?"1":"0");
    (void)logical;
}
esp_err_t mira_gpio43_write(unsigned output,bool logical_on){
    if(output>=2)return ESP_ERR_INVALID_ARG;
    if(!take())return ESP_ERR_INVALID_STATE;
    mira_gpio43_output_t *o=&outputs[output];
    if(!o->enabled){drop();return ESP_ERR_INVALID_STATE;}
    const bool physical=logical_on!=o->inverted;
    const esp_err_t err=mira_display_io_write(output,physical);
    if(err==ESP_OK){
        if(o->logical_on!=logical_on){
            o->logical_on=logical_on;
            revision++;
            if(o->boot==1){
                nvs_handle_t nv;
                if(nvs_open("mira43gpio",NVS_READWRITE,&nv)==ESP_OK){
                    char k[16];key(k,sizeof(k),"last",output);
                    if(nvs_set_u8(nv,k,logical_on?1:0)!=ESP_OK||
                       nvs_commit(nv)!=ESP_OK)
                        ESP_LOGW(TAG,"DO%u restore-state NVS write failed",output);
                    nvs_close(nv);
                }
            }
        }
    }
    drop();
    if(err==ESP_OK)notify_links(output,logical_on,physical);
    return err;
}
esp_err_t mira_gpio43_write_physical(unsigned output,bool high){
    if(output>=2)return ESP_ERR_INVALID_ARG;
    if(!take())return ESP_ERR_INVALID_STATE;
    const bool inverted=outputs[output].inverted;
    drop();
    return mira_gpio43_write(output,high!=inverted);
}
esp_err_t mira_gpio43_configure(unsigned output,bool enabled,
                                bool inverted,uint8_t boot){
    if(output>=2||boot>2)return ESP_ERR_INVALID_ARG;
    if(!take())return ESP_ERR_INVALID_STATE;
    mira_gpio43_output_t previous=outputs[output];
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira43gpio",NVS_READWRITE,&nv);
    if(err!=ESP_OK){drop();return err;}
    char k[16];key(k,sizeof(k),"mode",output);
    err=nvs_set_u8(nv,k,enabled?1:0);
    if(err==ESP_OK){
        key(k,sizeof(k),"inv",output);
        err=nvs_set_u8(nv,k,inverted?1:0);
    }
    if(err==ESP_OK){
        key(k,sizeof(k),"boot",output);
        err=nvs_set_u8(nv,k,boot);
    }
    if(err==ESP_OK&&boot==1){
        key(k,sizeof(k),"last",output);
        err=nvs_set_u8(nv,k,previous.enabled&&previous.logical_on?1:0);
    }
    if(err==ESP_OK)err=nvs_commit(nv);
    nvs_close(nv);
    if(err!=ESP_OK){drop();return err;}
    const bool changed=previous.enabled!=enabled||previous.inverted!=inverted;
    const bool logical=enabled&&!changed&&previous.logical_on;
    const bool physical=enabled?(logical!=inverted):false;
    err=mira_display_io_write(output,physical);
    if(err==ESP_OK){
        outputs[output].enabled=enabled;
        outputs[output].inverted=inverted;
        outputs[output].boot=boot;
        outputs[output].logical_on=logical;
        revision++;
    }
    drop();
    if(err==ESP_OK)notify_links(output,logical,physical);
    return err;
}
esp_err_t mira_gpio43_link(const char *name,int output,uint8_t kind){
    if(!safe_name(name,62)||output>1||output< -1||kind>2)
        return ESP_ERR_INVALID_ARG;
    if(!take())return ESP_ERR_INVALID_STATE;
    if(output>=0&&!outputs[output].enabled){drop();return ESP_ERR_INVALID_STATE;}
    int slot=find_link(name);
    if(slot<0){
        if(output<0){drop();return ESP_OK;}
        for(unsigned i=0;i<MIRA_LINKS;i++)
            if(!links[i].name[0]){slot=(int)i;break;}
    }
    if(slot<0){drop();return ESP_ERR_NO_MEM;}
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira43links",NVS_READWRITE,&nv);
    if(err!=ESP_OK){drop();return err;}
    char k[16];key(k,sizeof(k),"name",(unsigned)slot);
    if(output<0){
        err=nvs_erase_key(nv,k);
        if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
    }else err=nvs_set_str(nv,k,name);
    if(err==ESP_OK){
        key(k,sizeof(k),"out",(unsigned)slot);
        err=output<0?nvs_erase_key(nv,k):
            nvs_set_u8(nv,k,(uint8_t)output);
        if(err==ESP_ERR_NVS_NOT_FOUND&&output<0)err=ESP_OK;
    }
    if(err==ESP_OK){
        key(k,sizeof(k),"kind",(unsigned)slot);
        err=output<0?nvs_erase_key(nv,k):nvs_set_u8(nv,k,kind);
        if(err==ESP_ERR_NVS_NOT_FOUND&&output<0)err=ESP_OK;
    }
    if(err==ESP_OK)err=nvs_commit(nv);
    nvs_close(nv);
    if(err==ESP_OK){
        memset(&links[slot],0,sizeof(links[slot]));
        if(output>=0){
            snprintf(links[slot].name,sizeof(links[slot].name),"%s",name);
            links[slot].output=(uint8_t)output;
            links[slot].kind=kind;
        }
        revision++;
    }
    drop();
    return err;
}
bool mira_gpio43_action_mapped(const char *name){
    if(!name||!take())return false;
    const bool mapped=find_link(name)>=0;
    drop();return mapped;
}
bool mira_gpio43_execute_action(const char *name){
    if(!name||!take())return false;
    const int slot=find_link(name);
    const int output=slot>=0&&links[slot].kind<2?links[slot].output:-1;
    const bool high=slot>=0&&links[slot].kind==1;
    drop();
    return output>=0 && mira_gpio43_write_physical((unsigned)output,high)==ESP_OK;
}
static bool toggle_link(const char *name,int *output,bool *on_high){
    char on[96],off[96];
    if(!name||strlen(name)>62)return false;
    snprintf(on,sizeof(on),"On_%s",name);
    snprintf(off,sizeof(off),"Off_%s",name);
    const int i=find_link(on),j=find_link(off);
    if(i<0||j<0||links[i].output!=links[j].output||
       links[i].kind>=2||links[j].kind>=2||
       links[i].kind==links[j].kind)return false;
    *output=links[i].output;
    *on_high=links[i].kind==1;
    return true;
}
bool mira_gpio43_toggle_mapped(const char *name){
    if(!take())return false;
    char id[96];int output=0;bool level=false;
    const bool configured=toggle_link(name,&output,&level);
    (void)id;
    drop();return configured;
}
bool mira_gpio43_execute_toggle(const char *name,bool on){
    if(!take())return false;
    int output=0;bool high=false;
    const bool valid=toggle_link(name,&output,&high);
    drop();
    return valid && mira_gpio43_write_physical((unsigned)output,on?high:!high)==ESP_OK;
}
bool mira_gpio43_toggle_state(const char *name,bool *on){
    if(!on||!take())return false;
    int output=0;bool high=false;
    const bool valid=toggle_link(name,&output,&high);
    if(valid){
        const mira_gpio43_output_t *o=&outputs[output];
        *on=(o->logical_on!=o->inverted)==high;
    }
    drop();
    return valid;
}
esp_err_t mira_gpio43_multi(const char *name,int input,bool visible){
    if(!safe_name(name,45)||input>1||input< -1)return ESP_ERR_INVALID_ARG;
    if(!take())return ESP_ERR_INVALID_STATE;
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira43multi",NVS_READWRITE,&nv);
    if(err!=ESP_OK){drop();return err;}
    for(unsigned i=0;i<2&&err==ESP_OK;i++){
        if((int)i==input){
            char k[16];key(k,sizeof(k),"name",i);
            err=nvs_set_str(nv,k,name);
            if(err==ESP_OK){
                key(k,sizeof(k),"vis",i);
                err=nvs_set_u8(nv,k,visible?1:0);
            }
        }else if(!strcmp(multi[i].name,name)){
            char k[16];key(k,sizeof(k),"name",i);
            err=nvs_erase_key(nv,k);
            if(err==ESP_ERR_NVS_NOT_FOUND)err=ESP_OK;
        }
    }
    if(err==ESP_OK)err=nvs_commit(nv);
    nvs_close(nv);
    if(err==ESP_OK){
        for(unsigned i=0;i<2;i++){
            if((int)i==input){
                snprintf(multi[i].name,sizeof(multi[i].name),"%s",name);
                multi[i].visible=visible;
                memset(&gesture[i],0,sizeof(gesture[i]));
            }else if(!strcmp(multi[i].name,name)){
                multi[i].name[0]=0;multi[i].visible=false;
                memset(&gesture[i],0,sizeof(gesture[i]));
            }
        }
        revision++;
    }
    drop();return err;
}
bool mira_gpio43_multi_visible(const char *name){
    if(!name||!take())return false;
    bool visible=false;
    for(unsigned i=0;i<2;i++)
        if(multi[i].name[0]&&!strcmp(multi[i].name,name))
            visible=multi[i].visible;
    drop();return visible;
}
void mira_gpio43_describe(mira_gpio43_link_t out_links[24],
                          mira_gpio43_multi_t out_inputs[2]){
    if(!out_links||!out_inputs)return;
    memset(out_links,0,sizeof(links));memset(out_inputs,0,sizeof(multi));
    if(!take())return;
    memcpy(out_links,links,sizeof(links));
    memcpy(out_inputs,multi,sizeof(multi));
    drop();
}
