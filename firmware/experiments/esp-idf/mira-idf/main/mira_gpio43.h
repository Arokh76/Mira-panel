#pragma once
/* Waveshare ESP32-S3-Touch-LCD-4.3C: 2 isolated digital inputs (DI0/DI1)
 * and 2 isolated digital outputs (DO0/DO1), via CH32 I2C expander @0x24.
 * Unlike the WT32 EXT, these are NOT six generic ESP GPIOs.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool enabled;
    bool inverted;
    uint8_t boot; /* 0=off, 1=restore last, 2=on; explicit user choice */
    bool logical_on;
} mira_gpio43_output_t;
typedef struct {
    bool input[2];
    bool output[2];
    mira_gpio43_output_t config[2];
    uint32_t revision;
} mira_gpio43_state_t;

esp_err_t mira_gpio43_init(void);
esp_err_t mira_gpio43_state(mira_gpio43_state_t *out);
esp_err_t mira_gpio43_configure(unsigned output, bool enabled, bool inverted, uint8_t boot);
esp_err_t mira_gpio43_write(unsigned output, bool logical_on);
esp_err_t mira_gpio43_write_physical(unsigned output, bool high);

/* Optional named local output actions from beta, including On_/Off_ pairs.
 * Local hardware commands must NOT also be sent over Jeedom/MQTT.
 */
esp_err_t mira_gpio43_link(const char *name, int output, uint8_t kind);
bool mira_gpio43_action_mapped(const char *name);
bool mira_gpio43_execute_action(const char *name);
bool mira_gpio43_toggle_mapped(const char *name);
bool mira_gpio43_execute_toggle(const char *name, bool on);
bool mira_gpio43_toggle_state(const char *name, bool *on);

/* Input dry-contact gestures, active-low as in MiraWT32Gpio.h.
 * input = -1 unbinds. Never count an input already pressed at startup.
 */
esp_err_t mira_gpio43_multi(const char *name, int input, bool visible);
bool mira_gpio43_multi_visible(const char *name);

typedef struct {
    char name[64];
    uint8_t output,kind;
} mira_gpio43_link_t;
typedef struct {
    char name[48];
    bool visible;
} mira_gpio43_multi_t;
void mira_gpio43_describe(mira_gpio43_link_t links[24],
                          mira_gpio43_multi_t inputs[2]);
