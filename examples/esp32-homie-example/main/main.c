#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_log.h>

#include "homie.h"

static const char* TAG = "EXAMPLE";

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void wifi_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "start the WIFI SSID:[%s] password:[%s]", CONFIG_WIFI_SSID, "******");
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    EventGroupHandle_t homie_event_group;

    wifi_init();

    static homie_config_t homie_conf = {
        .mqtt_uri = CONFIG_MQTT_URI,
        .mqtt_username = CONFIG_MQTT_USERNAME,
        .mqtt_password = CONFIG_MQTT_PASSWORD,
        .device_name = "My Device",
        .base_topic = "homie",
        .firmware_name = "Example",
        .firmware_version = "0.0.1",
        .ota_enabled = true,
    };
    homie_event_group = xEventGroupCreate();
    if (homie_event_group == NULL) {
        ESP_LOGE(TAG, "homie_event_group()");
    }
    homie_conf.event_group = &homie_event_group;

    homie_init(&homie_conf);

    // Keep the main task around
    while (1) {
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
}
