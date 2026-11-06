// Native ESP-IDF equivalent of the beta Arduino DNSServer wildcard.
// The original setup AP redirects DNS queries to the local Mira web server.
#include "mira_beta_captive_dns.h"
#include "mira_beta_setup.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG="mira-beta-dns";
static void dns_task(void *arg){
    (void)arg;
    int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(fd<0){ESP_LOGE(TAG,"DNS captive socket unavailable");vTaskDelete(NULL);return;}
    struct sockaddr_in addr={0};
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    addr.sin_port=htons(53);
    struct timeval timeout={.tv_sec=1,.tv_usec=0};
    (void)setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    if(bind(fd,(struct sockaddr*)&addr,sizeof(addr))<0){
        ESP_LOGW(TAG,"DNS captive port 53 unavailable");
        close(fd);vTaskDelete(NULL);return;
    }
    ESP_LOGI(TAG,"Mira Panel captive DNS active on first-run AP");
    for(;;){
        unsigned char buf[512]={0};
        struct sockaddr_in client={0};
        socklen_t client_len=sizeof(client);
        int n=recvfrom(fd,buf,sizeof(buf),0,(struct sockaddr*)&client,&client_len);
        if(n<17 || !mira_beta_setup_unconfigured())continue;
        // Never respond to LAN/STA requests as a public DNS resolver.
        if((ntohl(client.sin_addr.s_addr)&0xffffff00u)!=0xc0a80400u)continue;
        // Standard one-question A/IN queries only; don't parse pointers in
        // question labels, which could otherwise create compression loops.
        if(buf[2]&0x80 || buf[4]!=0 || buf[5]!=1)continue;
        int offset=12;
        bool valid=false;
        while(offset<n){
            unsigned length=buf[offset++];
            if(!length){valid=true;break;}
            if(length>63||offset+(int)length>=n){valid=false;break;}
            offset+=(int)length;
        }
        if(!valid || offset+4>n || buf[offset] || buf[offset+1]!=1 ||
           buf[offset+2] || buf[offset+3]!=1)continue;
        offset+=4;
        if(offset+16>(int)sizeof(buf))continue;
        buf[2]=0x81;buf[3]=0x80;
        buf[4]=0;buf[5]=1;buf[6]=0;buf[7]=1;
        for(int j=8;j<12;j++)buf[j]=0;
        const uint8_t answer[16]={
            0xc0,0x0c, 0,1, 0,1, 0,0,0,30, 0,4, 192,168,4,1
        };
        memcpy(buf+offset,answer,sizeof(answer));
        (void)sendto(fd,buf,(size_t)offset+sizeof(answer),0,
                     (struct sockaddr*)&client,client_len);
    }
}
esp_err_t mira_beta_captive_dns_start(void){
    if(xTaskCreate(dns_task,"mira_dns",4096,NULL,3,NULL)!=pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
