/*
 * 蜂鸣器命令处理模块
 *
 * 支持的 act:
 *   beep    {"count": N}               哔 N 次 (1~20)
 *   tone    {"freq": Hz, "ms": 时长}    发声
 *   melody  {"name": "startup|success|error"}
 *   volume  {"value": 0~100}
 *   stop    {}                          停止发声
 */

#include <string.h>
#include "cmd_handler.h"
#include "buzzer.h"
#include "esp_log.h"

esp_err_t cmd_buzzer_handler(const char *action, const cJSON *req, cJSON *resp)
{
    /* beep */
    if (strcasecmp(action, "beep") == 0) {
        const cJSON *j = cJSON_GetObjectItem(req, "count");
        int count = cJSON_IsNumber(j) ? j->valueint : 1;
        if (count < 1 || count > 20) {
            CMD_RESP_MSG(resp, "count 范围 1~20");
            return ESP_ERR_INVALID_ARG;
        }
        buzzer_beep(count);
        CMD_RESP_MSG(resp, "beep %d", count);
        return ESP_OK;
    }

    /* tone */
    if (strcasecmp(action, "tone") == 0) {
        const cJSON *j_freq = cJSON_GetObjectItem(req, "freq");
        const cJSON *j_ms   = cJSON_GetObjectItem(req, "ms");
        if (!cJSON_IsNumber(j_freq) || !cJSON_IsNumber(j_ms)) {
            CMD_RESP_MSG(resp, "需要 freq 和 ms 参数");
            return ESP_ERR_INVALID_ARG;
        }
        uint32_t freq = (uint32_t)j_freq->valueint;
        uint32_t ms   = (uint32_t)j_ms->valueint;
        if (freq < 20 || freq > 20000) {
            CMD_RESP_MSG(resp, "freq 范围 20~20000");
            return ESP_ERR_INVALID_ARG;
        }
        if (ms > 10000) {
            CMD_RESP_MSG(resp, "ms 最大 10000");
            return ESP_ERR_INVALID_ARG;
        }
        buzzer_tone_ms(freq, ms);
        CMD_RESP_MSG(resp, "tone %luHz %lums", (unsigned long)freq, (unsigned long)ms);
        return ESP_OK;
    }

    /* melody */
    if (strcasecmp(action, "melody") == 0) {
        const cJSON *j = cJSON_GetObjectItem(req, "name");
        const char *name = cJSON_IsString(j) ? j->valuestring : "";
        if (strcasecmp(name, "startup") == 0) {
            buzzer_play_startup();
        } else if (strcasecmp(name, "success") == 0) {
            buzzer_play_success();
        } else if (strcasecmp(name, "error") == 0) {
            buzzer_play_error();
        } else {
            CMD_RESP_MSG(resp, "未知旋律: %s (可选: startup, success, error)", name);
            return ESP_ERR_INVALID_ARG;
        }
        CMD_RESP_MSG(resp, "melody %s", name);
        return ESP_OK;
    }

    /* volume */
    if (strcasecmp(action, "volume") == 0) {
        const cJSON *j = cJSON_GetObjectItem(req, "value");
        if (!cJSON_IsNumber(j)) {
            CMD_RESP_MSG(resp, "需要 value 参数");
            return ESP_ERR_INVALID_ARG;
        }
        int vol = j->valueint;
        if (vol < 0 || vol > 100) {
            CMD_RESP_MSG(resp, "value 范围 0~100");
            return ESP_ERR_INVALID_ARG;
        }
        buzzer_set_volume((uint8_t)vol);
        CMD_RESP_MSG(resp, "volume %d", vol);
        return ESP_OK;
    }

    /* stop */
    if (strcasecmp(action, "stop") == 0) {
        buzzer_stop();
        CMD_RESP_MSG(resp, "buzzer stopped");
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}
