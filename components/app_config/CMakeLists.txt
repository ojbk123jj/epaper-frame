set(LOCAL_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/../../config")
if(NOT EXISTS "${LOCAL_CONFIG_DIR}/local_config.h")
    configure_file("${LOCAL_CONFIG_DIR}/local_config.example.h"
                   "${LOCAL_CONFIG_DIR}/local_config.h" COPYONLY)
endif()

idf_component_register(INCLUDE_DIRS "include" "${LOCAL_CONFIG_DIR}")
