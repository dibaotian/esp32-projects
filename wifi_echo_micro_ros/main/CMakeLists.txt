# Copyright (c) 2026 minxie <laba22@163.com>
# All rights reserved.

idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES buzzer servo tm1637 grove_lcd
                             micro_ros_espidf_component
                             nvs_flash esp_event)
