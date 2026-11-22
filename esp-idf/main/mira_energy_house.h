#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

// An isolated 800x480 LVGL preview of Mira's energy house.
// All functions execute on Mira's existing LVGL task. No network/OTA changes.
lv_obj_t *mira_energy_house_create(lv_obj_t *screen, void (*back_to_home)(void));
void mira_energy_house_destroy(void);
void mira_energy_house_show(bool show);
bool mira_energy_house_exists(void);
lv_obj_t *mira_energy_house_field(unsigned field); // existing solar/home/grid/battery/soc1..3
void mira_energy_house_update(unsigned field, const char *value);
void mira_energy_house_aux(const char *name, const char *value);
void mira_energy_house_options(bool solar, bool battery, bool ev, bool ecs);
