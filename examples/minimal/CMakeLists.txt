cmake_minimum_required(VERSION 3.16)

# This example assumes you've cloned esp32-lib-webui as a sibling
# directory. Adjust EXTRA_COMPONENT_DIRS if your layout differs.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(webui-minimal)
