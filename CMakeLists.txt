# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Use custom partition table
set(PARTITION_CSV_PATH "${CMAKE_SOURCE_DIR}/partitions.csv")

# Configure esp-matter paths (must be set before project.cmake)
if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(WARNING "ESP_MATTER_PATH not set - Matter support disabled")
else()
    # Set paths that esp_matter component expects
    set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
    set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

    # Set device path if not already defined (use hollow for minimal device HAL)
    if(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})
        set(ENV{ESP_MATTER_DEVICE_PATH} ${ESP_MATTER_PATH}/device_hal/device/hollow)
    endif()

    set(EXTRA_COMPONENT_DIRS
        "${ESP_MATTER_PATH}/components"
        "${MATTER_SDK_PATH}/config/esp32/components"
        "${ESP_MATTER_PATH}/device_hal/device"
        "${ESP_MATTER_PATH}/device_hal/led_driver"
        "${ESP_MATTER_PATH}/device_hal/button_driver/button"
    )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gale)

# Add compile options for Matter after project() is called
if(DEFINED ESP_MATTER_PATH)
    idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
    idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
    idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
endif()
