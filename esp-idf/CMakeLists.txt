cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/lvgl")
# The project descriptor version MUST match firmware/targets.json for
# authenticated automatic OTA. Bump this on official native-only releases.
set(PROJECT_VER "0.7.37")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mira_idf_7std)

# Arduino-ESP32 dependency pulls esp-modbus 2.1.4; GCC 14 diagnoses a
# third-party strncpy pattern as stringop-truncation. Disable just this
# -Werror category for that component, preserve warnings elsewhere.
idf_component_get_property(mira_modbus_lib espressif__esp-modbus COMPONENT_LIB)
if(mira_modbus_lib)
  target_compile_options(${mira_modbus_lib} PRIVATE -Wno-error=stringop-truncation)
endif()

# Only AsyncTCP's persistent event task gets an 8KB PSRAM stack. This does
# not alter the Wi-Fi, RGB, HTTPD or Arduino core task allocation policies.
idf_component_get_property(mira_asynctcp_lib esp32async__asynctcp COMPONENT_LIB)
if(mira_asynctcp_lib)
  target_compile_options(${mira_asynctcp_lib} PRIVATE
    "-include" "${CMAKE_CURRENT_LIST_DIR}/main/mira_asynctcp_psram.h")
endif()
