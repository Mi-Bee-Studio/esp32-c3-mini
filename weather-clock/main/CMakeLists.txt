idf_component_register(SRCS "main.c" "app_web.c" "st7735.c" "font5x7.c" "aht30.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash lwip
                                     esp_http_server app_update esp_timer
                                     esp_driver_gpio esp_driver_spi esp_driver_i2c
                                     esp_lcd esp_hw_support cjson)
