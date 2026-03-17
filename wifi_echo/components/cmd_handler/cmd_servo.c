/*
 * 舵机命令处理模块
 *
 * 支持的 act:
 *   set     {"angle": 角度}                 直接设置角度
 *   get     {}                              获取当前角度
 *   smooth  {"angle": 角度, "speed": °/s}   平滑移动
 *   sweep   {"start":, "end":, "speed":, "count":}  往复扫描
 *   pulse   {"us": 脉宽μs}                 直接设置脉宽
 *   stop    {}                              释放力矩
 */

#include <string.h>
#include "cmd_handler.h"
#include "servo.h"
#include "esp_log.h"

/* 外部声明的全局舵机句柄 (在 main.c 中定义) */
extern servo_handle_t g_servo;

esp_err_t cmd_servo_handler(const char *action, const cJSON *req, cJSON *resp)
{
    if (!g_servo) {
        CMD_RESP_MSG(resp, "舵机未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    /* set */
    if (strcasecmp(action, "set") == 0) {
        const cJSON *j = cJSON_GetObjectItem(req, "angle");
        if (!cJSON_IsNumber(j)) {
            CMD_RESP_MSG(resp, "需要 angle 参数");
            return ESP_ERR_INVALID_ARG;
        }
        float angle = (float)j->valuedouble;
        if (angle < 0 || angle > 180) {
            CMD_RESP_MSG(resp, "angle 超出范围 0~180");
            return ESP_ERR_INVALID_ARG;
        }
        servo_set_angle(g_servo, angle);
        CMD_RESP_MSG(resp, "angle=%.1f", angle);
        return ESP_OK;
    }

    /* get */
    if (strcasecmp(action, "get") == 0) {
        float angle = servo_get_angle(g_servo);
        cJSON_AddNumberToObject(resp, "angle", angle);
        CMD_RESP_MSG(resp, "angle=%.1f", angle);
        return ESP_OK;
    }

    /* smooth */
    if (strcasecmp(action, "smooth") == 0) {
        const cJSON *j_angle = cJSON_GetObjectItem(req, "angle");
        const cJSON *j_speed = cJSON_GetObjectItem(req, "speed");
        if (!cJSON_IsNumber(j_angle) || !cJSON_IsNumber(j_speed)) {
            CMD_RESP_MSG(resp, "需要 angle 和 speed 参数");
            return ESP_ERR_INVALID_ARG;
        }
        float angle = (float)j_angle->valuedouble;
        float speed = (float)j_speed->valuedouble;
        if (angle < 0 || angle > 180) {
            CMD_RESP_MSG(resp, "angle 超出范围 0~180");
            return ESP_ERR_INVALID_ARG;
        }
        if (speed <= 0) {
            CMD_RESP_MSG(resp, "speed 必须 > 0");
            return ESP_ERR_INVALID_ARG;
        }
        servo_smooth_move(g_servo, angle, speed);
        CMD_RESP_MSG(resp, "smooth %.1f° @ %.1f°/s", angle, speed);
        return ESP_OK;
    }

    /* sweep */
    if (strcasecmp(action, "sweep") == 0) {
        const cJSON *j_start = cJSON_GetObjectItem(req, "start");
        const cJSON *j_end   = cJSON_GetObjectItem(req, "end");
        const cJSON *j_speed = cJSON_GetObjectItem(req, "speed");
        const cJSON *j_count = cJSON_GetObjectItem(req, "count");
        if (!cJSON_IsNumber(j_start) || !cJSON_IsNumber(j_end) || !cJSON_IsNumber(j_speed)) {
            CMD_RESP_MSG(resp, "需要 start, end, speed 参数");
            return ESP_ERR_INVALID_ARG;
        }
        float start = (float)j_start->valuedouble;
        float end   = (float)j_end->valuedouble;
        float speed = (float)j_speed->valuedouble;
        int count   = cJSON_IsNumber(j_count) ? j_count->valueint : 1;
        if (start < 0 || start > 180 || end < 0 || end > 180) {
            CMD_RESP_MSG(resp, "start/end 超出范围 0~180");
            return ESP_ERR_INVALID_ARG;
        }
        if (speed <= 0) {
            CMD_RESP_MSG(resp, "speed 必须 > 0");
            return ESP_ERR_INVALID_ARG;
        }
        servo_sweep(g_servo, start, end, speed, count);
        CMD_RESP_MSG(resp, "sweep %.1f~%.1f° @ %.1f°/s x%d", start, end, speed, count);
        return ESP_OK;
    }

    /* pulse */
    if (strcasecmp(action, "pulse") == 0) {
        const cJSON *j = cJSON_GetObjectItem(req, "us");
        if (!cJSON_IsNumber(j)) {
            CMD_RESP_MSG(resp, "需要 us 参数");
            return ESP_ERR_INVALID_ARG;
        }
        uint32_t us = (uint32_t)j->valueint;
        servo_set_pulse_width(g_servo, us);
        CMD_RESP_MSG(resp, "pulse=%luus", (unsigned long)us);
        return ESP_OK;
    }

    /* stop */
    if (strcasecmp(action, "stop") == 0) {
        servo_stop(g_servo);
        CMD_RESP_MSG(resp, "servo stopped");
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}
