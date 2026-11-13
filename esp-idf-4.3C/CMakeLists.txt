cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/lvgl")
# The project descriptor version MUST match firmware/targets.json for
# authenticated automatic OTA. Bump this on official native-only releases.
set(PROJECT_VER "0.7.37")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mira_idf_scroll)
