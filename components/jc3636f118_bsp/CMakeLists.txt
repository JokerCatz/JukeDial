# SPDX-License-Identifier: LGPL-2.1-or-later
if(JC_FAKEPOD OR "$ENV{JC_FAKEPOD}")   # `make build-fakepod`: components/fakepod_bsp is the board support there
    idf_component_register()
else()
idf_component_register(
    SRCS "src/bsp_math.c" "src/bsp_i2c.c" "src/bsp_display.c" "src/bsp_touch.c" "src/bsp_knob.c" "src/bsp_knob_fsm.c" "src/bsp_button.c" "src/bsp_haptic.c" "src/bsp_audio.c" "src/bsp_mic.c" "src/bsp_sdcard.c" "src/bsp_imu.c" "src/bsp_buzzer.c" "src/bsp_battery.c" "src/bsp_board.c" "src/bsp_rtc.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES driver esp_timer esp_adc fatfs sdmmc esp_driver_sdmmc esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_driver_ledc esp_driver_spi esp_lcd lvgl__lvgl espressif__esp_lvgl_adapter espressif__esp_lcd_st77916 espressif__esp_lcd_touch espressif__esp_lcd_touch_cst816s player_core)
endif()
