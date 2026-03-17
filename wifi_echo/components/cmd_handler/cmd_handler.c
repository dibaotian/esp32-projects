/*
 * Copyright (c) 2026 minxie <laba22@163.com>
 * All rights reserved.
 */

/*
 * JSON 命令分发器实现
 */

#include <stdio.h>
#include <string.h>
#include "cmd_handler.h"
#include "esp_log.h"

#define TAG "CMD"

char *cmd_dispatch(const char *json_str, const cmd_module_t modules[])
{
    cJSON *req = cJSON_Parse(json_str);
    if (!req) {
        ESP_LOGE(TAG, "JSON 解析失败");
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "status", "error");
        cJSON_AddStringToObject(err, "msg", "JSON 解析失败");
        char *out = cJSON_PrintUnformatted(err);
        cJSON_Delete(err);
        return out;
    }

    /* 提取 cmd 和 act */
    const cJSON *j_cmd = cJSON_GetObjectItem(req, "cmd");
    const cJSON *j_act = cJSON_GetObjectItem(req, "act");

    const char *cmd_name = cJSON_IsString(j_cmd) ? j_cmd->valuestring : NULL;
    const char *act_name = cJSON_IsString(j_act) ? j_act->valuestring : NULL;

    cJSON *resp = cJSON_CreateObject();
    if (cmd_name) cJSON_AddStringToObject(resp, "cmd", cmd_name);
    if (act_name) cJSON_AddStringToObject(resp, "act", act_name);

    if (!cmd_name) {
        cJSON_AddStringToObject(resp, "status", "error");
        cJSON_AddStringToObject(resp, "msg", "缺少 cmd 字段");
        goto done;
    }

    /* 查找模块 */
    for (int i = 0; modules[i].name != NULL; i++) {
        if (strcasecmp(modules[i].name, cmd_name) == 0) {
            if (!act_name) {
                cJSON_AddStringToObject(resp, "status", "error");
                cJSON_AddStringToObject(resp, "msg", "缺少 act 字段");
                goto done;
            }

            esp_err_t ret = modules[i].handler(act_name, req, resp);
            if (ret == ESP_OK) {
                cJSON_AddStringToObject(resp, "status", "ok");
            } else if (ret == ESP_ERR_NOT_FOUND) {
                cJSON_AddStringToObject(resp, "status", "error");
                char buf[64];
                snprintf(buf, sizeof(buf), "未知动作: %s", act_name);
                cJSON_AddStringToObject(resp, "msg", buf);
            } else {
                cJSON_AddStringToObject(resp, "status", "error");
                if (!cJSON_GetObjectItem(resp, "msg")) {
                    cJSON_AddStringToObject(resp, "msg", esp_err_to_name(ret));
                }
            }
            goto done;
        }
    }

    /* 模块未找到 */
    cJSON_AddStringToObject(resp, "status", "error");
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "未知模块: %s", cmd_name);
        cJSON_AddStringToObject(resp, "msg", buf);
    }

done:;
    char *out = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    cJSON_Delete(req);
    return out;
}

char *cmd_help(const cmd_module_t modules[])
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "cmd", "sys");
    cJSON_AddStringToObject(resp, "act", "help");

    cJSON *arr = cJSON_AddArrayToObject(resp, "modules");
    for (int i = 0; modules[i].name != NULL; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(modules[i].name));
    }

    char *out = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    return out;
}
