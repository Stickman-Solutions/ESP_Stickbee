# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/esp-idf-hdc1080
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/esp-idf-opt3001
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESP_Stickbee)