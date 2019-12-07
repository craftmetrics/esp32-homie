/*
 *   MIT License
 *
 *   Copyright (c) 2018 Craft Metrics Inc.
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

#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#if defined(ESP_IDF_VERSION_MAJOR) // defined in esp-idf 4.x, but not 3.x
#define HOMIE_IDF_VERSION4
#else
#define HOMIE_IDF_VERSION3
#endif

#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#if defined(HOMIE_IDF_VERSION4)
#include <esp_event.h>
#else
#include <esp_event_loop.h>
#endif

#include "homie.h"

#if defined(HOMIE_IDF_VERSION4)
#include "task_ota.h"
#else
#include "task_ota_3_2.h"
#endif

#define QOS_0 (0)
#define QOS_1 (1)
#define QOS_2 (2)
#define RETAINED (1)
#define NOT_RETAINED (0)
#define AUTO_LENGTH (0)

#if defined(CONFIG_IDF_TARGET_ESP32S2BETA)
#define CHIP_NAME "ESP32-S2 Beta"
#else
#define CHIP_NAME "ESP32"
#endif

#define FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(f) do { \
        if (f <= 0) { \
            goto fail; \
        } \
    } while (0)

static const char *TAG = "HOMIE";
static const char homie_node_name[] = "esp";
static esp_mqtt_client_handle_t client;
static homie_config_t *config;
static EventGroupHandle_t *mqtt_group;

static esp_err_t homie_connected();

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
    if (homie_publish("log", QOS_1, NOT_RETAINED, buf) <= 0) {
        ESP_LOGW(TAG, "_homie_logger(): homie_publish() failed");
    }
    return vprintf(str, l);
}

static void homie_handle_mqtt_event(esp_mqtt_event_handle_t event)
{
    esp_err_t err;
    ESP_LOGD(TAG, "topic: %s length: %d data length: %d", event->topic, event->topic_len, event->data_len);

    // Check if it is reboot command
    char topic[HOMIE_MAX_TOPIC_LEN];
    ESP_ERROR_CHECK(homie_mktopic(topic, "esp/reboot/set", sizeof(topic)));
    if ((strncmp(topic, event->topic, event->topic_len) == 0) && (strncmp("reboot", event->data, event->data_len) == 0))
    {
        if (config->reboot_enabled) {
            ESP_LOGI(TAG, "Rebooting...");
            if (homie_publish("esp/reboot", QOS_1, RETAINED, "rebooting") == 0) {
                ESP_LOGE(TAG, "homie_publish() failed");
            }
            esp_restart();
            while (1) {}
        }
        goto finish;
    }

    // Check if it is enable remote console
    ESP_ERROR_CHECK(homie_mktopic(topic, "$implementation/logging", sizeof(topic)));
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
        goto finish;
    }

    // Check if it is a OTA update
    ESP_ERROR_CHECK(homie_mktopic(topic, "esp/ota/set", sizeof(topic)));
    if (config->ota_enabled && _starts_with(topic, event->topic, event->topic_len) && strncmp("run", event->data, event->data_len) == 0)
    {
        if (homie_publish("esp/ota", QOS_1, RETAINED, "running") == 0) {
            ESP_LOGW(TAG, "failed to set esp/ota to `running`");
        }
        err = do_ota(config->ota_uri, config->cert_pem);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "do_ota() failed");
        }
        if (homie_publish("esp/ota", QOS_1, RETAINED, "enabled") == 0) {
            ESP_LOGW(TAG, "failed to set esp/ota to `enabled`");
        }
        goto finish;
    }

    // Call the application's handler
    ESP_ERROR_CHECK(homie_mktopic(topic, "", sizeof(topic)));
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
finish:
    return;
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
    char lwt_topic[HOMIE_MAX_TOPIC_LEN];
    esp_err_t err;

    ESP_LOGI(TAG, "URI: `%s`", config->mqtt_uri);
    ESP_LOGI(TAG, "client_id: `%s`", config->client_id);
    ESP_LOGI(TAG, "user name: `%s`", config->mqtt_username);
    ESP_LOGI(TAG, "base_topic: `%s`", config->base_topic);
    ESP_LOGI(TAG, "stack_size: %d", config->stack_size);

    ESP_ERROR_CHECK(homie_mktopic(lwt_topic, "$online", sizeof(lwt_topic)));

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

esp_err_t homie_mktopic(char *topic, const char *subtopic, const size_t topic_size)
{
    int ret = snprintf(topic, topic_size, "%s/%s/%s",
            config->base_topic, config->client_id, subtopic);

    if (ret < 0 || ret >= topic_size) {
        ESP_LOGE(TAG, "homie_mktopic(): topic is too short: ret: %d, topic_size: %d",
                ret, topic_size);
        goto fail;
    }
    return ESP_OK;
fail:
    return ESP_FAIL;
}

int homie_subscribe(const char *subtopic, const int qos)
{
    int msg_id;
    char topic[HOMIE_MAX_TOPIC_LEN];
    if (qos < 0 || qos > 2) {
        ESP_LOGE(TAG, "invalid QoS: %d", qos);
        goto fail;
    }
    ESP_ERROR_CHECK(homie_mktopic(topic, subtopic, sizeof(topic)));

    msg_id = esp_mqtt_client_subscribe(client, topic, qos);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "esp_mqtt_client_subscribe() failed: topic: `%s`", topic);
        goto fail;
    }
    ESP_LOGI(TAG, "sent subscribe successful, topic: `%s` msg_id=%d", topic, msg_id);
    return msg_id;
fail:
    return -1;
}

int homie_publish(const char *subtopic, int qos, int retain, const char *payload)
{
    int msg_id = -1;
    char topic[HOMIE_MAX_TOPIC_LEN];
    if (homie_mktopic(topic, subtopic, sizeof(topic)) != ESP_OK) {
        ESP_LOGW(TAG, "homie_mktopic() failed");
        goto fail;
    }
    ESP_LOGD(TAG, "topic `%s` payload: `%s`", topic, payload);
    msg_id = esp_mqtt_client_publish(client, topic, payload, AUTO_LENGTH, qos, retain);
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

int homie_publish_int(const char *subtopic, int qos, int retain, int payload)
{
    char payload_string[16];
    int msd_id;
    int ret;
    ret = snprintf(payload_string, sizeof(payload_string), "%d", payload);
    if (ret < 0 || ret >= sizeof(payload_string)) {
        ESP_LOGE(TAG, "homie_publish_int(): payload is too long");
        goto fail;
    }
    msd_id = homie_publish(subtopic, qos, retain, payload_string);
    if (msd_id < 0) {
        ESP_LOGE(TAG, "homie_publish_int(): homie_publish() failed");
        goto fail;
    }
    return msd_id;
fail:
    return -1;
}

int homie_publish_bool(const char *subtopic, int qos, int retain, bool payload)
{
    return homie_publish(subtopic, qos, retain, payload ? "true" : "false");
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

static esp_err_t homie_connected()
{
    char mac_address[] = "00:00:00:00:00:00";
    char ip_address[16];
    esp_chip_info_t chip_info;

    ESP_ERROR_CHECK(_get_mac(mac_address, sizeof(mac_address), true));
    ESP_ERROR_CHECK(_get_ip(ip_address, sizeof(ip_address)));
    esp_chip_info(&chip_info);

    int ret;
    char nodes[HOMIE_MAX_NODE_LISTS_LEN + sizeof(homie_node_name)];

    if (strnlen(config->node_lists, HOMIE_MAX_NODE_LISTS_LEN) > 0) {
        ret = snprintf(nodes, sizeof(nodes), "%s,%s", homie_node_name, config->node_lists);
        if (ret < 0 || ret >= sizeof(nodes)) {
            ESP_LOGE(TAG, "homie_connected(): node_lists too long");
            goto fail;
        }
    } else {
        if (strlcpy(nodes, homie_node_name, sizeof(homie_node_name)) >= sizeof(nodes)) {
            ESP_LOGE(TAG, "homie_connected(): homie_node_name too long");
            goto fail;
        }
    }

    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("$state", QOS_1, RETAINED, "init"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("$homie", QOS_1, RETAINED, "4.0.1"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("$name", QOS_1, RETAINED, config->device_name));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("$nodes", QOS_1, RETAINED, nodes));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/$name", QOS_1, RETAINED, CHIP_NAME));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publishf("esp/$type", QOS_1, RETAINED, "rev: %d", chip_info.revision));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/$properties", QOS_1, RETAINED, "uptime,rssi,signal,freeheap,mac,ip,sdk,firmware,firmware_version,ota,reboot"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/uptime/$name", QOS_1, RETAINED, "Uptime since boot"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/uptime/$datatype", QOS_1, RETAINED, "integer"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/rssi/$name", QOS_1, RETAINED, "WiFi RSSI"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/rssi/$datatype", QOS_1, RETAINED, "integer"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/signal/$name", QOS_1, RETAINED, "WiFi RSSI in signal strength"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/signal/$datatype", QOS_1, RETAINED, "integer"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/freeheap/$name", QOS_1, RETAINED, "Free heap memory"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/freeheap/$datatype", QOS_1, RETAINED, "integer"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/mac/$name", QOS_1, RETAINED, "MAC address"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/mac/$datatype", QOS_1, RETAINED, "string"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/mac", QOS_1, RETAINED, mac_address));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ip/$name", QOS_1, RETAINED, "IP address"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ip/$datatype", QOS_1, RETAINED, "string"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ip", QOS_1, RETAINED, ip_address));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/sdk/$name", QOS_1, RETAINED, "SDK version"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/sdk/$datatype", QOS_1, RETAINED, "string"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/sdk", QOS_1, RETAINED, esp_get_idf_version()));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/firmware/$name", QOS_1, RETAINED, "Firmware name"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/firmware/$datatype", QOS_1, RETAINED, "string"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/firmware", QOS_1, RETAINED, config->firmware_name));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/firmware_version/$name", QOS_1, RETAINED, "Firmware version"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/firmware_version/$datatype", QOS_1, RETAINED, "string"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/firmware_version", QOS_1, RETAINED, config->firmware_version));

    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ota/$name", QOS_1, RETAINED, "OTA state"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ota/$datatype", QOS_1, RETAINED, "enum"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ota/$settable", QOS_1, RETAINED, config->ota_enabled ? "true" : "false"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ota/$format", QOS_1, RETAINED, "idle,disabled,running,run"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ota", QOS_1, RETAINED, config->ota_enabled ? "idle" : "disabled"));
    if (config->ota_enabled && homie_subscribe("esp/ota/set", QOS_1) < 0) {
        ESP_LOGE(TAG, "failed to subscribe esp/ota/set");
    }

    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/reboot/$name", QOS_1, RETAINED, "Reboot state"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/reboot/$datatype", QOS_1, RETAINED, "enum"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/reboot/$settable", QOS_1, RETAINED, config->reboot_enabled ? "true" : "false"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/reboot/$format", QOS_1, RETAINED, "disabled,enabled,rebooting,reboot"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/reboot", QOS_1, RETAINED, config->reboot_enabled ? "enabled" : "disabled"));
    if (config->reboot_enabled && homie_subscribe("esp/reboot/set", QOS_1) < 0) {
        ESP_LOGE(TAG, "failed to subscribe esp/reboot/set");
    }

    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("$state", QOS_1, RETAINED, "ready"));
    if (config->init_handler != NULL) {
        config->init_handler();
    }
    xEventGroupClearBits(*mqtt_group, HOMIE_MQTT_STATUS_UPDATE_REQUIRED);
    ESP_LOGI(TAG, "device status has been updated");
    return ESP_OK;
fail:
    return ESP_FAIL;
}

/**
 * @brief Create MQTT topic path to an attribute
 *
 * @param[out] buf String buffer to keep the path
 * @param[in] len Size of buf
 * @param[in] node Node name (ignored if the Homie version does not support it)
 * @param[in] attr Attribute name
 * @return -1 on error, The number of characters printed (see snprintf(3)).
 */
