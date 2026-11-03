idf_component_register(
    SRCS "main.c" "gallery.c" "frame.c" "proto.c" "fw_update.c" "logbuf.c" "buttons.c"
         "board/board.c" "pmic/axp2101.c" "rtc/pcf8563.c"
         "screen_driver/screen_driver.c" "tf_card/tf_card.c"
    INCLUDE_DIRS "." "board" "pmic" "rtc" "screen_driver" "tf_card"
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_driver_i2c esp_driver_usb_serial_jtag
                  esp_partition esp_app_format esp_timer nvs_flash fatfs sdmmc app_update bootloader_support esp_rom)
