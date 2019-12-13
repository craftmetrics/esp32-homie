/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_http_client.h>
#include <esp_flash_partitions.h>
#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "esp_idf_lib_helpers.h"
#if HELPER_TARGET_VERSION >= HELPER_TARGET_VERSION_ESP32_V4
#include <esp_event.h>
#else
#include <esp_event_loop.h>
#endif

#define BUFFSIZE 1024
#define HASH_LEN 32 /* SHA-256 digest length */
#define MUTEX_DO_NOT_BLOCK (0)

static const char *TAG = "native_ota_example";
/*an ota data write buffer ready to write to the flash*/
static char ota_write_data[BUFFSIZE + 1] = { 0 };
extern SemaphoreHandle_t mutex_ota;

static void http_cleanup(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

void do_ota(void *pvParameter)
{
    bool got_mutex = false;
    int binary_file_length = 0;
    esp_err_t err;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;
    esp_http_client_handle_t client = NULL;
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_http_client_config_t *config = (esp_http_client_config_t *)pvParameter;

    ESP_LOGI(TAG, "Starting OTA");

    if (xSemaphoreTake(mutex_ota, (TickType_t) MUTEX_DO_NOT_BLOCK) != pdTRUE) {
        ESP_LOGW(TAG, "Another OTA is in progress");
        goto fail;
    }
    got_mutex = true;

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);
    ESP_LOGI(TAG, "Fetching the update");
    printf("firmware URL: %s\n", config->url);

    client = esp_http_client_init(config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        goto fail;
    }
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        goto fail;
    }
    esp_http_client_fetch_headers(client);

    update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);
    assert(update_partition != NULL);

    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        goto fail;
    }
    ESP_LOGI(TAG, "esp_ota_begin succeeded");

    /*deal with all receive packet*/
    while (1) {
        int data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "Error: SSL data read error");
            goto fail;
        } else if (data_read > 0) {
            err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) {
                goto fail;
            }
            binary_file_length += data_read;
            ESP_LOGD(TAG, "Written image length %d", binary_file_length);
        } else if (data_read == 0) {
            ESP_LOGI(TAG, "Connection closed,all data received");
            break;
        }
    }
    ESP_LOGI(TAG, "Total Write binary data length : %d", binary_file_length);

    if (esp_ota_end(update_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed!");
        http_cleanup(client);
        goto fail;
    }

    if (esp_partition_check_identity(esp_ota_get_running_partition(), update_partition) == true) {
        ESP_LOGI(TAG, "The current running firmware is same as the firmware just downloaded");
        goto fail;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        goto fail;
    }
    ESP_LOGI(TAG, "Prepare to restart system!");
    esp_restart();
    while (1) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    /* NOT REACHED */
fail:
    if (got_mutex) {
        xSemaphoreGive(mutex_ota);
    }
    if (client != NULL) {
        http_cleanup(client);
    }
    vTaskDelete(NULL);
}

esp_err_t start_ota(esp_http_client_config_t config)
{
    ESP_LOGI(TAG, "Starting OTA");
    if (xTaskCreate(&do_ota, "do_ota", configMINIMAL_STACK_SIZE * 20, (void *)&config, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate() failed");
        goto fail;
    }
    return ESP_OK;
fail:
    return ESP_FAIL;
}
