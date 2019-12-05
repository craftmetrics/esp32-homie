#if defined(CONFIG_IDF_TARGET_ESP32) // defined in esp-idf 4.x, but not 3.x
#define HOMIE_IDF_VERSION3
#else
#define HOMIE_IDF_VERSION4
#endif

#include <stdarg.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#if defined(HOMIE_IDF_VERSION4)
#include "esp_event.h"
#else
#include "esp_event_loop.h"
#endif
#include "esp_ota_ops.h"
#include "freertos/event_groups.h"

#include "homie.h"
#include "ota.h"

#define QOS_0 (0)
#define QOS_1 (1)
#define QOS_2 (2)
#define RETAINED (1)
#define NOT_RETAINED (0)

static const char *TAG = "HOMIE";
static esp_mqtt_client_handle_t client;
static homie_config_t *config;
static EventGroupHandle_t *mqtt_group;

static void homie_connected();

static bool _starts_with(const char *pre, const char *str, int lenstr)
{
    size_t lenpre = strlen(pre);
    return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}

#define REMOTE_LOGGING_MAX_PAYLOAD_LEN 1024
static int _homie_logger(const char *str, va_list l)
{
    int ret;
    char buf[REMOTE_LOGGING_MAX_PAYLOAD_LEN];

    ret = vsnprintf(buf, REMOTE_LOGGING_MAX_PAYLOAD_LEN, str, l);
    if (ret < 0 || ret >= sizeof(buf)) {
        ESP_LOGW(TAG, "_homie_logger(): too long");
    }
    homie_publish("log", 1, 0, buf);
    return vprintf(str, l);
}

static void homie_handle_mqtt_event(esp_mqtt_event_handle_t event)
{
    ESP_LOGD(TAG, "topic: %s length: %d data length: %d", event->topic, event->topic_len, event->data_len);

    // Check if it is reboot command
    char topic[HOMIE_MAX_TOPIC_LEN];
    ESP_ERROR_CHECK(homie_mktopic(topic, "$implementation/reboot"));
    if ((strncmp(topic, event->topic, event->topic_len) == 0) && (strncmp("true", event->data, event->data_len) == 0))
    {
        ESP_LOGI(TAG, "Rebooting...");
        esp_restart();
        return;
    }

    // Check if it is enable remote console
    ESP_ERROR_CHECK(homie_mktopic(topic, "$implementation/logging"));
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
    ESP_ERROR_CHECK(homie_mktopic(topic, "$implementation/ota/url"));
    if (_starts_with(topic, event->topic, event->topic_len))
    {
        char *url = calloc(1, event->data_len + 1);
        strncpy(url, event->data, event->data_len);
        url[event->data_len] = '\0';
        ota_init(url, config->cert_pem, config->ota_status_handler);
        return;
    }

    // Call the application's handler
    ESP_ERROR_CHECK(homie_mktopic(topic, ""));
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

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    switch (event->event_id)
    {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
        break;

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        xEventGroupSetBits(*mqtt_group, HOMIE_MQTT_CONNECTED_BIT);
        xEventGroupSetBits(*mqtt_group, HOMIE_MQTT_STATUS_UPDATE_REQUIRED);
        if (config->connected_handler != NULL)
            config->connected_handler();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        xEventGroupClearBits(*mqtt_group, HOMIE_MQTT_STATUS_UPDATE_REQUIRED);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        // Disabled to avoid triggering circular events with remote logging
        //ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
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

#if defined(HOMIE_IDF_VERSION4)
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}
#endif

static esp_mqtt_client_handle_t mqtt_app_start(void)
{
    char *lwt_topic = calloc(1, HOMIE_MAX_TOPIC_LEN);
    esp_err_t err;

    ESP_LOGI(TAG, "URI: `%s`", config->mqtt_uri);
    ESP_LOGI(TAG, "client_id: `%s`", config->client_id);
    ESP_LOGI(TAG, "user name: `%s`", config->mqtt_username);
    ESP_LOGI(TAG, "base_topic: `%s`", config->base_topic);
    ESP_LOGI(TAG, "stack_size: %d", config->stack_size);

    ESP_ERROR_CHECK(homie_mktopic(lwt_topic, "$online"));

    esp_mqtt_client_config_t mqtt_cfg = {
        .client_id = config->client_id,
        .uri = config->mqtt_uri,
        .username = config->mqtt_username,
        .password = config->mqtt_password,
#if defined(HOMIE_IDF_VERSION3)
        .event_handle = mqtt_event_handler_cb,
#endif
        .lwt_msg = "false",
        .lwt_retain = 1,
        .lwt_topic = lwt_topic,
        .keepalive = 15,
        .cert_pem = config->cert_pem,
        .task_stack = config->stack_size,
    };

    if ((client = esp_mqtt_client_init(&mqtt_cfg)) == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init() failed");
        goto fail;
    }
#if defined(HOMIE_IDF_VERSION4)
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
#endif
    if ((err = esp_mqtt_client_start(client)) != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start(): %s", esp_err_to_name(err));
        goto fail;
    }
    return client;
fail:
    return NULL;
}

esp_err_t homie_mktopic(char *topic, const char *subtopic)
{
    int ret = snprintf(topic, HOMIE_MAX_TOPIC_LEN, "%s/%s/%s",
            config->base_topic, config->client_id, subtopic);

    if (ret < 0 || ret >= HOMIE_MAX_TOPIC_LEN) {
        ESP_LOGE(TAG, "homie_mktopic(): MQTT topic length is too long: ret: %d, HOMIE_MAX_TOPIC_LEN %d",
                ret, HOMIE_MAX_TOPIC_LEN);
        goto fail;
    }
    return ESP_OK;
fail:
    return ESP_FAIL;
}

void homie_subscribe(const char *subtopic)
{
    int msg_id;
    char topic[HOMIE_MAX_TOPIC_LEN];
    ESP_ERROR_CHECK(homie_mktopic(topic, subtopic));

    msg_id = esp_mqtt_client_subscribe(client, topic, 0);
    ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
}

int homie_publish(const char *subtopic, int qos, int retain, const char *payload)
{
    int msg_id = -1;
    char topic[HOMIE_MAX_TOPIC_LEN];
    if (homie_mktopic(topic, subtopic) != ESP_OK) {
        ESP_LOGW(TAG, "homie_mktopic() failed");
        goto fail;
    }
    ESP_LOGD(TAG, "%s\n", topic);
    msg_id = esp_mqtt_client_publish(client, topic, payload, 0, qos, retain);
fail:
    return msg_id;
}

int homie_publishf(const char *subtopic, int qos, int retain, const char *format, ...)
{
    char payload_string[64];
    int msg_id;
    va_list argptr;
    int ret;
    va_start(argptr, format);
    ret = vsnprintf(payload_string, sizeof(payload_string), format, argptr);
    va_end(argptr);
    if (ret < 0 || ret >= sizeof(payload_string)) {
        ESP_LOGW(TAG, "homie_publishf(): too long");
        goto fail;
    }
    msg_id = homie_publish(subtopic, qos, retain, payload_string);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "homie_publish() failed");
        goto fail;
    }
    return msg_id;
