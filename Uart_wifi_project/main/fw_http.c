#include "fw_http.h"

#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_log.h"

#define TAG "fw_http"

struct fw_http {
    esp_http_client_handle_t client;
};

fw_http_t *fw_http_open(const char *url, uint32_t *out_len)
{
    esp_http_client_config_t cfg = {
        .url        = url,
        .timeout_ms = 15000,
    };
    fw_http_t *h;

    if (out_len) {
        *out_len = 0;
    }

    h = calloc(1, sizeof(*h));
    if (!h) {
        return NULL;
    }

    h->client = esp_http_client_init(&cfg);
    if (!h->client) {
        free(h);
        return NULL;
    }

    if (esp_http_client_open(h->client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "连接失败: %s", url);
        esp_http_client_cleanup(h->client);
        free(h);
        return NULL;
    }

    int64_t len = esp_http_client_fetch_headers(h->client);
    int status = esp_http_client_get_status_code(h->client);
    ESP_LOGI(TAG, "HTTP %d, content-length=%lld", status, (long long)len);

    if (status != 200 || len <= 0) {
        ESP_LOGE(TAG, "HTTP 状态或长度不对");
        esp_http_client_close(h->client);
        esp_http_client_cleanup(h->client);
        free(h);
        return NULL;
    }

    if (out_len) {
        *out_len = (uint32_t)len;
    }
    return h;
}

int fw_http_read(fw_http_t *h, uint8_t *buf, size_t cap)
{
    if (!h || !h->client) {
        return -1;
    }
    return esp_http_client_read(h->client, (char *)buf, cap);
}

void fw_http_close(fw_http_t *h)
{
    if (!h) {
        return;
    }
    if (h->client) {
        esp_http_client_close(h->client);
        esp_http_client_cleanup(h->client);
    }
    free(h);
}
