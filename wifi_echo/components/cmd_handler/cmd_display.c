/*
 * Copyright (c) 2026 minxie <laba22@163.com>
 * All rights reserved.
 */

/*
 * 数码管命令处理模块
 *
 * 支持的 act:
 *   number  {"value": 0~9999, "leading_zero": bool}   显示数字
 *   text    {"value": "1234"}                          显示字符串 (0-9,A-F,-,空格)
 *   raw     {"segs": [0x3F,0x06,0x5B,0x4F]}           直接段码
 *   clear   {}                                         清空
 *   bright  {"value": 0~7}                             设置亮度
 *   colon   {"on": true/false}                         冒号开关
 */

#include <string.h>
#include "cmd_handler.h"
#include "tm1637.h"
#include "esp_log.h"

/* 外部声明的全局数码管句柄 (在 main.c 中定义) */
extern tm1637_handle_t g_tm1637;

esp_err_t cmd_display_handler(const char *action, const cJSON *req, cJSON *resp)
{
    if (!g_tm1637) {
        CMD_RESP_MSG(resp, "数码管未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    /* number */
    if (strcasecmp(action, "number") == 0) {
        const cJSON *j = cJSON_GetObjectItem(req, "value");
        if (!cJSON_IsNumber(j)) {
            CMD_RESP_MSG(resp, "需要 value 参数");
            return ESP_ERR_INVALID_ARG;
        }
        int num = j->valueint;
        if (num < 0 || num > 9999) {
            CMD_RESP_MSG(resp, "value 范围 0~9999");
            return ESP_ERR_INVALID_ARG;
        }
        const cJSON *j_lz = cJSON_GetObjectItem(req, "leading_zero");
        bool lz = cJSON_IsBool(j_lz) ? cJSON_IsTrue(j_lz) : false;
        tm1637_show_number(g_tm1637, num, lz);
        CMD_RESP_MSG(resp, "显示: %04d", num);
        return ESP_OK;
    }

    /* text */
    if (strcasecmp(action, "text") == 0) {
        const cJSON *j = cJSON_GetObjectItem(req, "value");
        if (!cJSON_IsString(j) || j->valuestring == NULL) {
            CMD_RESP_MSG(resp, "需要 value 字符串参数");
            return ESP_ERR_INVALID_ARG;
        }
        tm1637_show_string(g_tm1637, j->valuestring);
        CMD_RESP_MSG(resp, "显示: %.4s", j->valuestring);
        return ESP_OK;
    }

    /* raw */
    if (strcasecmp(action, "raw") == 0) {
        const cJSON *j = cJSON_GetObjectItem(req, "segs");
        if (!cJSON_IsArray(j) || cJSON_GetArraySize(j) != 4) {
            CMD_RESP_MSG(resp, "需要 segs 数组 [4字节]");
            return ESP_ERR_INVALID_ARG;
        }
        uint8_t segs[4];
        for (int i = 0; i < 4; i++) {
            segs[i] = (uint8_t)cJSON_GetArrayItem(j, i)->valueint;
        }
        tm1637_show_raw(g_tm1637, segs);
        CMD_RESP_MSG(resp, "raw: 0x%02X 0x%02X 0x%02X 0x%02X", segs[0], segs[1], segs[2], segs[3]);
        return ESP_OK;
    }

    /* clear */
    if (strcasecmp(action, "clear") == 0) {
        tm1637_clear(g_tm1637);
        CMD_RESP_MSG(resp, "已清空");
        return ESP_OK;
    }

    /* bright */
    if (strcasecmp(action, "bright") == 0) {
        const cJSON *j = cJSON_GetObjectItem(req, "value");
        if (!cJSON_IsNumber(j)) {
            CMD_RESP_MSG(resp, "需要 value 参数");
            return ESP_ERR_INVALID_ARG;
        }
        int val = j->valueint;
        if (val < 0 || val > 7) {
            CMD_RESP_MSG(resp, "value 范围 0~7");
            return ESP_ERR_INVALID_ARG;
        }
        tm1637_set_brightness(g_tm1637, (uint8_t)val);
        CMD_RESP_MSG(resp, "亮度=%d", val);
        return ESP_OK;
    }

    /* colon */
    if (strcasecmp(action, "colon") == 0) {
        const cJSON *j = cJSON_GetObjectItem(req, "on");
        if (!cJSON_IsBool(j)) {
            CMD_RESP_MSG(resp, "需要 on (true/false) 参数");
            return ESP_ERR_INVALID_ARG;
        }
        tm1637_set_colon(g_tm1637, cJSON_IsTrue(j));
        CMD_RESP_MSG(resp, "冒号: %s", cJSON_IsTrue(j) ? "开" : "关");
        return ESP_OK;
    }

    CMD_RESP_MSG(resp, "未知 act: %s", action);
    return ESP_ERR_NOT_FOUND;
}
