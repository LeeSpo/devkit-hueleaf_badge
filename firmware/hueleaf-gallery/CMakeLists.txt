cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(DEFINED HL_REMOTE_BUILD AND HL_REMOTE_BUILD)
    project(hueleaf_remote_v1)
else()
    project(hueleaf_gallery)
endif()

# IDF 5.5.4 LP-core support includes UART wake helpers but omits their
# component dependency. Keep the compatibility fix in this project, not SDK.
if(CONFIG_ULP_COPROC_TYPE_LP_CORE)
    idf_component_get_property(hl_ulp_lib ulp COMPONENT_LIB)
    target_link_libraries(${hl_ulp_lib} PRIVATE idf::esp_driver_uart)
endif()

# ---- Stock-firmware protection --------------------------------------------
# ESP-IDF flashes the app to the "boot default" partition, which is `factory`
# (0x10000) = the STOCK app we must keep. Re-point every flash target that
# carries the app to ota_0, and replace the blank otadata (which would boot
# factory) with one that selects ota_0. Configure fails if 0x10000 survives.
partition_table_get_partition_info(HL_FACTORY_OFF "--partition-type app --partition-subtype factory" "offset")
partition_table_get_partition_info(HL_OTA0_OFF "--partition-name ota_0" "offset")
if((NOT CONFIG_HL_REMOTE AND NOT HL_FACTORY_OFF) OR NOT HL_OTA0_OFF)
    message(FATAL_ERROR "HueLeaf: partition table must contain factory and ota_0")
endif()
set(HL_OTADATA "${CMAKE_BINARY_DIR}/otadata_ota0.bin")
idf_build_get_property(HL_PYTHON PYTHON)
add_custom_command(OUTPUT "${HL_OTADATA}"
    COMMAND ${HL_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/tools/mk_otadata.py" "${HL_OTADATA}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/mk_otadata.py" VERBATIM)
add_custom_target(hl_otadata ALL DEPENDS "${HL_OTADATA}")
foreach(tgt flash app-flash otadata-flash)
    if(NOT TARGET ${tgt})
        continue()
    endif()
    add_dependencies(${tgt} hl_otadata)
    foreach(prop FLASH_FILE FLASH_ENTRY IMAGES)
        get_target_property(val ${tgt} ${prop})
        if(NOT val)
            continue()
        endif()
        if(HL_FACTORY_OFF)
        string(REPLACE "\"${HL_FACTORY_OFF}\"" "\"${HL_OTA0_OFF}\"" val "${val}")
        string(REGEX REPLACE "(^|;)${HL_FACTORY_OFF} " "\\1${HL_OTA0_OFF} " val "${val}")
        endif()
        string(REPLACE "ota_data_initial.bin" "otadata_ota0.bin" val "${val}")
        if(NOT CONFIG_HL_REMOTE AND val MATCHES "(\"|^|;)${HL_FACTORY_OFF}[\" ]")
            message(FATAL_ERROR "HueLeaf: ${tgt}.${prop} still targets factory ${HL_FACTORY_OFF}")
        endif()
        set_property(TARGET ${tgt} PROPERTY ${prop} "${val}")
    endforeach()
endforeach()
message(STATUS "HueLeaf: flash/app-flash -> ota_0 @ ${HL_OTA0_OFF}; factory ${HL_FACTORY_OFF} (stock) is never written")

if(CONFIG_HL_REMOTE)
    if(NOT HL_OTA0_OFF STREQUAL "0x20000" OR HL_FACTORY_OFF)
        message(FATAL_ERROR "remote-v1 requires partitions.remote.csv")
    endif()
endif()
