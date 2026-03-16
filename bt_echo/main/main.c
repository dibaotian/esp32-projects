/*
 * ESP32 蓝牙 SPP 回声服务器
 * 功能: 接收消息并回复 "收到: <原始消息>"
 * 基于 ESP-IDF Bluedroid SPP Acceptor 示例
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"

#define TAG "BT_ECHO"
#define SPP_SERVER_NAME "ESP32_ECHO"
#define DEVICE_NAME "ESP32-Echo"

/* 回复前缀 */
static const char REPLY_PREFIX[] = "收到: ";

/* 当前连接的 SPP handle，用于发送数据 */
static uint32_t spp_handle = 0;

static char *bda2str(uint8_t *bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return str;
}

static void esp_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    char bda_str[18] = {0};

    switch (event) {
    case ESP_SPP_INIT_EVT:
        if (param->init.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(TAG, "SPP 初始化成功, 启动服务器...");
            esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_SLAVE, 0, SPP_SERVER_NAME);
        } else {
            ESP_LOGE(TAG, "SPP 初始化失败: %d", param->init.status);
        }
        break;

    case ESP_SPP_START_EVT:
        if (param->start.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(TAG, "SPP 服务器已启动, 等待连接...");
            esp_bt_gap_set_device_name(DEVICE_NAME);
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        } else {
            ESP_LOGE(TAG, "SPP 服务启动失败: %d", param->start.status);
        }
        break;

    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(TAG, "客户端已连接! handle:%"PRIu32" 地址:[%s]",
                 param->srv_open.handle,
                 bda2str(param->srv_open.rem_bda, bda_str, sizeof(bda_str)));
        spp_handle = param->srv_open.handle;
        break;

    case ESP_SPP_DATA_IND_EVT: {
        /* 收到数据 */
        uint16_t len = param->data_ind.len;
        uint8_t *data = param->data_ind.data;

        /* 打印收到的消息 (限制显示长度) */
        if (len < 512) {
            ESP_LOGI(TAG, "收到消息 (%d 字节): %.*s", len, len, (char *)data);
        } else {
            ESP_LOGI(TAG, "收到消息 (%d 字节)", len);
        }

        /* 构造回复: "收到: " + 原始消息 */
        size_t prefix_len = strlen(REPLY_PREFIX);
        size_t reply_len = prefix_len + len;

        /* 限制回复长度避免内存问题 */
        if (reply_len > 1024) {
            reply_len = 1024;
            len = reply_len - prefix_len;
        }

        uint8_t *reply = malloc(reply_len);
        if (reply != NULL) {
            memcpy(reply, REPLY_PREFIX, prefix_len);
            memcpy(reply + prefix_len, data, len);
            esp_spp_write(param->data_ind.handle, reply_len, reply);
            free(reply);
            ESP_LOGI(TAG, "已回复");
        } else {
            ESP_LOGE(TAG, "内存分配失败");
        }
        break;
    }

    case ESP_SPP_WRITE_EVT:
        if (param->write.status != ESP_SPP_SUCCESS) {
            ESP_LOGE(TAG, "发送失败: %d", param->write.status);
        }
        break;

    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(TAG, "连接已断开 (handle:%"PRIu32")", param->close.handle);
        if (param->close.handle == spp_handle) {
            spp_handle = 0;
        }
        break;

    default:
        ESP_LOGI(TAG, "SPP 事件: %d", event);
        break;
    }
}

static void esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    char bda_str[18] = {0};

    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "认证成功: %s [%s]",
                     param->auth_cmpl.device_name,
                     bda2str(param->auth_cmpl.bda, bda_str, sizeof(bda_str)));
        } else {
            ESP_LOGE(TAG, "认证失败: %d", param->auth_cmpl.stat);
        }
        break;

    case ESP_BT_GAP_PIN_REQ_EVT:
        ESP_LOGI(TAG, "PIN 码请求, 使用默认: 1234");
        {
            esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;

    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "SSP 确认请求, 数值: %"PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;

    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "SSP Passkey: %"PRIu32, param->key_notif.passkey);
        break;

    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(TAG, "SSP Key 请求");
        break;

    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(TAG, "GAP 模式变更: %d [%s]", param->mode_chg.mode,
                 bda2str(param->mode_chg.bda, bda_str, sizeof(bda_str)));
        break;

    default:
        ESP_LOGI(TAG, "GAP 事件: %d", event);
        break;
    }
}

void app_main(void)
{
    esp_err_t ret;

    /* 初始化 NVS (蓝牙需要) */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 释放 BLE 内存 (只用经典蓝牙) */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    /* 初始化蓝牙控制器 */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "蓝牙控制器初始化失败: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "蓝牙控制器启用失败: %s", esp_err_to_name(ret));
        return;
    }

    /* 初始化 Bluedroid */
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid 初始化失败: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid 启用失败: %s", esp_err_to_name(ret));
        return;
    }

    /* 注册 GAP 和 SPP 回调 */
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(esp_bt_gap_cb));
    ESP_ERROR_CHECK(esp_spp_register_callback(esp_spp_cb));

    /* 初始化 SPP */
    esp_spp_cfg_t spp_cfg = {
        .mode = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
        .tx_buffer_size = 0,
    };
    ESP_ERROR_CHECK(esp_spp_enhanced_init(&spp_cfg));

    /* 设置 SSP (Secure Simple Pairing) */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    /* 设置 PIN */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    char bda_str[18] = {0};
    ESP_LOGI(TAG, "=== ESP32 蓝牙回声服务器 ===");
    ESP_LOGI(TAG, "设备名称: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "MAC 地址: [%s]", bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));
    ESP_LOGI(TAG, "等待蓝牙连接...");
}
