/*
 * Copyright (c) 2026 minxie <laba22@163.com>
 * All rights reserved.
 */

/*
 * ESP32 micro-ROS 控制节点
 * 功能:
 *   - WiFi STA 连接 Ubuntu 热点 (SSID: Ubuntu-ROS)
 *   - micro-ROS 节点通过 UDP 连接 Agent (10.42.0.1:8888)
 *   - ROS 2 话题控制蜂鸣器/舵机/数码管/LCD
 *   - 心跳发布 + 舵机状态反馈
 */

#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include <uros_network_interfaces.h>
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <std_msgs/msg/int32.h>
#include <std_msgs/msg/float32.h>
#include <std_msgs/msg/string.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>

#ifdef CONFIG_MICRO_ROS_ESP_XRCE_DDS_MIDDLEWARE
#include <rmw_microros/rmw_microros.h>
#endif

#include "buzzer.h"
#include "servo.h"
#include "tm1637.h"
#include "grove_lcd.h"

#define TAG "UROS_CTRL"

#define RCCHECK(fn) { rcl_ret_t temp_rc = fn; \
    if ((temp_rc != RCL_RET_OK)) { \
        ESP_LOGE(TAG, "Failed line %d: %d. Aborting.", __LINE__, (int)temp_rc); \
        vTaskDelete(NULL); } }
#define RCSOFTCHECK(fn) { rcl_ret_t temp_rc = fn; \
    if ((temp_rc != RCL_RET_OK)) { \
        ESP_LOGW(TAG, "Failed line %d: %d. Continuing.", __LINE__, (int)temp_rc); } }

/* ---- 硬件句柄 ---- */
static servo_handle_t g_servo = NULL;
static tm1637_handle_t g_tm1637 = NULL;
static grove_lcd_handle_t g_lcd = NULL;

/* ---- ROS 发布者 ---- */
static rcl_publisher_t pub_heartbeat;
static rcl_publisher_t pub_servo_state;

/* ---- ROS 订阅者 ---- */
static rcl_subscription_t sub_servo_cmd;
static rcl_subscription_t sub_buzzer_cmd;
static rcl_subscription_t sub_display_cmd;
static rcl_subscription_t sub_lcd_cmd;

/* ---- 消息缓冲 ---- */
static std_msgs__msg__Int32   msg_heartbeat;
static std_msgs__msg__Float32 msg_servo_state;
static std_msgs__msg__Float32 msg_servo_cmd;
static std_msgs__msg__Int32   msg_buzzer_cmd;
static std_msgs__msg__Int32   msg_display_cmd;
static std_msgs__msg__String  msg_lcd_cmd;

/* LCD 消息接收缓冲 (静态分配, 避免动态内存) */
static char lcd_cmd_buf[64];

/* ================================================================
 *  硬件命令队列 (非阻塞回调 → worker 任务执行)
 *
 *  所有 subscriber 回调只往队列发命令立即返回,
 *  不阻塞 executor, 保持 XRCE-DDS 会话活跃。
 * ================================================================ */

typedef enum {
    HW_CMD_SERVO,
    HW_CMD_BUZZER,
    HW_CMD_DISPLAY,
    HW_CMD_LCD,
} hw_cmd_type_t;

typedef struct {
    hw_cmd_type_t type;
    union {
        float   servo_angle;
        int32_t buzzer_val;
        int32_t display_val;
        char    lcd_text[64];
    };
} hw_cmd_t;

static QueueHandle_t g_hw_queue = NULL;

