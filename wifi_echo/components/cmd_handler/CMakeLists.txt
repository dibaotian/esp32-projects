idf_component_register(SRCS "cmd_handler.c" "cmd_buzzer.c" "cmd_servo.c" "cmd_display.c" "cmd_lcd.c"
                    INCLUDE_DIRS "include"
                    REQUIRES json buzzer servo tm1637 grove_lcd)
