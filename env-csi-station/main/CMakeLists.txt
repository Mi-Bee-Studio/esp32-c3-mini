idf_component_register(SRCS "main.c" "app_aht20.c" "app_net.c" "app_csi.c" "app_pir.c" "soft_i2c.c" "ssd1306.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash lwip esp_http_server
                                     app_update esp_timer esp_driver_gpio esp_driver_i2c
                                     cjson esp_partition esp_system spi_flash)
