/*
 * JSON 命令分发器 — 模块级静态分发, 零动态分配
 *
 * 协议格式:
 *   请求: {"cmd":"模块", "act":"动作", ...参数}
 *   响应: {"status":"ok|error", "cmd":"模块", "act":"动作", ...结果}
 */

#pragma once

#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 模块命令处理函数类型
 * @param action  动作名 (如 "beep", "set")
 * @param req     完整请求 JSON (可从中取参数)
 * @param resp    响应 JSON (需往里填结果字段)
 * @return ESP_OK=成功, ESP_ERR_NOT_FOUND=未知动作, 其他=执行错误
 */
typedef esp_err_t (*cmd_module_handler_t)(const char *action, const cJSON *req, cJSON *resp);

/** 模块描述 */
typedef struct {
    const char          *name;     /**< 模块名 (如 "buzzer", "servo") */
    cmd_module_handler_t handler;  /**< 模块处理函数 */
} cmd_module_t;

/**
 * @brief 处理 JSON 命令字符串 (核心入口)
 * @param json_str   输入的 JSON 字符串
 * @param modules    模块表 (以 {NULL,NULL} 结尾)
 * @return 响应 JSON 字符串, 调用者需 free(); NULL=解析失败
 */
char *cmd_dispatch(const char *json_str, const cmd_module_t modules[]);

/**
 * @brief 生成帮助信息 JSON
 * @param modules  模块表
 * @return JSON 字符串, 调用者需 free()
 */
char *cmd_help(const cmd_module_t modules[]);

/* ---- 便捷响应宏 ---- */

/** 向 resp 添加 "msg" 字段 */
#define CMD_RESP_MSG(resp, fmt, ...) do { \
    char _buf[192]; \
    snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    cJSON_AddStringToObject(resp, "msg", _buf); \
} while(0)

#ifdef __cplusplus
}
#endif
