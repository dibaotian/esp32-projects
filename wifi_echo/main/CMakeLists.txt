idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES cmd_handler buzzer servo tm1637 grove_lcd nvs_flash esp_wifi esp_netif lwip esp_event)
