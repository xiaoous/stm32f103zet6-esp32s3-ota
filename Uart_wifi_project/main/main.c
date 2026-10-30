#include "app_config.h"
#include "ota_proto.h"
#include "wifi_sta.h"
#include "ota_client.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

/*
 * ESP32-S3 侧 OTA 主机
 *
 * 流程：
 *   NVS 初始化 → 串口初始化 → 等 STM32 启动
 *   → 连 WiFi → 打开 HTTP 固件流 → START 帧
 *   → 边下边发（每 2KB 一帧，等 STM32 的 'A'）
 *   → 发 END 帧（整包 CRC32）→ 提示按键安装
 */
void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ota_proto_init();

    /* STM32 从上电到跳进 App 约 60ms，这里多等一会儿更稳 */
    vTaskDelay(pdMS_TO_TICKS(1500));

    if (wifi_sta_connect(APP_WIFI_SSID, APP_WIFI_PASS, APP_WIFI_TIMEOUT) != 0) {
        ESP_LOGE(TAG, "WiFi 连接失败，中止");
        return;
    }

    ESP_LOGI(TAG, "开始升级: %s", APP_FW_URL);

    if (ota_client_update(APP_FW_URL) != 0) {
        ESP_LOGE(TAG, "升级失败");
        return;
    }

    ESP_LOGI(TAG, "升级流程结束");
}
