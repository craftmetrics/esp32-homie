/*
 * esp32-homie OTA functionality is loosely based on
 * https://github.com/tuanpmt/esp32-fota
 * by Tuanpmt
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_request.h"
#include "homie.h"

#define TAG "HOMIE_OTA"

static void ota_deinit(char * url)
{
    free(url);
    vTaskDelete(NULL);
}

int download_callback(request_t *req, char *data, int len)
{
    esp_err_t err;
    req_list_t *tmp;
    static int total_len = -1;
    static int remaining_len;
    static esp_ota_handle_t update_handle;
    static const esp_partition_t *update_partition;

    if (total_len == -1) {
        tmp = req_list_get_key(req->response->header, "Content-Length");
        if (!tmp) {
            ESP_LOGE(TAG, "Content-Length not found");
            homie_publish("$implementation/ota/status", 0, 1, "500 no Content-Length");
            return -1;
        }
        total_len = atoi(tmp->value);
        remaining_len = total_len;
        ESP_LOGI(TAG, "Downloading %d bytes...", total_len);

        update_partition = esp_ota_get_next_update_partition(NULL);
        if (update_partition == NULL) {
            ESP_LOGE(TAG, "Invalid or no OTA data partition found");
            homie_publish("$implementation/ota/status", 0, 1, "500 no ota partition");
            return -1;
        }
        ESP_LOGI(TAG, "Writing to partition type %d subtype %d (offset 0x%08x)",
            update_partition->type, update_partition->subtype, update_partition->address);

        err = esp_ota_begin(update_partition, total_len, &update_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin error: %d", err);
            homie_publishf("$implementation/ota/status", 0, 1, "500 esp_ota_begin=%d", err);
            return -1;
        }
    }

    err = esp_ota_write(update_handle, (const void *)data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write error: %d", err);
        homie_publishf("$implementation/ota/status", 0, 1, "500 esp_ota_write=%d", err);
        return -1;
    }
    remaining_len -= len;

    // Send status message to indicate OTA progress
    char buf[32];
    sprintf(buf, "206 %d/%d", total_len-remaining_len, total_len);
    homie_publish("$implementation/ota/status", 0, 0, buf);

    if (remaining_len <= 0) {
        err = esp_ota_end(update_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end error: %d", err);
            homie_publishf("$implementation/ota/status", 0, 1, "500 esp_ota_end=%d", err);
            return -1;
        }
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition error: %d", err);
            homie_publishf("$implementation/ota/status", 0, 1, "500 esp_ota_set_boot_partition=%d", err);
            return -1;
        }
        ESP_LOGI(TAG, "OTA Update Complete - rebooting");
        // Send status message to indicate that OTA is complete
        homie_publish("$implementation/ota/status", 0, 1, "200");
        vTaskDelay(3000/portTICK_PERIOD_MS);
        esp_restart();
    }

    return 0;
}

static void ota_task(void *pvParameter)
{
    char * url = (char *)pvParameter;

    ESP_LOGI(TAG, "Downloading %s", url);

    request_t *req;
    int status;
    req = req_new(url);
    req_setopt(req, REQ_FUNC_DOWNLOAD_CB, download_callback);
    req_setopt(req, REQ_SET_HEADER, "User-Agent: esp32-homie OTA");

    status = req_perform(req);
    ESP_LOGI(TAG, "req_perform returned %d", status);
    req_clean(req);

    // Report http-related error
    if (status != 200) homie_publish_int("$implementation/ota/status", 0, 1, status);

    ota_deinit(url);
}

void ota_init(char * url)
{
    ESP_LOGI(TAG, "Initiating OTA");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    // Ensure OTA is configured
    if ((configured == NULL) || (configured == NULL)) {
        ESP_LOGE(TAG, "OTA partitions not configured");
        homie_publish("$implementation/ota/status", 0, 1, "500 no ota partitions");
        free(url);
        return;
    }

    // Check OTA partitions
    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);

    // Begin OTA task
    xTaskCreate(&ota_task, "ota_task", 8192, url, 5, NULL);
}
