cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(curtain_announce)

# Register data directory for SPIFFS partition
spiffs_create_partition_image(storage data FLASH_IN_PROJECT)
