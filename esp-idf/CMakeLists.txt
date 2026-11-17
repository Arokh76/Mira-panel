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

# JPEGDEC's S3 SIMD assembly is guarded by ARDUINO_ARCH_ESP32. Its ESP-IDF
# component is compiled separately from Arduino; otherwise the archive's
# s3_simd_420/444.S objects are empty while our JPEG core expects symbols.
idf_component_get_property(mira_jpegdec_lib bitbank2__jpegdec COMPONENT_LIB)
if(mira_jpegdec_lib)
  target_compile_definitions(${mira_jpegdec_lib} PRIVATE ARDUINO_ARCH_ESP32)
endif()
