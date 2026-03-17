/*
 * Copyright (c) 2026 minxie <laba22@163.com>
 * All rights reserved.
 */

/*
 * Grove LCD RGB 命令处理模块
 *
 * 支持的 act:
 *   print   {"row":0, "text":"Hello"}         在指定行显示文字
 *   clear   {}                                清屏
 *   rgb     {"r":255, "g":0, "b":0}           设置背光颜色
 *   cursor  {"col":0, "row":0}                设置光标位置
 *   on      {}                                开启显示
 *   off     {}                                关闭显示
 */

#include <string.h>
#include "cmd_handler.h"
#include "grove_lcd.h"
#include "esp_log.h"

/* 外部声明的全局 LCD 句柄 (在 main.c 中定义) */
extern grove_lcd_handle_t g_lcd;

esp_err_t cmd_lcd_handler(const char *action, const cJSON *req, cJSON *resp)
{
    if (!g_lcd) {
        CMD_RESP_MSG(resp, "LCD 未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    /* print */
    if (strcasecmp(action, "print") == 0) {
        const cJSON *j_text = cJSON_GetObjectItem(req, "text");
        if (!cJSON_IsString(j_text) || !j_text->valuestring) {
            CMD_RESP_MSG(resp, "需要 text 参数");
            return ESP_ERR_INVALID_ARG;
        }
        const cJSON *j_row = cJSON_GetObjectItem(req, "row");
        int row = cJSON_IsNumber(j_row) ? j_row->valueint : 0;
        if (row < 0 || row > 1) {
            CMD_RESP_MSG(resp, "row 范围 0~1");
            return ESP_ERR_INVALID_ARG;
        }
        grove_lcd_set_cursor(g_lcd, 0, (uint8_t)row);
        /* 先清行 (写 16 个空格) */
        grove_lcd_print(g_lcd, "                ");
        grove_lcd_set_cursor(g_lcd, 0, (uint8_t)row);
        grove_lcd_print(g_lcd, j_text->valuestring);
        CMD_RESP_MSG(resp, "行%d: %.16s", row, j_text->valuestring);
        return ESP_OK;
    }

    /* clear */
    if (strcasecmp(action, "clear") == 0) {
        grove_lcd_clear(g_lcd);
        CMD_RESP_MSG(resp, "LCD 已清屏");
        return ESP_OK;
    }

    /* rgb */
    if (strcasecmp(action, "rgb") == 0) {
        const cJSON *j_r = cJSON_GetObjectItem(req, "r");
        const cJSON *j_g = cJSON_GetObjectItem(req, "g");
        const cJSON *j_b = cJSON_GetObjectItem(req, "b");
        if (!cJSON_IsNumber(j_r) || !cJSON_IsNumber(j_g) || !cJSON_IsNumber(j_b)) {
            CMD_RESP_MSG(resp, "需要 r, g, b 参数");
            return ESP_ERR_INVALID_ARG;
        }
        uint8_t r = (uint8_t)j_r->valueint;
        uint8_t g = (uint8_t)j_g->valueint;
        uint8_t b = (uint8_t)j_b->valueint;
        grove_lcd_set_rgb(g_lcd, r, g, b);
        CMD_RESP_MSG(resp, "背光 RGB(%d,%d,%d)", r, g, b);
        return ESP_OK;
    }

    /* cursor */
    if (strcasecmp(action, "cursor") == 0) {
        const cJSON *j_col = cJSON_GetObjectItem(req, "col");
        const cJSON *j_row = cJSON_GetObjectItem(req, "row");
        if (!cJSON_IsNumber(j_col) || !cJSON_IsNumber(j_row)) {
            CMD_RESP_MSG(resp, "需要 col, row 参数");
            return ESP_ERR_INVALID_ARG;
        }
        grove_lcd_set_cursor(g_lcd, (uint8_t)j_col->valueint, (uint8_t)j_row->valueint);
        CMD_RESP_MSG(resp, "光标 (%d,%d)", j_col->valueint, j_row->valueint);
        return ESP_OK;
    }

    /* on */
    if (strcasecmp(action, "on") == 0) {
        grove_lcd_display_on(g_lcd);
        grove_lcd_backlight_on(g_lcd);
        CMD_RESP_MSG(resp, "LCD 已开启");
        return ESP_OK;
    }

    /* off */
    if (strcasecmp(action, "off") == 0) {
        grove_lcd_display_off(g_lcd);
        grove_lcd_backlight_off(g_lcd);
        CMD_RESP_MSG(resp, "LCD 已关闭");
        return ESP_OK;
    }

    CMD_RESP_MSG(resp, "未知 act: %s", action);
    return ESP_ERR_NOT_FOUND;
}
