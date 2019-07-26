/*
 * esp32-homie OTA functionality is loosely based on
 * https://github.com/tuanpmt/esp32-fota
 * by Tuanpmt
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "math.h"

#include "esp_wifi.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "homie.h"

#define TAG "HOMIE_OTA"

typedef struct
{
    char *url;
    void (*status_handler)(int);
} homie_ota_config_t;
static homie_ota_config_t *config = NULL;

static void ota_deinit()
{
    free(config->url);
    free(config);
    config = NULL;
    vTaskDelete(NULL);
}

int download_callback(esp_http_client_handle_t client, char *data, int len)
{
    esp_err_t err;

    // FIXME: After a failed HTTP request, this static state is not cleaned up
    static int total_len = -1;
    static int remaining_len;
    static esp_ota_handle_t update_handle;
    static const esp_partition_t *update_partition;

    if (total_len == -1)
    {
        int tmp = esp_http_client_get_content_length(client);
        if (!tmp)
        {
            ESP_LOGE(TAG, "Content-Length not found");
            homie_publish("$implementation/ota/status", 1, 0, "500 no Content-Length");
            return -1;
        }
        total_len = tmp;
        remaining_len = total_len;
        ESP_LOGI(TAG, "Downloading %d bytes...", total_len);

        update_partition = esp_ota_get_next_update_partition(NULL);
        if (update_partition == NULL)
        {
            ESP_LOGE(TAG, "Invalid or no OTA data partition found");
            homie_publish("$implementation/ota/status", 1, 0, "500 no ota partition");
            return -1;
        }
        ESP_LOGI(TAG, "Writing to partition type %d subtype %d (offset 0x%08x)",
                 update_partition->type, update_partition->subtype, update_partition->address);

        err = esp_ota_begin(update_partition, total_len, &update_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_begin error: %d", err);
            homie_publishf("$implementation/ota/status", 1, 0, "500 esp_ota_begin=%d", err);
            return -1;
        }

        // Called once with `0` to let application know that OTA has been initiated
        if (config->status_handler)
        {
            config->status_handler(0);
        }
    }

    err = esp_ota_write(update_handle, (const void *)data, len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_write error: %d", err);
        homie_publishf("$implementation/ota/status", 1, 0, "500 esp_ota_write=%d", err);
        return -1;
    }
    remaining_len -= len;

    // Send status message to indicate OTA progress
    char buf[32];
    sprintf(buf, "206 %d/%d", total_len - remaining_len, total_len);
    ESP_LOGI(TAG, "%s", buf);
    homie_publish("$implementation/ota/status", 1, 0, buf);

    // Called with non-zero values periodically, indicating the percentage complete
    if (config->status_handler)
    {
        config->status_handler(ceilf((float)(total_len - remaining_len) * 100. / total_len));
    }

    if (remaining_len <= 0)
    {
        err = esp_ota_end(update_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_end error: %d", err);
            homie_publishf("$implementation/ota/status", 1, 0, "500 esp_ota_end=%d", err);
            return -1;
        }
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition error: %d", err);
            homie_publishf("$implementation/ota/status", 1, 0, "500 esp_ota_set_boot_partition=%d", err);
            return -1;
        }
        ESP_LOGI(TAG, "OTA Update Complete - rebooting");
        // Send status message to indicate that OTA is complete
        homie_publish("$implementation/ota/status", 1, 0, "200");
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        esp_restart();
    }

    return 0;
}

esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        download_callback(evt->client, (char *)evt->data, evt->data_len);

        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;
}

static void ota_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Downloading %s", config->url);

    esp_http_client_config_t http_config = {
        .url = config->url,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    esp_err_t err = esp_http_client_perform(client);

    int status = esp_http_client_get_status_code(client);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "GET Status = %d, content_length = %d",
                 status,
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "GET request failed: %d", err);
    }

    esp_http_client_cleanup(client);

    // Report http-related error
    if (status != 200)
        homie_publish_int("$implementation/ota/status", 1, 0, status);

    ota_deinit();
}

void ota_init(char *url, void (*status_handler)(int))
{
    ESP_LOGI(TAG, "Initiating OTA");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    // Ensure OTA is configured
    if ((configured == NULL) || (configured == NULL))
    {
        ESP_LOGE(TAG, "OTA partitions not configured");
        homie_publish("$implementation/ota/status", 1, 0, "500 no ota partitions");
        free(url);
        return;
    }

    // Check OTA partitions
    if (configured != running)
    {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);

    // Ensure we can't start multiple
    if (config != NULL)
    {
        ESP_LOGE(TAG, "OTA already initiated (0x%x)", (unsigned int)config);
        homie_publish("$implementation/ota/status", 1, 0, "500 ota already initiated");
        free(url);
        return;
    }

    config = calloc(1, sizeof(homie_ota_config_t));
    config->url = url;
    config->status_handler = status_handler;

    // Begin OTA task
    xTaskCreate(&ota_task, "ota_task", 8192, NULL, 5, NULL);
}