fail:
    return -1;
}

void homie_publish_int(const char *subtopic, int qos, int retain, int payload)
{
    char payload_string[16];
    int ret;
    ret = snprintf(payload_string, sizeof(payload_string), "%d", payload);
    if (ret < 0 || ret >= sizeof(payload_string)) {
        ESP_LOGW(TAG, "homie_publish_int(): payload is too long");
    }
    homie_publish(subtopic, qos, retain, payload_string);
}

void homie_publish_bool(const char *subtopic, int qos, int retain, bool payload)
{
    homie_publish(subtopic, qos, retain, payload ? "true" : "false");
}

static int _clamp(int n, int lower, int upper)
{
    return n <= lower ? lower : n >= upper ? upper : n;
}

static int8_t _get_wifi_rssi()
{
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK)
    {
        ESP_LOGW(TAG, "esp_wifi_sta_get_ap_info() failed");
        return 0;
    }
    else
    {
        return info.rssi;
    }
}

static esp_err_t _get_ip(char *ip_string, size_t len)
{
    int ret;
    tcpip_adapter_ip_info_t ip;
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip);

    ret = snprintf(
        ip_string,
        len,
        "%u.%u.%u.%u",
        (ip.ip.addr & 0x000000ff),
        (ip.ip.addr & 0x0000ff00) >> 8,
        (ip.ip.addr & 0x00ff0000) >> 16,
        (ip.ip.addr & 0xff000000) >> 24);
    if (ret < 0 || ret >= len) {
        ESP_LOGE(TAG, "_get_ip(): ip_string too short");
        goto fail;
    }
    return ESP_OK;
fail:
    return ESP_FAIL;
}

