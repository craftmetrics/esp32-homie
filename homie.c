#include <stdarg.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

#include "homie.h"
#include "mqtt_client.h"
#include "ota.h"

static const char *TAG = "HOMIE";

static esp_mqtt_client_handle_t client;
static homie_config_t *config;

static void homie_connected();

static bool _starts_with(const char *pre, const char *str, int lenstr)
{
    size_t lenpre = strlen(pre);
    return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}

#define REMOTE_LOGGING_MAX_PAYLOAD_LEN 1024
static int _homie_logger(const char *str, va_list l)
{
    char buf[REMOTE_LOGGING_MAX_PAYLOAD_LEN];

    vsnprintf(buf, REMOTE_LOGGING_MAX_PAYLOAD_LEN, str, l);
    homie_publish("log", 1, 0, buf, 0);
    return vprintf(str, l);
}

static void homie_handle_mqtt_event(esp_mqtt_event_handle_t event)
{
    printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
    printf("DATA=%.*s\r\n", event->data_len, event->data);

    // Check if it is reboot command
    char topic[HOMIE_MAX_TOPIC_LEN];
    homie_mktopic(topic, "$implementation/reboot");
    if ((strncmp(topic, event->topic, event->topic_len) == 0) && (strncmp("true", event->data, event->data_len) == 0))
    {
        ESP_LOGI(TAG, "Rebooting...");
        esp_restart();
        return;
    }

    // Check if it is enable remote console
    homie_mktopic(topic, "$implementation/logging");
    if (strncmp(topic, event->topic, event->topic_len) == 0)
    {
        if (strncmp("true", event->data, event->data_len) == 0)
        {
            ESP_LOGI(TAG, "Enable remote logging");
            esp_log_set_vprintf(_homie_logger);
            ESP_LOGI(TAG, "Remote logging enabled");
        }
        else
        {
            ESP_LOGI(TAG, "Disable remote logging");
            esp_log_set_vprintf(vprintf);
            ESP_LOGI(TAG, "Remote logging disabled");
        }
        return;
    }

    // Check if it is a OTA update
    homie_mktopic(topic, "$implementation/ota/url");
    if (_starts_with(topic, event->topic, event->topic_len))
    {
        char *url = calloc(1, event->data_len + 1);
        strncpy(url, event->data, event->data_len);
        url[event->data_len] = '\0';
        ota_init(url, config->cert_pem, config->ota_status_handler);
        return;
    }

    // Call the application's handler
    homie_mktopic(topic, "");
    if (config->msg_handler)
    {
        int subtopic_len = event->topic_len - strlen(topic);
        char *subtopic = calloc(1, subtopic_len + 1);
        strncpy(subtopic, event->topic + strlen(topic), subtopic_len);
        subtopic[subtopic_len] = '\0';

        char *payload = calloc(1, event->data_len + 1);
        strncpy(payload, event->data, event->data_len);
        payload[event->data_len] = '\0';

        config->msg_handler(subtopic, payload);

        free(subtopic);
        free(payload);
    }
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    switch (event->event_id)
    {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
        break;

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        homie_connected();
        if (config->connected_handler)
            config->connected_handler();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        if (config->disconnected_handler)
            config->disconnected_handler();
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        // Disabled to avoid triggering circular events with remote logging
        // ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        homie_handle_mqtt_event(event);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;

    case MQTT_EVENT_ANY:
        ESP_LOGI(TAG, "MQTT_EVENT_ANY");
        break;

    case MQTT_EVENT_DELETED:
        ESP_LOGI(TAG, "MQTT_EVENT_DELETED");
        break;
    }

    return ESP_OK;
}

