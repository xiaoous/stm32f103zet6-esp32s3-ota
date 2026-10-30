#include "ota_client.h"
#include "ota_proto.h"
#include "fw_http.h"
#include "app_config.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#define TAG "ota"

#define STAGE_SIZE  APP_PROTO_CHUNK     /* 攒够一块再发，保证偶数长度 */

/*
 * 只下载一遍就够：
 *   START 帧先只报 size（crc32 填 0），边发边算整包 CRC32，
 *   最后一帧发完再发 END 帧把 CRC32 交给 STM32。
 *   STM32 校验通过后才擦写 W25Q128，并把结果通过 END 的应答告诉这里：
 *     'A' = 校验通过并已写入 W25Q128
 *     'E' = CRC 不对或写盘失败（此时 W25Q128 不被破坏）
 */

int ota_client_update(const char *url)
{
    uint32_t total = 0;
    uint32_t declared;
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t *stage = NULL;
    uint32_t stage_len = 0;
    uint32_t sent = 0;
    uint32_t prev_decade = 0;
    int ret = -1;
    fw_http_t *h = NULL;

    stage = malloc(STAGE_SIZE);
    if (!stage) {
        return -1;
    }

    h = fw_http_open(url, &total);
    if (!h) {
        goto out;
    }
    if (!strstr(url, ".bin")) {
        ESP_LOGW(TAG, "URL 里没有 .bin（%s）—— 很可能在下载目录页面而不是固件", url);
    }
    if (total < 512) {
        ESP_LOGE(TAG, "Content-Length=%u 太小，不可能是固件", (unsigned)total);
        goto out;
    }

    /* 奇数长度补 0xFF，补的字节也算进 CRC32，和 STM32 收到的保持一致 */
    declared = (total + 1u) & ~1u;

    if (ota_proto_start(declared, 0) != 0) {
        ESP_LOGE(TAG, "START 被拒绝（STM32 没在收？接线？W25Q128 没识别？）");
        goto out;
    }
    ESP_LOGI(TAG, "START OK，开始传输 %u 字节", (unsigned)declared);

    stage_len = 0;
    for (;;) {
        int n = fw_http_read(h, stage + stage_len, STAGE_SIZE - stage_len);
        if (n < 0) {
            ESP_LOGE(TAG, "HTTP 读取失败");
            goto out;
        }
        if (n == 0) {
            break;
        }
        stage_len += (uint32_t)n;

        if (stage_len >= STAGE_SIZE) {
            crc = ota_crc32_update(crc, stage, stage_len);
            if (ota_proto_send(stage, stage_len) != 0) {
                ESP_LOGE(TAG, "发送失败 @%u", (unsigned)sent);
                goto out;
            }
            sent += stage_len;
            stage_len = 0;
            {
                /* One line per 10%: a 100 KB image would otherwise print 50 lines. */
                uint32_t pct = sent * 100u / declared;
                if ((pct / 10u) != (prev_decade / 10u)) {
                    prev_decade = pct;
                    ESP_LOGI(TAG, "  %u/%u (%u%%)", (unsigned)sent, (unsigned)declared,
                             (unsigned)pct);
                }
            }
        }
    }

    /* 尾块（奇数补 0xFF） */
    if (sent < declared) {
        if (stage_len & 1u) {
            stage[stage_len++] = 0xFF;
        }
        if (stage_len) {
            crc = ota_crc32_update(crc, stage, stage_len);
            if (ota_proto_send(stage, stage_len) != 0) {
                ESP_LOGE(TAG, "尾块发送失败");
                goto out;
            }
            sent += stage_len;
        }
    }

    if (sent != declared) {
        ESP_LOGE(TAG, "长度不符: 发 %u / 声明 %u", (unsigned)sent, (unsigned)declared);
        goto out;
    }

    crc ^= 0xFFFFFFFFu;
    ESP_LOGI(TAG, "传输完成 %u 字节，crc32=0x%08X，发 END 让 STM32 校验并写 W25Q128 ...",
             (unsigned)sent, (unsigned)crc);

    if (ota_proto_end(crc) != 0) {
        ESP_LOGE(TAG, "END 失败：STM32 校验未通过或写 W25Q128 失败（flash 未被破坏）");
        goto out;
    }

    ESP_LOGI(TAG, "STM32 校验通过，镜像已存入 W25Q128；按 STM32 的 KEY1(PE3) 才会安装");
    ret = 0;

out:
    if (h) {
        fw_http_close(h);
    }
    free(stage);
    return ret;
}