static esp_err_t _get_mac(char *mac_string, size_t len, bool sep)
{
    // NB: This is the base mac of the device. The actual wifi and eth MAC addresses
    //     will be assigned as offsets from this.

    uint8_t mac[6];
    int ret;
    esp_efuse_mac_get_default(mac);

    if (sep)
        ret = snprintf(mac_string, len, "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    else
        ret = snprintf(mac_string, len, "%x%x%x%x%x%x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (ret < 0 || ret >= len) {
        ESP_LOGE(TAG, "_get_mac(): mac_string too short");
        goto fail;
    }
    return ESP_OK;
fail:
    return ESP_FAIL;
}

static void homie_connected()
{
    char mac_address[] = "00:00:00:00:00:00";
    char ip_address[16];
    int msg_id;
    ESP_ERROR_CHECK(_get_mac(mac_address, sizeof(mac_address), true));
    ESP_ERROR_CHECK(_get_ip(ip_address, sizeof(ip_address)));

    homie_publish("$homie", QOS_1, RETAINED, "2.0.1");
    homie_publish("$online", QOS_1, RETAINED, "true");
    homie_publish("$name", QOS_1, RETAINED, config->device_name);
    homie_publish("$localip", QOS_1, RETAINED, ip_address);
    homie_publish("$mac", QOS_1, RETAINED, mac_address);
    homie_publish("$fw/name", QOS_1, RETAINED, config->firmware_name);
    homie_publish("$fw/version", QOS_1, RETAINED, config->firmware_version);
    homie_publish("$nodes", QOS_1, RETAINED, ""); // FIXME: needs to be extendible
    homie_publish("$implementation", QOS_1, RETAINED, "esp32-idf");
    homie_publish("$implementation/version", QOS_1, RETAINED, "dev");

    homie_publish("$stats", QOS_1, RETAINED, "uptime,rssi,signal,freeheap"); // FIXME: needs to be extendible
    homie_publish("$stats/interval", QOS_1, RETAINED, "30");
    homie_publish_bool("$implementation/ota/enabled", QOS_1, RETAINED, config->ota_enabled);

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition != NULL)
    {
        msg_id = homie_publishf("$implementation/ota/running",
                                QOS_1, RETAINED, "0x%08x", running_partition->address);
        if (msg_id < 0) {
            ESP_LOGE(TAG, "homie_connected(): homie_publishf() failed");
            goto fail;
        }
    }
    else
    {
        msg_id = homie_publishf("$implementation/ota/running",
                                QOS_1, RETAINED, "NULL");
        if (msg_id < 0) {
            ESP_LOGE(TAG, "homie_connected(): homie_publishf() failed");
            goto fail;
        }
    }

    const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
    if (boot_partition != NULL)
    {
        msg_id = homie_publishf("$implementation/ota/boot",
                                QOS_1, RETAINED, "0x%08x", boot_partition->address);
        if (msg_id < 0) {
            ESP_LOGE(TAG, "homie_connected(): homie_publishf() failed");
            goto fail;
        }
    }
    else
    {
        msg_id = homie_publishf("$implementation/ota/boot",
                                QOS_1, RETAINED, "NULL");
        if (msg_id < 0) {
            ESP_LOGE(TAG, "homie_connected(): homie_publishf() failed");
            goto fail;
        }
    }

    homie_subscribe("$implementation/reboot");
    homie_subscribe("$implementation/logging");
    if (config->ota_enabled)
        homie_subscribe("$implementation/ota/url/#");
    xEventGroupClearBits(*mqtt_group, HOMIE_MQTT_STATUS_UPDATE_REQUIRED);
    ESP_LOGI(TAG, "device status has been updated");
fail:
    return;
}

static void homie_task(void *pvParameter)
{
    while (1)
    {
        if ((xEventGroupGetBits(*mqtt_group) & HOMIE_MQTT_STATUS_UPDATE_REQUIRED) > 0) {
            homie_connected();
        }
        homie_publish_int("$stats/uptime", QOS_1, RETAINED, esp_timer_get_time() / 1000000);
        int rssi = _get_wifi_rssi();
        homie_publish_int("$stats/rssi", QOS_1, RETAINED, rssi);

        // Translate to "signal" percentage, assuming RSSI range of (-100,-50)
        homie_publish_int("$stats/signal", QOS_1, RETAINED, _clamp((rssi + 100) * 2, 0, 100));

        homie_publish_int("$stats/freeheap", QOS_1, RETAINED, esp_get_free_heap_size());
        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}

esp_mqtt_client_handle_t homie_init(homie_config_t *passed_config)
{
    config = passed_config;
    mqtt_group = config->event_group;
    if (!config) {
        ESP_LOGE(TAG, "invalid argument");
        goto fail;
    }
    if (!config->event_group) {
        ESP_LOGE(TAG, "invalid argument: event_group");
        goto fail;
    }

    // If client_id is blank, generate one based off the mac
    if (!config->client_id[0])
        _get_mac(config->client_id, HOMIE_MAX_CLIENT_ID_LEN, false);

    if ((client = mqtt_app_start()) == NULL) {
        ESP_LOGE(TAG, "mqtt_app_start(): failed");
        goto fail;
    }
    if (xTaskCreate(&homie_task, "homie_task", configMINIMAL_STACK_SIZE * 10, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate() failed");
        goto fail;
    }
    return client;
fail:
    return NULL;
}
