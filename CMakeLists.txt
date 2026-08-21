cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "0.1.0")
project(magicusb)

# esp_tinyusb 2.x bundles callbacks for whole-device SPI-flash/SD storage.
# MagicUSB needs custom read-only callbacks backed by one selected image, so
# retain Espressif's driver/descriptors/task but exclude its MSC backend.
idf_component_get_property(magicusb_tinyusb_lib espressif__esp_tinyusb COMPONENT_LIB)
get_target_property(magicusb_tinyusb_sources ${magicusb_tinyusb_lib} SOURCES)
list(FILTER magicusb_tinyusb_sources EXCLUDE REGEX "tinyusb_msc\\.c$")
set_property(TARGET ${magicusb_tinyusb_lib} PROPERTY SOURCES "${magicusb_tinyusb_sources}")
