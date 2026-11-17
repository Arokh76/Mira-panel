#pragma once
// Experimental ESPAsyncWebServer bring-up; no production HTTP routes are
// redirected until protocol parity has been validated on the 7-inch hardware.
#ifdef __cplusplus
extern "C" {
#endif
void mira_async_web_start(void);
// Direct passive Jeedom changes are published to WT32-compatible EventSource.
void mira_async_web_emit_direct(int id,const char *value);
#ifdef __cplusplus
}
#endif
