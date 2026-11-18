#pragma once
// Build-only adaptation scoped strictly to esp32async__asynctcp.
// Waveshare RGB+GT911+native compatibility HTTP reserves fragmented INTERNAL
// DRAM before AsyncTCP starts. The stock AsyncTCP 8KB task stack cannot be
// allocated then, although 8MB octal PSRAM remains free.
//
// Include FreeRTOS declarations BEFORE the below macros so the original
// xTaskCreate signatures are not rewritten in SDK headers.
// AsyncTCP.cpp is the only compilation unit receiving this -include file.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"

static inline BaseType_t mira_async_stack_pinned(
    TaskFunction_t fn,const char *name,uint32_t bytes,void *parameter,
    UBaseType_t priority,TaskHandle_t *task,BaseType_t core){
    return xTaskCreatePinnedToCoreWithCaps(fn,name,bytes,parameter,priority,
                                           task,core,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
static inline BaseType_t mira_async_stack_any(
    TaskFunction_t fn,const char *name,uint32_t bytes,void *parameter,
    UBaseType_t priority,TaskHandle_t *task){
    return xTaskCreateWithCaps(fn,name,bytes,parameter,priority,
                               task,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
#define xTaskCreatePinnedToCore(fn,name,bytes,param,prio,handle,core) \
    mira_async_stack_pinned((fn),(name),(bytes),(param),(prio),(handle),(core))
#define xTaskCreate(fn,name,bytes,param,prio,handle) \
    mira_async_stack_any((fn),(name),(bytes),(param),(prio),(handle))