/* Worker 任务: 从队列取命令, 执行硬件操作 (允许阻塞) */
static void hw_worker_task(void *arg)
{
    hw_cmd_t cmd;
    while (1) {
        if (xQueueReceive(g_hw_queue, &cmd, portMAX_DELAY) != pdTRUE)
            continue;
        switch (cmd.type) {
        case HW_CMD_SERVO: {
            float angle = cmd.servo_angle;
            if (angle < 0.0f)   angle = 0.0f;
            if (angle > 180.0f) angle = 180.0f;
            ESP_LOGI(TAG, "舵机命令: %.1f°", angle);
            servo_set_angle(g_servo, angle);
            break;
        }
        case HW_CMD_BUZZER: {
            int32_t val = cmd.buzzer_val;
            if (val > 0 && val <= 10) {
                ESP_LOGI(TAG, "蜂鸣器: beep × %d", (int)val);
                buzzer_beep((int)val);
            } else if (val > 20 && val <= 20000) {
                ESP_LOGI(TAG, "蜂鸣器: tone %d Hz / 500ms", (int)val);
                buzzer_tone_ms((uint32_t)val, 500);
            } else if (val == 0) {
                ESP_LOGI(TAG, "蜂鸣器: 停止");
                buzzer_stop();
            } else if (val == -1) {
                buzzer_play_startup();
            } else if (val == -2) {
                buzzer_play_success();
            } else if (val == -3) {
                buzzer_play_error();
            }
            break;
        }
        case HW_CMD_DISPLAY: {
            int32_t num = cmd.display_val;
            ESP_LOGI(TAG, "数码管: %d", (int)num);
            if (num >= 0 && num <= 9999) {
                tm1637_show_number(g_tm1637, (int)num, false);
            } else if (num == -1) {
                tm1637_clear(g_tm1637);
            }
            break;
        }
        case HW_CMD_LCD: {
            ESP_LOGI(TAG, "LCD: %s", cmd.lcd_text);
            grove_lcd_clear(g_lcd);
            grove_lcd_set_cursor(g_lcd, 0, 0);
            size_t len = strlen(cmd.lcd_text);
            if (len <= 16) {
                grove_lcd_print(g_lcd, cmd.lcd_text);
            } else {
                char line1[17];
                memcpy(line1, cmd.lcd_text, 16);
                line1[16] = '\0';
                grove_lcd_print(g_lcd, line1);
                grove_lcd_set_cursor(g_lcd, 0, 1);
                grove_lcd_print(g_lcd, cmd.lcd_text + 16);
            }
            break;
        }
        }
    }
}

/* ================================================================
 *  订阅者回调 (非阻塞: 仅入队, 立即返回)
 * ================================================================ */

/*
 * /esp32/servo_cmd (Float32) → 舵机角度 0~180
 */
static void servo_cmd_callback(const void *msgin)
{
    const std_msgs__msg__Float32 *msg = (const std_msgs__msg__Float32 *)msgin;
    hw_cmd_t cmd = { .type = HW_CMD_SERVO, .servo_angle = msg->data };
    xQueueSend(g_hw_queue, &cmd, 0);
}

/*
 * /esp32/buzzer_cmd (Int32) → 蜂鸣器控制
 */
static void buzzer_cmd_callback(const void *msgin)
{
    const std_msgs__msg__Int32 *msg = (const std_msgs__msg__Int32 *)msgin;
    hw_cmd_t cmd = { .type = HW_CMD_BUZZER, .buzzer_val = msg->data };
    xQueueSend(g_hw_queue, &cmd, 0);
}

/*
 * /esp32/display_cmd (Int32) → 数码管显示
 */
static void display_cmd_callback(const void *msgin)
{
    const std_msgs__msg__Int32 *msg = (const std_msgs__msg__Int32 *)msgin;
    hw_cmd_t cmd = { .type = HW_CMD_DISPLAY, .display_val = msg->data };
    xQueueSend(g_hw_queue, &cmd, 0);
}

/*
 * /esp32/lcd_cmd (String) → LCD 显示文本
 */
static void lcd_cmd_callback(const void *msgin)
{
    const std_msgs__msg__String *msg = (const std_msgs__msg__String *)msgin;
    if (msg->data.data && msg->data.size > 0) {
        hw_cmd_t cmd = { .type = HW_CMD_LCD };
        size_t copy_len = msg->data.size < sizeof(cmd.lcd_text) - 1
                        ? msg->data.size : sizeof(cmd.lcd_text) - 1;
        memcpy(cmd.lcd_text, msg->data.data, copy_len);
        cmd.lcd_text[copy_len] = '\0';
        xQueueSend(g_hw_queue, &cmd, 0);
    }
}

