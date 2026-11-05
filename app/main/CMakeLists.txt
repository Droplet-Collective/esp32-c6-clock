if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/wifi_credentials.h")
    message(FATAL_ERROR
        "main/wifi_credentials.h not found.\n"
        "Copy main/wifi_credentials.example.h to main/wifi_credentials.h and set your Wi-Fi SSID/password "
        "(the file is git-ignored).")
endif()

idf_component_register(SRCS "main.cpp" "display.cpp" "button.cpp" "imu.cpp" "wifi.cpp" "time_sync.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc esp_driver_i2c
                                esp_wifi esp_netif esp_event nvs_flash
                                esp_http_client mbedtls esp_timer)
