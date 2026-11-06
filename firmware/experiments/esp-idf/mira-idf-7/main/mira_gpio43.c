/* Waveshare 7 STD: compatibility shim only. The 4.3C CH32 isolated
 * DI/DO outputs do not exist on the validated 7-inch CH422G profile.
 * Never drive CH422G panel/reset/power pins from generic GPIO actions. */
#include "mira_gpio43.h"
#include <string.h>

esp_err_t mira_gpio43_init(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t mira_gpio43_state(mira_gpio43_state_t *out) {
    if (out) memset(out, 0, sizeof(*out));
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t mira_gpio43_configure(unsigned output, bool enabled, bool inverted, uint8_t boot) {
    (void)output; (void)enabled; (void)inverted; (void)boot;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t mira_gpio43_write(unsigned output, bool logical_on) {
    (void)output; (void)logical_on; return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t mira_gpio43_write_physical(unsigned output, bool high) {
    (void)output; (void)high; return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t mira_gpio43_link(const char *name, int output, uint8_t kind) {
    (void)name; (void)output; (void)kind; return ESP_ERR_NOT_SUPPORTED;
}
bool mira_gpio43_action_mapped(const char *name) { (void)name; return false; }
bool mira_gpio43_execute_action(const char *name) { (void)name; return false; }
bool mira_gpio43_toggle_mapped(const char *name) { (void)name; return false; }
bool mira_gpio43_execute_toggle(const char *name, bool on) {
    (void)name; (void)on; return false;
}
bool mira_gpio43_toggle_state(const char *name, bool *on) {
    (void)name; if (on) *on=false; return false;
}
esp_err_t mira_gpio43_multi(const char *name, int input, bool visible) {
    (void)name; (void)input; (void)visible; return ESP_ERR_NOT_SUPPORTED;
}
bool mira_gpio43_multi_visible(const char *name) { (void)name; return false; }
void mira_gpio43_describe(mira_gpio43_link_t links[24], mira_gpio43_multi_t inputs[2]) {
    if (links) memset(links, 0, sizeof(mira_gpio43_link_t)*24);
    if (inputs) memset(inputs, 0, sizeof(mira_gpio43_multi_t)*2);
}