/* ================================================================
 *  心跳定时器回调 (5 秒周期)
 * ================================================================ */

static void heartbeat_timer_callback(rcl_timer_t *timer, int64_t last_call_time)
{
    RCLC_UNUSED(last_call_time);
    if (timer == NULL) return;

    /* 发布心跳: uptime 秒 */
    msg_heartbeat.data = (int32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
    RCSOFTCHECK(rcl_publish(&pub_heartbeat, &msg_heartbeat, NULL));

    /* 发布舵机当前角度 */
    msg_servo_state.data = servo_get_angle(g_servo);
    RCSOFTCHECK(rcl_publish(&pub_servo_state, &msg_servo_state, NULL));
}

/* ================================================================
 *  开机动画
 * ================================================================ */

static void boot_animation(void)
{
    /* LCD: 显示启动信息 + 彩虹 RGB */
    const uint8_t rainbow[][3] = {
        {255,0,0}, {255,80,0}, {255,200,0}, {200,255,0},
        {0,255,0}, {0,255,150}, {0,200,255}, {0,100,255},
        {0,0,255}, {80,0,255}, {200,0,255}, {255,0,200},
    };
    grove_lcd_clear(g_lcd);
    grove_lcd_set_cursor(g_lcd, 1, 0);
    grove_lcd_print(g_lcd, "micro-ROS Node");
    grove_lcd_set_cursor(g_lcd, 1, 1);
    grove_lcd_print(g_lcd, "== ESP32  AI ==");
    for (int i = 0; i < 12; i++) {
        grove_lcd_set_rgb(g_lcd, rainbow[i][0], rainbow[i][1], rainbow[i][2]);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    /* 数码管: 显示 "rOS2" */
    tm1637_show_string(g_tm1637, "rOS2");

    /* 舵机: 快速扫描 */
    servo_smooth_move(g_servo, 0, 80);
    vTaskDelay(pdMS_TO_TICKS(300));
    servo_smooth_move(g_servo, 180, 60);
    vTaskDelay(pdMS_TO_TICKS(300));
    servo_smooth_move(g_servo, 90, 40);

    /* LCD: 切换到连接提示 */
    grove_lcd_clear(g_lcd);
    grove_lcd_set_rgb(g_lcd, 0, 200, 255);
    grove_lcd_set_cursor(g_lcd, 0, 0);
    grove_lcd_print(g_lcd, "Connecting WiFi");
    grove_lcd_set_cursor(g_lcd, 0, 1);
    grove_lcd_print(g_lcd, "Ubuntu-ROS...");

    buzzer_beep(2);
}

/* ================================================================
 *  micro-ROS 主任务
 * ================================================================ */

static void micro_ros_task(void *arg)
{
    rcl_allocator_t allocator = rcl_get_default_allocator();
    rclc_support_t support;

    /* 初始化选项 */
    rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();
    RCCHECK(rcl_init_options_init(&init_options, allocator));

#ifdef CONFIG_MICRO_ROS_ESP_XRCE_DDS_MIDDLEWARE
    rmw_init_options_t *rmw_options =
        rcl_init_options_get_rmw_init_options(&init_options);
    RCCHECK(rmw_uros_options_set_udp_address(
        CONFIG_MICRO_ROS_AGENT_IP,
        CONFIG_MICRO_ROS_AGENT_PORT,
        rmw_options));
#endif

    /* 连接 Agent (带重试, 每次超时后重新初始化) */
    ESP_LOGI(TAG, "连接 micro-ROS Agent (%s:%s) ...",
             CONFIG_MICRO_ROS_AGENT_IP, CONFIG_MICRO_ROS_AGENT_PORT);
    rcl_ret_t rc;
    int attempt = 0;
    do {
        attempt++;
        rc = rclc_support_init_with_options(&support, 0, NULL,
                                            &init_options, &allocator);
        if (rc != RCL_RET_OK) {
            ESP_LOGW(TAG, "Agent 连接失败 (尝试 %d), 2 秒后重试...", attempt);
            vTaskDelay(pdMS_TO_TICKS(2000));
            /* 重新初始化 init_options 用于下次尝试 */
            rcl_init_options_fini(&init_options);
            init_options = rcl_get_zero_initialized_init_options();
            rcl_init_options_init(&init_options, allocator);
#ifdef CONFIG_MICRO_ROS_ESP_XRCE_DDS_MIDDLEWARE
            rmw_options = rcl_init_options_get_rmw_init_options(&init_options);
            rmw_uros_options_set_udp_address(
                CONFIG_MICRO_ROS_AGENT_IP,
                CONFIG_MICRO_ROS_AGENT_PORT,
                rmw_options);
#endif
        }
    } while (rc != RCL_RET_OK);
    ESP_LOGI(TAG, "Agent 已连接! (第 %d 次尝试)", attempt);

    /* 创建节点: /esp32/esp32_controller */
    rcl_node_t node;
    RCCHECK(rclc_node_init_default(&node, "esp32_controller", "esp32",
                                   &support));
    ESP_LOGI(TAG, "micro-ROS 节点已创建");

    /* ---- 发布者 ---- */
    RCCHECK(rclc_publisher_init_default(
        &pub_heartbeat, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
        "/esp32/heartbeat"));

    RCCHECK(rclc_publisher_init_default(
        &pub_servo_state, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32),
        "/esp32/servo_state"));

    /* ---- 订阅者 ---- */
    RCCHECK(rclc_subscription_init_default(
        &sub_servo_cmd, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32),
        "/esp32/servo_cmd"));

    RCCHECK(rclc_subscription_init_default(
        &sub_buzzer_cmd, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
        "/esp32/buzzer_cmd"));

    RCCHECK(rclc_subscription_init_default(
        &sub_display_cmd, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
        "/esp32/display_cmd"));

    RCCHECK(rclc_subscription_init_default(
        &sub_lcd_cmd, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String),
        "/esp32/lcd_cmd"));

    /* ---- 心跳定时器 (5 秒) ---- */
    rcl_timer_t timer;
    RCCHECK(rclc_timer_init_default2(
        &timer, &support,
        RCL_MS_TO_NS(5000),
        heartbeat_timer_callback, true));

    /* 预分配 lcd_cmd String 缓冲 (避免动态内存) */
    msg_lcd_cmd.data.data = lcd_cmd_buf;
    msg_lcd_cmd.data.size = 0;
    msg_lcd_cmd.data.capacity = sizeof(lcd_cmd_buf);

    /* ---- 执行器: 4 subscribers + 1 timer = 5 handles ---- */
    rclc_executor_t executor;
    RCCHECK(rclc_executor_init(&executor, &support.context, 5, &allocator));
    RCCHECK(rclc_executor_add_subscription(
        &executor, &sub_servo_cmd, &msg_servo_cmd,
        servo_cmd_callback, ON_NEW_DATA));
    RCCHECK(rclc_executor_add_subscription(
        &executor, &sub_buzzer_cmd, &msg_buzzer_cmd,
        buzzer_cmd_callback, ON_NEW_DATA));
    RCCHECK(rclc_executor_add_subscription(
        &executor, &sub_display_cmd, &msg_display_cmd,
        display_cmd_callback, ON_NEW_DATA));
    RCCHECK(rclc_executor_add_subscription(
        &executor, &sub_lcd_cmd, &msg_lcd_cmd,
        lcd_cmd_callback, ON_NEW_DATA));
    RCCHECK(rclc_executor_add_timer(&executor, &timer));

    /* 连接成功 → 更新 LCD */
    grove_lcd_clear(g_lcd);
    grove_lcd_set_rgb(g_lcd, 0, 0, 255);
    grove_lcd_set_cursor(g_lcd, 0, 0);
    grove_lcd_print(g_lcd, "ROS2 Connected!");
    grove_lcd_set_cursor(g_lcd, 0, 1);
    grove_lcd_print(g_lcd, "Node: esp32_ctrl");
    tm1637_show_string(g_tm1637, "GO  ");
    buzzer_play_success();

    /* 启动硬件 worker 任务 (从队列取命令, 允许阻塞) */
    g_hw_queue = xQueueCreate(8, sizeof(hw_cmd_t));
    xTaskCreate(hw_worker_task, "hw_worker", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "执行器已启动, 等待 ROS 2 命令...");

    /* 主循环 */
    while (1) {
        rclc_executor_spin_some(&executor, RCL_MS_TO_NS(100));
        usleep(10000);
    }

    /* 清理 (正常不会到达) */
    RCCHECK(rcl_subscription_fini(&sub_servo_cmd, &node));
    RCCHECK(rcl_subscription_fini(&sub_buzzer_cmd, &node));
    RCCHECK(rcl_subscription_fini(&sub_display_cmd, &node));
    RCCHECK(rcl_subscription_fini(&sub_lcd_cmd, &node));
    RCCHECK(rcl_publisher_fini(&pub_heartbeat, &node));
    RCCHECK(rcl_publisher_fini(&pub_servo_state, &node));
    RCCHECK(rcl_timer_fini(&timer));
    RCCHECK(rcl_node_fini(&node));
    vTaskDelete(NULL);
}

