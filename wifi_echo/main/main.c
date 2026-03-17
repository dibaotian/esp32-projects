/*
 * Copyright (c) 2026 minxie <laba22@163.com>
 * All rights reserved.
 */

/*
 * ESP32 WiFi AP + TCP JSON 控制服务器
 * 功能:
 *   - ESP32 创建 WiFi 热点 (SSID: ESP32-Control, 密码: esp32ctrl)
 *   - 启动 TCP 服务器 (端口 3333)
 *   - JSON 协议控制蜂鸣器/舵机, 支持模块化扩展
 *   - 非 JSON 消息回声
 */

#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "buzzer.h"
#include "servo.h"
#include "cmd_handler.h"
#include "tm1637.h"
#include "grove_lcd.h"

#define TAG              "WIFI_CTRL"
#define WIFI_SSID        "ESP32-Control"
#define WIFI_PASS        "esp32ctrl"
#define WIFI_CHANNEL     6
#define MAX_CLIENTS      4
#define TCP_PORT         3333
#define BUF_SIZE         1024

/* 全局舵机句柄 (cmd_servo.c 通过 extern 引用) */
servo_handle_t g_servo = NULL;
tm1637_handle_t g_tm1637 = NULL;
grove_lcd_handle_t g_lcd = NULL;

/* 外部声明的模块处理函数 */
extern esp_err_t cmd_buzzer_handler(const char *action, const cJSON *req, cJSON *resp);
extern esp_err_t cmd_servo_handler(const char *action, const cJSON *req, cJSON *resp);
extern esp_err_t cmd_display_handler(const char *action, const cJSON *req, cJSON *resp);
extern esp_err_t cmd_lcd_handler(const char *action, const cJSON *req, cJSON *resp);

/* sys 模块: help */
static esp_err_t cmd_sys_handler(const char *action, const cJSON *req, cJSON *resp)
{
    if (strcasecmp(action, "help") == 0) {
        cJSON_AddStringToObject(resp, "msg",
            "模块: buzzer(beep/tone/melody/volume/stop), "
            "servo(set/get/smooth/sweep/pulse/stop), "
            "display(number/text/raw/clear/bright/colon), "
            "lcd(print/clear/rgb/cursor/on/off), "
            "sys(help)");
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

/* ---- 模块注册表 (静态, 零分配) ---- */
static const cmd_module_t g_modules[] = {
    { "buzzer",  cmd_buzzer_handler  },
    { "servo",   cmd_servo_handler   },
    { "display", cmd_display_handler },
    { "lcd",     cmd_lcd_handler     },
    { "sys",     cmd_sys_handler     },
    { NULL, NULL }  /* 终止标记 */
};

/* WiFi 事件处理 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "客户端连接, AID=%d", event->aid);
        buzzer_beep(3);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "客户端断开, AID=%d", event->aid);
        buzzer_beep(1);
    }
}

/* 初始化 WiFi AP */
static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_CLIENTS,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .required = true },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP 已启动");
    ESP_LOGI(TAG, "  SSID:     %s", WIFI_SSID);
    ESP_LOGI(TAG, "  密码:     %s", WIFI_PASS);
    ESP_LOGI(TAG, "  频道:     %d", WIFI_CHANNEL);
    ESP_LOGI(TAG, "  IP:       192.168.4.1");
    ESP_LOGI(TAG, "  TCP 端口: %d", TCP_PORT);
}

/* ---- TCP 回复辅助 ---- */
static void tcp_reply(int sock, const char *msg)
{
    int len = strlen(msg);
    int sent = 0;
    while (sent < len) {
        int ret = send(sock, msg + sent, len - sent, 0);
        if (ret < 0) break;
        sent += ret;
    }
}

/* 处理单个客户端连接 */
static void handle_client(void *pvParameters)
{
    int sock = (int)(intptr_t)pvParameters;
    char rx_buf[BUF_SIZE];

    ESP_LOGI(TAG, "客户端任务启动, socket=%d", sock);

    while (1) {
        int len = recv(sock, rx_buf, sizeof(rx_buf) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "接收错误: errno %d", errno);
            break;
        }
        if (len == 0) {
            ESP_LOGI(TAG, "客户端断开连接");
            break;
        }

        rx_buf[len] = '\0';

        /* 去除末尾换行 */
        while (len > 0 && (rx_buf[len - 1] == '\n' || rx_buf[len - 1] == '\r')) {
            rx_buf[--len] = '\0';
        }
        if (len == 0) continue;

        ESP_LOGI(TAG, "收到 (%d 字节): %s", len, rx_buf);

        /* 检测是否为 JSON (以 '{' 开头) */
        if (rx_buf[0] == '{') {
            char *resp = cmd_dispatch(rx_buf, g_modules);
            if (resp) {
                tcp_reply(sock, resp);
                tcp_reply(sock, "\n");
                ESP_LOGI(TAG, "回复: %s", resp);
                free(resp);
            }
        } else {
            /* 非 JSON: 回声 */
            static const char prefix[] = "收到: ";
            char tx_buf[BUF_SIZE];
            size_t prefix_len = strlen(prefix);
            size_t reply_len = prefix_len + len;
            if (reply_len >= sizeof(tx_buf)) {
                reply_len = sizeof(tx_buf) - 1;
            }
            memcpy(tx_buf, prefix, prefix_len);
            memcpy(tx_buf + prefix_len, rx_buf, reply_len - prefix_len);
            tx_buf[reply_len] = '\0';
            tcp_reply(sock, tx_buf);
            tcp_reply(sock, "\n");
            ESP_LOGI(TAG, "回声: %s", tx_buf);
        }
    }

    close(sock);
    ESP_LOGI(TAG, "客户端任务结束, socket=%d", sock);
    vTaskDelete(NULL);
}

