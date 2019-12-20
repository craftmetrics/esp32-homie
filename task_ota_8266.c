/*
 *   MIT License
 *
 *   Copyright (c) 2019 Tomoyuki Sakurai
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy
 *   of this software and associated documentation files (the "Software"), to deal
 *   in the Software without restriction, including without limitation the rights
 *   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *   copies of the Software, and to permit persons to whom the Software is
 *   furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in all
 *   copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *   SOFTWARE.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_system.h>

#include "task_ota_8266.h"

#define MUTEX_DO_NOT_BLOCK (0)

static const char *TAG = "task_ota";
extern SemaphoreHandle_t mutex_ota;

static void do_ota(void *pvParameter)
{
    bool got_mutex = false;
    esp_err_t err;

    ESP_LOGI(TAG, "Starting OTA");
    esp_http_client_config_t *config = (esp_http_client_config_t *)pvParameter;
    if (xSemaphoreTake(mutex_ota, (TickType_t) MUTEX_DO_NOT_BLOCK) != pdTRUE) {
        ESP_LOGW(TAG, "Another OTA is in progress");
        goto fail;
    }
    got_mutex = true;
    printf("URL: %s", config->url);

    err = esp_https_ota(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Firmware upgrade failed");
        goto fail;
    }
    ESP_LOGI(TAG, "Prepare to restart system!");
    esp_restart();
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    /* NOT REACHED */
fail:
    if (got_mutex) {
        xSemaphoreGive(mutex_ota);
    }
    vTaskDelete(NULL);
}

esp_err_t start_ota(const esp_http_client_config_t config)
{
    ESP_LOGI(TAG, "Creating do_ota task");
    if (xTaskCreate(&do_ota, "do_ota", configMINIMAL_STACK_SIZE * 5, (void *)&config, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate()");
        goto fail;
    }
    return ESP_OK;
fail:
    return ESP_FAIL;
}