/* ================================================================
 *  app_main
 * ================================================================ */

void app_main(void)
{
    /* NVS 初始化 (WiFi 驱动需要) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== ESP32 micro-ROS 控制节点 ===");

    /* 初始化硬件驱动 */
    buzzer_init(NULL);
    servo_init(NULL, &g_servo);

    tm1637_config_t tm_cfg = TM1637_CONFIG_DEFAULT();
    tm_cfg.brightness = 7;
    tm1637_init(&tm_cfg, &g_tm1637);

    grove_lcd_config_t lcd_cfg = GROVE_LCD_CONFIG_DEFAULT();
    grove_lcd_init(&lcd_cfg, &g_lcd);

    /* 开机动画 */
    boot_animation();

    /* 初始化网络接口 (WiFi STA 连接 Ubuntu-ROS 热点) */
#if defined(CONFIG_MICRO_ROS_ESP_NETIF_WLAN) || \
    defined(CONFIG_MICRO_ROS_ESP_NETIF_ENET)
    ESP_ERROR_CHECK(uros_network_interface_initialize());
#endif
    ESP_LOGI(TAG, "WiFi 已连接, 启动 micro-ROS 任务");

    /* LCD 更新: WiFi 已连接 */
    grove_lcd_clear(g_lcd);
    grove_lcd_set_rgb(g_lcd, 0, 255, 80);
    grove_lcd_set_cursor(g_lcd, 0, 0);
    grove_lcd_print(g_lcd, "WiFi Connected!");
    grove_lcd_set_cursor(g_lcd, 0, 1);
    grove_lcd_print(g_lcd, "Starting uROS..");
    vTaskDelay(pdMS_TO_TICKS(5000));

    /* 5 秒后切换到自定义欢迎消息 */
    grove_lcd_clear(g_lcd);
    grove_lcd_set_rgb(g_lcd, 0, 0, 255);
    grove_lcd_set_cursor(g_lcd, 0, 0);
    grove_lcd_print(g_lcd, "Hi Min Iam ready");
    grove_lcd_set_cursor(g_lcd, 0, 1);
    grove_lcd_print(g_lcd, "feed me token!");

    /* 启动 micro-ROS 任务 (PIN 到 APP_CPU, PRO_CPU 处理 WiFi) */
    xTaskCreate(micro_ros_task,
                "uros_task",
                CONFIG_MICRO_ROS_APP_STACK,
                NULL,
                CONFIG_MICRO_ROS_APP_TASK_PRIO,
                NULL);
}
