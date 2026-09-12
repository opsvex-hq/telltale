cmake_minimum_required(VERSION 3.16)

# Consume telltale straight from this repository. In your own project you would
# instead add it to idf_component.yml and let the component manager fetch it.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(telltale_basic)