static int topic_path_to_node_attribute(char *buf, const size_t len, const char *node, const char *attr)
{
    int ret;

    /* homie/24ac45ac44/esp/freeheap -> value */
    char format[] = "%s/%s";
    ret = snprintf(buf, len, format, node, attr);
    if (ret < 0 || ret >= len) {
        ESP_LOGE(TAG, "topic_path_to_node_attribute(): buf is too small");
        goto fail;
    }
    return ret;
fail:
    return -1;
}

static void homie_task(void *pvParameter)
{
    int msg_id;
    int rssi;
    char buf[32 + 1];

    while (1)
    {
        rssi = _get_wifi_rssi();
        if ((xEventGroupGetBits(*mqtt_group) & HOMIE_MQTT_STATUS_UPDATE_REQUIRED) > 0) {
            if (homie_connected() != ESP_OK) {
                ESP_LOGW(TAG, "homie_task(): homie_connected() failed");
            }
        }

        if (topic_path_to_node_attribute(buf, sizeof(buf), homie_node_name, "uptime") < 0) {
            ESP_LOGW(TAG, "homie_task(): topic_path_to_node_attribute() failed: uptime");
        } else {
            msg_id = homie_publish_int(buf, QOS_1, RETAINED, esp_timer_get_time() / 1000000);
            if (msg_id < 0) {
                ESP_LOGW(TAG, "homie_task(): failed to publish uptime");
            }
        }

        if (topic_path_to_node_attribute(buf, sizeof(buf), homie_node_name, "rssi") < 0) {
            ESP_LOGW(TAG, "homie_task(): topic_path_to_node_attribute() failed: rssi");
        } else {
            msg_id = homie_publish_int(buf, QOS_1, RETAINED, rssi);
            if (msg_id < 0) {
                ESP_LOGW(TAG, "homie_task(): failed to publish rssi");
            }
        }

        if (topic_path_to_node_attribute(buf, sizeof(buf), homie_node_name, "signal") < 0) {
            ESP_LOGW(TAG, "homie_task(): topic_path_to_node_attribute() failed: signal");
        } else {

            /* Translate to "signal" percentage, assuming RSSI range of
             * (-100,-50)
             */
            msg_id = homie_publish_int(buf, QOS_1, RETAINED, _clamp((rssi + 100) * 2, 0, 100));
            if (msg_id < 0) {
                ESP_LOGW(TAG, "homie_task(): failed to publish signal");
            }
        }

        if (topic_path_to_node_attribute(buf, sizeof(buf), homie_node_name, "freeheap") < 0) {
            ESP_LOGW(TAG, "homie_task(): topic_path_to_node_attribute() failed: freeheap");
        } else {
            msg_id = homie_publish_int(buf, QOS_1, RETAINED, esp_get_free_heap_size());
            if (msg_id < 0) {
                ESP_LOGW(TAG, "homie_task(): failed to publish freeheap");
            }
        }
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