static void mqtt_app_start(void)
{
    char *lwt_topic = calloc(1, HOMIE_MAX_TOPIC_LEN);
    homie_mktopic(lwt_topic, "$online");

    esp_mqtt_client_config_t mqtt_cfg = {
        .client_id = config->client_id,
        .uri = config->mqtt_uri,
        .username = config->mqtt_username,
        .password = config->mqtt_password,

        .event_handle = mqtt_event_handler,
        .lwt_msg = "false",
        .lwt_retain = 1,
        .lwt_topic = lwt_topic,
        .keepalive = 15,
        .cert_pem = config->cert_pem,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
}

void homie_mktopic(char *topic, const char *subtopic)
{
    snprintf(topic, HOMIE_MAX_TOPIC_LEN, "%s/%s/%s", config->base_topic, config->client_id, subtopic);
}

void homie_subscribe(const char *subtopic)
{
    int msg_id;
    char topic[HOMIE_MAX_TOPIC_LEN];
    homie_mktopic(topic, subtopic);

    msg_id = esp_mqtt_client_subscribe(client, topic, 0);
    ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
}

int homie_publish(const char *subtopic, int qos, int retain, const char *payload, int len)
{
    if (config == NULL)
    {
        ESP_LOGI(TAG, "Attempted to publish before homie connected");
        return -2;
    }

    // int msg_id;
    char topic[HOMIE_MAX_TOPIC_LEN];
    homie_mktopic(topic, subtopic);

    return esp_mqtt_client_enqueue(client, topic, payload, len, qos, retain, true);
}

int homie_publishf(const char *subtopic, int qos, int retain, const char *format, ...)
{
    char payload_string[64];
    va_list argptr;
    va_start(argptr, format);
    vsnprintf(payload_string, 64, format, argptr);
    va_end(argptr);
    return homie_publish(subtopic, qos, retain, payload_string, 0);
}

int homie_publish_int(const char *subtopic, int qos, int retain, int payload)
{
    char payload_string[16];
    snprintf(payload_string, 16, "%d", payload);
    return homie_publish(subtopic, qos, retain, payload_string, 0);
}

int homie_publish_bool(const char *subtopic, int qos, int retain, bool payload)
{
    return homie_publish(subtopic, qos, retain, payload ? "true" : "false", 0);
}

static int _clamp(int n, int lower, int upper)
{
    return n <= lower ? lower : n >= upper ? upper : n;
}

static int8_t _get_wifi_rssi()
{
    wifi_ap_record_t info;
    if (!esp_wifi_sta_get_ap_info(&info))
    {
        return info.rssi;
    }
    return 0;
}

static void _get_ip(char *ip_string)
{
    tcpip_adapter_ip_info_t ip;
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip);

    sprintf(ip_string, "%u.%u.%u.%u", (ip.ip.addr & 0x000000ff), (ip.ip.addr & 0x0000ff00) >> 8,
            (ip.ip.addr & 0x00ff0000) >> 16, (ip.ip.addr & 0xff000000) >> 24);
}

static void _get_mac(char *mac_string, bool sep)
{
    // NB: This is the base mac of the device. The actual wifi and eth MAC addresses
    //     will be assigned as offsets from this.

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    if (sep)
        sprintf(mac_string, "%X:%X:%X:%X:%X:%X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    else
        sprintf(mac_string, "%x%x%x%x%x%x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void homie_connected()
{
    char mac_address[18];
    char ip_address[16];
    _get_mac(mac_address, true);
    _get_ip(ip_address);

    homie_publish("$homie", 0, 1, "2.0.1", 0);
    homie_publish("$online", 0, 1, "true", 0);
    homie_publish("$name", 0, 1, config->device_name, 0);
    homie_publish("$localip", 0, 1, ip_address, 0);
    homie_publish("$mac", 0, 1, mac_address, 0);
    homie_publish("$fw/name", 0, 1, config->firmware_name, 0);
    homie_publish("$fw/version", 0, 1, config->firmware_version, 0);
    homie_publish("$nodes", 0, 1, "", 0); // FIXME: needs to be extendible
    homie_publish("$implementation", 0, 1, "esp32-idf", 0);
    homie_publish("$implementation/version", 0, 1, "dev", 0);

    homie_publish("$stats", 0, 1, "uptime,rssi,signal,freeheap", 0); // FIXME: needs to be extendible
    homie_publish("$stats/interval", 0, 1, "30", 0);
    homie_publish_bool("$implementation/ota/enabled", 0, 1, config->ota_enabled);

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition != NULL)
    {
        homie_publishf("$implementation/ota/running", 0, 1, "0x%08x", running_partition->address);
    }
    else
    {
        homie_publishf("$implementation/ota/running", 0, 1, "NULL");
    }

    const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
    if (boot_partition != NULL)
    {
        homie_publishf("$implementation/ota/boot", 0, 1, "0x%08x", boot_partition->address);
    }
    else
    {
        homie_publishf("$implementation/ota/boot", 0, 1, "NULL");
    }

    homie_subscribe("$implementation/reboot");
    homie_subscribe("$implementation/logging");
    if (config->ota_enabled)
        homie_subscribe("$implementation/ota/url/#");
}

static void homie_task(void *pvParameter)
{
    while (1)
    {
        homie_publish_int("$stats/uptime", 0, 0, esp_timer_get_time() / 1000000);

        int rssi = _get_wifi_rssi();
        homie_publish_int("$stats/rssi", 0, 0, rssi);

        // Translate to "signal" percentage, assuming RSSI range of (-100,-50)
        homie_publish_int("$stats/signal", 0, 0, _clamp((rssi + 100) * 2, 0, 100));

        homie_publish_int("$stats/freeheap", 0, 0, esp_get_free_heap_size());

        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}

void homie_init(homie_config_t *passed_config)
{
    config = passed_config;

    // If client_id is blank, generate one based off the mac
    if (!config->client_id[0])
        _get_mac(config->client_id, false);

    mqtt_app_start();
    xTaskCreate(&homie_task, "homie_task", 8192, NULL, 5, NULL);
}
