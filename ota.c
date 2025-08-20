/*
 * esp32-homie OTA functionality is loosely based on
 * https://github.com/tuanpmt/esp32-fota
 * by Tuanpmt
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "math.h"

#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "homie.h"

#define TAG "HOMIE_OTA"

typedef struct
{
    char *url;
    const char *cert_pem;
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

static void ota_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Downloading %s", config->url);
    if (config->status_handler)
        config->status_handler(0);

    homie_publish("$implementation/ota/status", 1, 0, "202 ota begin", 0);

    esp_http_client_config_t ota_http_config = {.url = config->url, .cert_pem = config->cert_pem};
    esp_https_ota_config_t ota_config = {.http_config = &ota_http_config};
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK)
    {
        if (config->status_handler)
            config->status_handler(1);

        ESP_LOGI(TAG, "OTA Update Complete - rebooting");
        // Send status message to indicate that OTA is complete
        homie_publish("$implementation/ota/status", 1, 0, "200", 0);
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        esp_restart();
    }
    else
    {
        if (config->status_handler)
            config->status_handler(-1);

        ESP_LOGE(TAG, "esp_https_ota error: %d", ret);
        homie_publishf("$implementation/ota/status", 1, 0, "500 esp_https_ota=%d", ret);
    }

    ota_deinit();
}

void ota_init(char *url, const char *cert_pem, void (*status_handler)(int))
{
    ESP_LOGI(TAG, "Initiating OTA");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    // Ensure OTA is configured
    if ((configured == NULL) || (configured == NULL))
    {
        ESP_LOGE(TAG, "OTA partitions not configured");
        homie_publish("$implementation/ota/status", 1, 0, "500 no ota partitions", 0);
        free(url);
        return;
    }

    // Check OTA partitions
    if (configured != running)
    {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG,
                 "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)", running->type, running->subtype,
             running->address);

    // Ensure we can't start multiple
    if (config != NULL)
    {
        ESP_LOGE(TAG, "OTA already initiated (0x%x)", (unsigned int)config);
        homie_publish("$implementation/ota/status", 1, 0, "500 ota already initiated", 0);
        free(url);
        return;
    }

    config = calloc(1, sizeof(homie_ota_config_t));
    config->url = url;
    config->status_handler = status_handler;
    config->cert_pem = cert_pem;

    // Begin OTA task
    xTaskCreate(&ota_task, "ota_task", 8192, NULL, 5, NULL);
}
