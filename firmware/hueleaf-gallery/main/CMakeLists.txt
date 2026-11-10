idf_component_register(
    SRCS "main.c" "gallery.c" "frame.c" "proto.c" "runtime_state.c" "status_cache.c" "file_store.c" "fw_update.c" "fw_retire.c" "logbuf.c" "hl_cfg.c" "slideshow.c" "slideshow_policy.c" "usb_wake.c" "buttons.c" "imu/tap.c" "imu/tap_guard.c"
         "board/board.c" "pmic/axp2101.c" "rtc/pcf8563.c"
         "screen_driver/screen_driver.c" "screen_driver/mono_fast.c" "tf_card/tf_card.c"
    INCLUDE_DIRS "." "board" "pmic" "rtc" "imu" "screen_driver" "tf_card"
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_driver_i2c esp_driver_usb_serial_jtag esp_driver_uart
                  esp_partition esp_app_format esp_timer esp_hw_support nvs_flash fatfs sdmmc app_update bootloader_support esp_rom mbedtls bt esp_pm ulp)

# The LP external project inherits this component's public include paths.
# IDF 5.5.4 compiles UART helpers even for an I2C-only LP program.
idf_component_get_property(hl_uart_dir esp_driver_uart COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PUBLIC "${hl_uart_dir}/include")
ulp_embed_binary(ulp_usb_watch "lp_core/usb_watch.c" "usb_wake.c")

if(CONFIG_HL_REMOTE)
    target_sources(${COMPONENT_LIB} PRIVATE "remote.c" "remote_policy.c" "ble_remote.c")
endif()
