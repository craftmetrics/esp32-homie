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
#include <esp_err.h>
#include <esp_log.h>
#include <mqtt_client.h>

#include "esp_idf_lib_helpers.h"
#if HELPER_TARGET_VERSION >= HELPER_TARGET_VERSION_ESP32_V4
#include <esp_event.h>
#else
#include <esp_event_loop.h>
#endif

#include "task_log_mqtt.h"

#define TAG "homie_logger"
#define AUTO_LENGTH (0)

esp_mqtt_client_handle_t client;
TaskHandle_t task;
EventGroupHandle_t *event_group;
vprintf_like_t original_logger = NULL;
homie_log_mqtt_config_t *config;

esp_err_t log_mqtt_init(homie_log_mqtt_config_t *homie_log_mqtt_config)
{
    esp_err_t err;
    config = homie_log_mqtt_config;
    if (!config->mqtt_client) {
        err = ESP_ERR_INVALID_ARG;
        goto fail;
    }
    client = homie_log_mqtt_config->mqtt_client;
    event_group = config->mqtt_event_group;
    err = ESP_OK;
fail:
    return err;
}

static int logger(const char *str, va_list l)
{
    homie_log_message_t m;

    vsnprintf(m.payload, HOMIE_MAX_LOG_MESSAGE_LEN, str, l);
    if (xQueueSend(config->queue, (void *)&m, config->send_tick) != pdTRUE) {
        /* failed to send the item to the queue */
    }
    return 0;
}

static void log_mqtt_cleanup()
{
    if (original_logger != NULL) {
        esp_log_set_vprintf(original_logger);
        original_logger = NULL;
        ESP_LOGI(TAG, "Restored original_logger");
    }
}

static void log_mqtt(void *pvParameter)
{

    homie_log_message_t msg;
    int msg_id;

    ESP_LOGI(TAG, "Starting the loop");
    original_logger = esp_log_set_vprintf(&logger);

    while (1) {
        if (xQueueReceive(config->queue, &msg, config->wait_tick)) {
            msg_id = esp_mqtt_client_publish(
                    config->mqtt_client,
                    config->topic,
                    msg.payload,
                    AUTO_LENGTH,
                    config->qos,
                    config->retain);
            if (msg_id < 0) {
                /* failed to publish */
            }
        }
    }
    log_mqtt_cleanup();
    ESP_LOGE(TAG, "log_mqtt(): Deleting the task");
    vTaskDelete(NULL);
}

TaskHandle_t log_mqtt_start()
{
    ESP_LOGI(TAG, "Starting task_log_mqtt");
    if (xTaskCreate(&log_mqtt, "log_mqtt", config->stack_size, (void *)&config, config->priority, &task) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate()");
        goto fail;
    }
    return &task;
fail:
    return NULL;
}

void log_mqtt_stop()
{
    ESP_LOGI(TAG, "Stopping log_mqtt task");
    log_mqtt_cleanup();
    if (task != NULL) {
        vTaskDelete(task);
    }
}