/* TCP 服务器主任务 */
static void tcp_server_task(void *pvParameters)
{
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "创建 socket 失败: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "bind 失败: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, 2) != 0) {
        ESP_LOGE(TAG, "listen 失败: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "TCP 服务器已启动, 监听端口 %d", TCP_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "accept 失败: errno %d", errno);
            continue;
        }

        char addr_str[16];
        inet_ntoa_r(client_addr.sin_addr, addr_str, sizeof(addr_str));
        ESP_LOGI(TAG, "新客户端连接: %s:%d", addr_str, ntohs(client_addr.sin_port));

        /* 为每个客户端创建独立任务 */
        xTaskCreate(handle_client, "client", 8192,
                    (void *)(intptr_t)client_sock, 5, NULL);
    }

    close(listen_sock);
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* 初始化 NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== ESP32 WiFi 控制服务器 ===");

    /* 初始化蜂鸣器 */
    buzzer_init(NULL);

    /* 初始化舵机 */
    servo_init(NULL, &g_servo);

    /* 初始化数码管 */
    tm1637_config_t tm_cfg = TM1637_CONFIG_DEFAULT();
    tm_cfg.brightness = 7;
    tm1637_init(&tm_cfg, &g_tm1637);

    /* 开机彩蛋: 数码管 + LCD 同步炫彩动画 (~10秒) */

    /* 初始化 Grove LCD RGB */
    grove_lcd_config_t lcd_cfg = GROVE_LCD_CONFIG_DEFAULT();
    grove_lcd_init(&lcd_cfg, &g_lcd);

    /* --- 数码管动画任务 (后台运行) --- */
    static volatile bool tm_anim_done = false;
    void tm_anim_task(void *arg) {
        /* 阶段1: 数据流脉冲 (×5轮, ~2.7s) */
        const uint8_t pulse_frames[][4] = {
            {0x40, 0x00, 0x00, 0x00},
            {0x40, 0x40, 0x00, 0x00},
            {0x49, 0x40, 0x40, 0x00},
            {0x00, 0x49, 0x40, 0x40},
            {0x00, 0x00, 0x49, 0x40},
            {0x00, 0x00, 0x00, 0x49},
            {0x00, 0x00, 0x00, 0x00},
        };
        for (int r = 0; r < 5; r++) {
            for (int f = 0; f < 7; f++) {
                tm1637_show_raw(g_tm1637, pulse_frames[f]);
                vTaskDelay(pdMS_TO_TICKS(55));
            }
        }

        /* 阶段2: 黑客帝国解码 (~3s) */
        const char *hex = "0123456789AbCdEF";
        char buf[5] = "    ";
        uint32_t seed = (uint32_t)(xTaskGetTickCount());
        for (int i = 0; i < 30; i++) {
            seed = seed * 1103515245 + 12345;
            for (int d = 0; d < 4; d++)
                buf[d] = hex[(seed >> (d * 4)) & 0x0F];
            tm1637_show_string(g_tm1637, buf);
            vTaskDelay(pdMS_TO_TICKS(50 + i * 3));
        }

        /* 阶段3: 逐位锁定 9527 (~2.5s) */
        const char target[] = "9527";
        for (int lock = 0; lock < 4; lock++) {
            buf[lock] = target[lock];
            for (int i = 0; i < 6; i++) {
                seed = seed * 1103515245 + 12345;
                for (int d = lock + 1; d < 4; d++)
                    buf[d] = hex[(seed >> (d * 3)) & 0x0F];
                tm1637_show_string(g_tm1637, buf);
                vTaskDelay(pdMS_TO_TICKS(60 + lock * 25));
            }
        }

        /* 阶段4: 呼吸觉醒 (~1.8s) */
        tm1637_show_string(g_tm1637, "9527");
        for (int b = 0; b <= 7; b++) {
            tm1637_set_brightness(g_tm1637, b);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        /* 闪烁两下强化 */
        for (int i = 0; i < 2; i++) {
            tm1637_set_brightness(g_tm1637, 0);
            vTaskDelay(pdMS_TO_TICKS(120));
            tm1637_set_brightness(g_tm1637, 7);
            vTaskDelay(pdMS_TO_TICKS(120));
        }

        tm_anim_done = true;
        vTaskDelete(NULL);
    }

    /* --- 舵机动画任务 (后台运行) --- */
    static volatile bool servo_anim_done = false;
    void servo_anim_task(void *arg) {
        /* 阶段1: 雷达扫描 — 从0°慢扫到180° (~2.5s) */
        servo_smooth_move(g_servo, 0, 80);
        vTaskDelay(pdMS_TO_TICKS(300));
        servo_smooth_move(g_servo, 180, 60);
        vTaskDelay(pdMS_TO_TICKS(300));

        /* 阶段2: 快速左右探测 — 像在搜索信号 (~3s) */
        const float scan[] = {30, 150, 60, 120, 45, 135, 80, 100};
        for (int i = 0; i < 8; i++) {
            servo_set_angle(g_servo, scan[i]);
            vTaskDelay(pdMS_TO_TICKS(250));
        }

        /* 阶段3: 兴奋抖动 — 找到了! (~2s) */
        servo_set_angle(g_servo, 90);
        vTaskDelay(pdMS_TO_TICKS(200));
        for (int i = 0; i < 6; i++) {
            servo_set_angle(g_servo, 90 + (i % 2 ? 15 : -15));
            vTaskDelay(pdMS_TO_TICKS(120));
        }

        /* 阶段4: 优雅归位 — 慢速回到90°就绪位 (~2s) */
        servo_smooth_move(g_servo, 90, 30);

        servo_anim_done = true;
        vTaskDelete(NULL);
    }

    tm_anim_done = false;
    servo_anim_done = false;
    xTaskCreate(tm_anim_task, "tm_anim", 2048, NULL, 3, NULL);
    xTaskCreate(servo_anim_task, "sv_anim", 2048, NULL, 3, NULL);

    /* --- LCD 动画 (主线程, 与数码管并行) --- */
    {
        /* 阶段1: RGB 彩虹扫描 + "Booting..." (~2.4s) */
        const uint8_t rainbow[][3] = {
            {255,0,0}, {255,80,0}, {255,200,0}, {200,255,0},
            {0,255,0}, {0,255,150}, {0,200,255}, {0,100,255},
            {0,0,255}, {80,0,255}, {200,0,255}, {255,0,200},
        };
        grove_lcd_clear(g_lcd);
        grove_lcd_set_cursor(g_lcd, 2, 0);
        grove_lcd_print(g_lcd, "Booting...");
        grove_lcd_set_cursor(g_lcd, 1, 1);
        grove_lcd_print(g_lcd, "== ESP32  AI ==");
        for (int i = 0; i < 12; i++) {
            grove_lcd_set_rgb(g_lcd, rainbow[i][0], rainbow[i][1], rainbow[i][2]);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        /* 阶段2: 清屏 + 逐字打字机 第一行 (~3.2s) */
        grove_lcd_clear(g_lcd);
        grove_lcd_set_rgb(g_lcd, 0, 255, 80);
        grove_lcd_blink_on(g_lcd);
        const char *line1 = "Hi Min I'm ready";
        for (int i = 0; line1[i]; i++) {
            grove_lcd_set_cursor(g_lcd, i, 0);
            grove_lcd_write_char(g_lcd, line1[i]);
            vTaskDelay(pdMS_TO_TICKS(120));
            /* 背光微妙脉动 */
            uint8_t pulse = (uint8_t)(200 + 55 * ((i % 3 == 0) ? 1 : 0));
            grove_lcd_set_rgb(g_lcd, 0, pulse, 80);
        }
        vTaskDelay(pdMS_TO_TICKS(300));

        /* 阶段3: 逐字打字机 第二行 + 渐变到蓝色 (~3.4s) */
        const char *line2 = "Feed me token!";
        int len2 = 14;
        for (int i = 0; line2[i]; i++) {
            grove_lcd_set_cursor(g_lcd, i, 1);
            grove_lcd_write_char(g_lcd, line2[i]);
            /* 从绿渐变到蓝 */
            uint8_t r = 0;
            uint8_t g = (uint8_t)(255 - (i * 255 / len2));
            uint8_t b = (uint8_t)(80 + (i * 175 / len2));
            grove_lcd_set_rgb(g_lcd, r, g, b);
            vTaskDelay(pdMS_TO_TICKS(140));
        }
        grove_lcd_blink_off(g_lcd);

        /* 阶段4: 最终蓝色, 等数码管和舵机动画都结束 */
        grove_lcd_set_rgb(g_lcd, 0, 0, 255);
        while (!tm_anim_done || !servo_anim_done) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    ESP_LOGI(TAG, "数码管已就绪");
    ESP_LOGI(TAG, "Grove LCD 已就绪");

    /* 启动 WiFi AP */
    wifi_init_softap();

    /* 启动 TCP 服务器 */
    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);

    /* 设备就绪提示音 */
    buzzer_beep(2);
    ESP_LOGI(TAG, "设备已就绪");
}
