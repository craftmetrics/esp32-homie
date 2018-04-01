#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "freertos/event_groups.h"

#include "homie.h"

static const char* TAG = "HOMIE";

static esp_mqtt_client_handle_t client;
homie_config_t * config;

static void homie_connected();
static void homie_mktopic(char * topic, char * subtopic);

static bool _starts_with(const char *pre, const char *str, int lenstr)
{
    size_t lenpre = strlen(pre);
    return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}

static void homie_handle_mqtt_event(esp_mqtt_event_handle_t event)
{
    printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
    printf("DATA=%.*s\r\n", event->data_len, event->data);

    // Check if it is reboot command
    char topic[HOMIE_MAX_TOPIC_LEN];
    homie_mktopic(topic, "$implementation/reboot");
    if ((strncmp(topic, event->topic, event->topic_len) == 0) && (strncmp("true", event->data, event->data_len) == 0)) {
        ESP_LOGI(TAG, "Rebooting...");
        esp_restart_noos();
    }

    // Check if it is a OTA update
    homie_mktopic(topic, "$implementation/ota/firmware");
    if (_starts_with(topic, event->topic, event->topic_len)) {
        ESP_LOGI(TAG, "OTA Not Implemented");
        homie_publish("$implementation/ota/status", "501");
        return;
    }

    // Call the application's handler
    if (config->msg_handler) {
        config->msg_handler(event);
    }
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            homie_connected();
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            homie_handle_mqtt_event(event);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
    }
    return ESP_OK;
}

extern const uint8_t server_pem_start[] asm("_binary_server_pem_start");
extern const uint8_t server_pem_end[]   asm("_binary_server_pem_end");

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .event_handle = mqtt_event_handler,
        .lwt_msg = "false",
        .lwt_retain = 1,
        //.cert_pem = (const char *)server_pem_start,
    };
    strncpy(mqtt_cfg.uri, config->mqtt_uri, MQTT_MAX_HOST_LEN);
    strncpy(mqtt_cfg.username, config->mqtt_username, MQTT_MAX_USERNAME_LEN);
    strncpy(mqtt_cfg.password, config->mqtt_password, MQTT_MAX_PASSWORD_LEN);
    homie_mktopic(mqtt_cfg.lwt_topic, "$online");

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
}

static void homie_mktopic(char * topic, char * subtopic)
{
    snprintf(topic, HOMIE_MAX_TOPIC_LEN, "%s/%s/%s", config->base_topic, config->device_id, subtopic);
}

void homie_subscribe(char * subtopic)
{
    int msg_id;
    char topic[HOMIE_MAX_TOPIC_LEN];
    homie_mktopic(topic, subtopic);

    msg_id = esp_mqtt_client_subscribe(client, topic, 0);
    ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
}

void homie_publish(char * subtopic, char * payload)
{
    int msg_id;
    char topic[HOMIE_MAX_TOPIC_LEN];
    homie_mktopic(topic, subtopic);

    msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
    ESP_LOGI(TAG, "%s: %s => msg_id=%d", topic, payload, msg_id);
}

void homie_publish_int(char * subtopic, int payload)
{
    char payload_string[16];
    snprintf(payload_string, 16, "%d", payload);
    homie_publish(subtopic, payload_string);
}

void homie_publish_bool(char * subtopic, bool payload)
{
    homie_publish(subtopic, payload ? "true" : "false");
}

static int _clamp(int n, int lower, int upper) {
    return n <= lower ? lower : n >= upper ? upper : n;
}

static int8_t _get_wifi_rssi()
{
    wifi_ap_record_t info;
    if (!esp_wifi_sta_get_ap_info(&info)) {
        return info.rssi;
    }
    return 0;
}

static void _get_ip(char * ip_string)
{
    tcpip_adapter_ip_info_t ip;
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip);

    sprintf(
        ip_string,
        "%u.%u.%u.%u",
        (ip.ip.addr & 0x000000ff),
        (ip.ip.addr & 0x0000ff00) >> 8,
        (ip.ip.addr & 0x00ff0000) >> 16,
        (ip.ip.addr & 0xff000000) >> 24
    );
}

static void _get_mac(char * mac_string, bool sep)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    if (sep) sprintf(mac_string, "%X:%X:%X:%X:%X:%X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    else sprintf(mac_string, "%x%x%x%x%x%x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void homie_connected()
{
    char mac_address[18];
    char ip_address[16];
    _get_mac(mac_address, true);
    _get_ip(ip_address);

    homie_publish("$homie", "2.0.1");
    homie_publish("$online", "true");
    homie_publish("$name", config->device_name);
    homie_publish("$localip", ip_address);
    homie_publish("$mac", mac_address);
    homie_publish("$fw/name", config->firmware_name);
    homie_publish("$fw/version", config->firmware_version);
    homie_publish("$nodes", ""); // FIXME: needs to be extendible
    homie_publish("$implementation", "esp32-idf");
    homie_publish("$implementation/version", "dev");
    homie_publish("$stats", "uptime,rssi,signal,freeheap"); // FIXME: needs to be extendible
    homie_publish("$stats/interval", "30");
    homie_publish_bool("$implementation/ota/enabled", config->ota_enabled);

    homie_subscribe("$implementation/reboot");
    if (config->ota_enabled) homie_subscribe("$implementation/ota/firmware/#");

}

static void homie_task(void *pvParameter)
{
    while (1) {
        homie_publish_int("$stats/uptime", esp_timer_get_time()/1000000);

        int rssi = _get_wifi_rssi();
        homie_publish_int("$stats/rssi", rssi);

        // Translate to "signal" percentage, assuming RSSI range of (-100,-50)
        homie_publish_int("$stats/signal", _clamp((rssi+100)*2, 0, 100));

        homie_publish_int("$stats/freeheap", esp_get_free_heap_size());

        vTaskDelay(30000/portTICK_PERIOD_MS);
    }
}

void homie_init(homie_config_t *passed_config)
{
    config = passed_config;

    // If device_id is blank, generate one based off the mac
    if (!config->device_id[0]) _get_mac(config->device_id, false);

    mqtt_app_start();
    xTaskCreate(&homie_task, "homie_task", 8192, NULL, 5, NULL);
}
