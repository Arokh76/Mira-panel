#pragma once
// Experimental ESPAsyncWebServer bring-up; no production HTTP routes are
// redirected until protocol parity has been validated on the 7-inch hardware.
#ifdef __cplusplus
extern "C" {
#endif
void mira_async_web_start(void);
#ifdef __cplusplus
}
#endif
