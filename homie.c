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

#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#include "esp_idf_lib_helpers.h"
#if HELPER_TARGET_VERSION >= HELPER_TARGET_VERSION_ESP32_V4
#include <esp_event.h>
#else
#include <esp_event_loop.h>
#endif

#if HELPER_TARGET_IS_ESP8266 > 0
#include <esp_timer.h>
#endif

#include "homie.h"
#if HELPER_TARGET_IS_ESP32
#if HELPER_TARGET_VERSION >= HELPER_TARGET_VERSION_ESP32_V4
#include "task_ota.h"
#else
#include "task_ota_3_2.h"
#endif
#endif

#if HELPER_TARGET_IS_ESP8266
#include "task_ota_8266.h"
#endif

#define QOS_0 (0)
#define QOS_1 (1)
#define QOS_2 (2)
#define RETAINED (1)
#define NOT_RETAINED (0)
#define AUTO_LENGTH (0)
#define NOT_CLEAR_ON_EXIT       pdFALSE
#define CLEAR_ON_EXIT           pdTRUE
#define NOT_WAIT_FOR_ALL_BITS   pdFALSE
#define WAIT_FOR_ALL_BITS       pdTRUE

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
SemaphoreHandle_t mutex_ota;

static esp_err_t homie_connected();
char client_id[HOMIE_MAX_MQTT_CLIENT_ID_LEN];

static void handle_command(const char *topic, const char *data)
{
    esp_err_t err;
    char topic_reboot[HOMIE_MAX_MQTT_TOPIC_LEN];
    char topic_ota[HOMIE_MAX_MQTT_TOPIC_LEN];
    char command_reboot[] = "reboot";
    char command_ota[] = "run";

    ESP_ERROR_CHECK(homie_mktopic(topic_reboot, "esp/reboot/set", sizeof(topic_reboot)));
    ESP_ERROR_CHECK(homie_mktopic(topic_ota, "esp/ota/set", sizeof(topic_ota)));

    if (topic == NULL || data == NULL) {
        ESP_LOGE(TAG, "topic or data is NULL");
        goto fail;
    }
    if (strncmp(topic_ota, topic, HOMIE_MAX_MQTT_TOPIC_LEN) == 0) {
        if (!config->ota_enabled) {
            goto finish;
        }
        if (strncmp(command_ota, data, HOMIE_MAX_MQTT_DATA_LEN) == 0) {
            if (homie_publish("esp/ota", QOS_1, RETAINED, command_ota) == 0) {
                ESP_LOGW(TAG, "failed to set esp/ota to `run`");
            }

            /* clear retained message.
             *
             * the spec does not require this, but some implementations set
             * retained MQTT message. if the message is retained, the device
             * keep executing same command. another workaround would be
             * ignoring command messages with retained flag set. but the MQTT
             * library does not provide a function to access retained flag.
             */
            if (homie_remove_retained("esp/ota/set") <= 0) {
                ESP_LOGE(TAG, "homie_remove_retained() failed");
            }
            if (homie_publish("esp/ota", QOS_1, RETAINED, "running") == 0) {
                ESP_LOGW(TAG, "failed to set esp/ota to `running`");
            }

            /* start_ota() never return when OTA is performed and successful */
            ESP_LOGD(TAG, "Starting OTA");
            err = start_ota(config->http_config);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "start_ota() failed");
            }
            if (homie_publish("esp/ota", QOS_1, RETAINED, "idle") == 0) {
                ESP_LOGW(TAG, "failed to set esp/ota to `idle`");
            }
            ESP_LOGD(TAG, "OTA finished");
            goto finish;
        } else if (*data == '\0') {

            /* ignore NULL command */
            goto finish;
        } else {
            ESP_LOGW(TAG, "Unknown command for command topic: %s data: `%s`", topic_ota, data);
        }
    } else if (strncmp(topic_reboot, topic, HOMIE_MAX_MQTT_TOPIC_LEN) == 0) {
        if (!config->reboot_enabled) {
            goto finish;
        }
        if (strncmp(command_reboot, data, HOMIE_MAX_MQTT_DATA_LEN) == 0) {

            if (homie_publish("esp/reboot", QOS_1, RETAINED, "reboot") == 0) {
                ESP_LOGE(TAG, "homie_publish() failed");
            }
            if (homie_remove_retained("esp/reboot/set") <= 0) {
                ESP_LOGE(TAG, "homie_remove_retained() failed");
            }
            if (homie_publish("esp/reboot", QOS_1, RETAINED, "rebooting") == 0) {
                ESP_LOGE(TAG, "homie_publish() failed");
            }
            ESP_LOGI(TAG, "Rebooting...");
            vTaskDelay(1000 * 10 / portTICK_PERIOD_MS);
            esp_restart();
            while (1) {}

            /* NOT REACHED */
        } else if (*data == '\0') {

            /* ignore NULL command */
            goto finish;
        } else {
            ESP_LOGW(TAG, "Unknown command for command topic: %s data: `%s`", topic_reboot, data);
            goto fail;
        }
    } else {
        ESP_LOGW(TAG, "Unknown topic: `%s`", topic);
    }
finish:
fail:
    return;
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_err_t err = ESP_FAIL;
    static char *topic;
    static char *data_text;

    switch (event->event_id)
    {
#if HELPER_TARGET_IS_ESP32 > 0
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
        break;
#endif
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        xEventGroupSetBits(*config->event_group, HOMIE_MQTT_CONNECTED_BIT);
        xEventGroupSetBits(*config->event_group, HOMIE_MQTT_STATUS_UPDATE_REQUIRED);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        xEventGroupClearBits(*config->event_group, HOMIE_MQTT_CONNECTED_BIT);
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

        /* the first event of data */
        if (event->current_data_offset == 0) {
            ESP_LOGD(TAG, "topic_len: %d total_data_len: %d",
                    event->topic_len, event->total_data_len);

            /* the first event that contains topic.
             *
             * event->topic and event->data are not null-terminated C
             * string. allocate memory for topic_len + extra 1 byte for
             * '\0'.
             */
            topic = malloc(event->topic_len + 1);
            if (topic == NULL) {
                ESP_LOGE(TAG, "failed to malloc() on topic");
                break;
            }
            memset(topic, 0, 1);

            /* ignore return value of strlcpy(). it is almost always more
             * than event->topic_len + 1, as event->topic is not C string.
             */
            strlcpy(topic, event->topic, event->topic_len + 1);
            ESP_LOGD(TAG, "topic: `%s`", topic);

            data_text = malloc(event->total_data_len + 1);
            if (data_text == NULL) {
                ESP_LOGE(TAG, "failed to malloc(): topic `%s`",
                        topic);
                goto free;
            }
            memset(data_text, 0, 1);

        }

        /* the first and the rest of events */
        if (topic == NULL || data_text == NULL) {

            /* when something went wrong in parsing the first event,
             * ignore the rest of the events
             */
            goto free;
        }

        if (event->total_data_len > 0) {
            strlcat(data_text, event->data, event->data_len + 1);
        }

        /* the last event */
        if (event->current_data_offset + event->data_len >= event->total_data_len) {

            if (topic == NULL || data_text == NULL) {
                goto free;
            }
            ESP_LOGD(TAG, "topic: `%s` data: `%s`", topic, data_text);
            handle_command(topic, data_text);
            ESP_LOGD(TAG, "handle_command() ends");
        }
free:
        free(topic);
        topic = NULL;
        free(data_text);
        data_text = NULL;
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    }
    if (config->mqtt_handler != NULL) {
        err = config->mqtt_handler(event);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mqtt_handler failed: event_id: %d err: %s",
                    event->event_id, esp_err_to_name(err));
            goto fail;
        }
    }
    err = ESP_OK;
fail:
    return err;
}

#if HELPER_TARGET_IS_ESP32 > 0 && HELPER_TARGET_VERSION >= HELPER_TARGET_VERSION_ESP32_V4
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}
#endif

static esp_mqtt_client_handle_t mqtt_app_start(void)
{
    char topic[HOMIE_MAX_MQTT_TOPIC_LEN];
    char lwt_topic[HOMIE_MAX_MQTT_TOPIC_LEN];
    esp_err_t err;

    ESP_ERROR_CHECK(homie_mktopic(lwt_topic, "$state", sizeof(lwt_topic)));
    ESP_LOGD(TAG, "lwt_topic: %s", lwt_topic);

#if (HELPER_TARGET_IS_ESP32 > 0 && HELPER_TARGET_VERSION < HELPER_TARGET_VERSION_ESP32_V4) || HELPER_TARGET_IS_ESP8266 > 0
    config->mqtt_config.event_handle = mqtt_event_handler_cb;
#endif
    /* fixed configurations */
    config->mqtt_config.lwt_topic = lwt_topic;
    config->mqtt_config.lwt_msg = "lost";
    config->mqtt_config.lwt_qos = 1;
    config->mqtt_config.lwt_retain = 1;

    ESP_ERROR_CHECK(homie_mktopic(topic, "#", sizeof(topic)));
    ESP_LOGI(TAG, "MQTT URI: `%s`", config->mqtt_config.uri);
    ESP_LOGI(TAG, "MQTT topic: `%s`", topic);
    ESP_LOGD(TAG, "MQTT user name: `%s`", config->mqtt_config.username);
    ESP_LOGD(TAG, "MQTT client ID: `%s`", client_id);
    ESP_LOGD(TAG, "device_name: %s", config->device_name);
    ESP_LOGD(TAG, "MQTT base topic: `%s`", config->base_topic);
    ESP_LOGD(TAG, "Firmware name: `%s`", config->firmware_name);
    ESP_LOGD(TAG, "Firmware version: `%s`", config->firmware_version);
    ESP_LOGI(TAG, "Reboot enabled: %s", config->reboot_enabled ? "true" : "false");
    ESP_LOGI(TAG, "OTA enabled: %s", config->ota_enabled ? "true" : "false");
    if (config->ota_enabled) {
        ESP_LOGI(TAG, "OTA firmware URL: `%s`", config->http_config.url);
    }
    ESP_LOGD(TAG, "Stack size of MQTT task in byte: %d", config->mqtt_config.task_stack);
    ESP_LOGD(TAG, "node_lists: `%s`", config->node_lists);
    if ((client = esp_mqtt_client_init(&config->mqtt_config)) == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init() failed");
        goto fail;
    }
#if HELPER_TARGET_IS_ESP32 > 0 && HELPER_TARGET_VERSION >= HELPER_TARGET_VERSION_ESP32_V4
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
#endif

    ESP_LOGI(TAG, "Running esp_mqtt_client_start()");
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
    int ret;
    if (!config->base_topic) {
        ESP_LOGE(TAG, "base_topic must be set in homie_config");
        goto fail;
    }
    ret = snprintf(topic, topic_size, "%s/%s",
            config->base_topic, subtopic);

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
    char topic[HOMIE_MAX_MQTT_TOPIC_LEN];
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
    ESP_LOGI(TAG, "successfully subscribed to topic: `%s` msg_id=%d", topic, msg_id);
    return msg_id;
fail:
    return -1;
}

int homie_publish(const char *subtopic, int qos, int retain, const char *payload)
{
    int msg_id = -1;
    char topic[HOMIE_MAX_MQTT_TOPIC_LEN];
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

esp_err_t homie_get_mac(char *mac_string, size_t len, bool sep)
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
        ret = snprintf(mac_string, len, "%02X%02X%02X%02X%02X%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (ret < 0 || ret >= len) {
        ESP_LOGE(TAG, "homie_get_mac(): mac_string too short");
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

    ESP_ERROR_CHECK(homie_get_mac(mac_address, sizeof(mac_address), true));
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
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/$properties", QOS_1, RETAINED, "uptime,rssi,signal,freeheap,mac,ip,sdk,firmware,firmware-version,ota,reboot"));
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
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/firmware-version/$name", QOS_1, RETAINED, "Firmware version"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/firmware-version/$datatype", QOS_1, RETAINED, "string"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/firmware-version", QOS_1, RETAINED, config->firmware_version));

    /* topics that accept commands */
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ota/$name", QOS_1, RETAINED, "OTA state"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ota/$datatype", QOS_1, RETAINED, "enum"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ota/$settable", QOS_1, RETAINED, config->ota_enabled ? "true" : "false"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ota/$retained", QOS_1, RETAINED, "false")); // command message should NOT be retained
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ota/$format", QOS_1, RETAINED, "idle,disabled,running,run"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/ota", QOS_1, RETAINED, config->ota_enabled ? "idle" : "disabled"));

    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/reboot/$name", QOS_1, RETAINED, "Reboot state"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/reboot/$datatype", QOS_1, RETAINED, "enum"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/reboot/$settable", QOS_1, RETAINED, config->reboot_enabled ? "true" : "false"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/reboot/$retained", QOS_1, RETAINED, "false")); // command message should NOT be retained
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/reboot/$format", QOS_1, RETAINED, "disabled,enabled,rebooting,reboot"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("esp/reboot", QOS_1, RETAINED, config->reboot_enabled ? "enabled" : "disabled"));

    /* XXX override the value with NOT_RETAINED to clear RETAINED message
     * before subscribing */
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_remove_retained("esp/reboot/set"));
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_remove_retained("esp/ota/set"));

    if (config->reboot_enabled && homie_subscribe("esp/reboot/set", QOS_1) < 0) {
        ESP_LOGE(TAG, "failed to subscribe esp/reboot/set");
    }
    if (config->ota_enabled && homie_subscribe("esp/ota/set", QOS_1) < 0) {
        ESP_LOGE(TAG, "failed to subscribe esp/ota/set");
    }
    if (config->init_handler != NULL) {
        config->init_handler();
    }
    FAIL_IF_LESS_THAN_OR_EQUAL_ZERO(homie_publish("$state", QOS_1, RETAINED, "ready"));
    xEventGroupClearBits(*config->event_group, HOMIE_MQTT_STATUS_UPDATE_REQUIRED);
    ESP_LOGI(TAG, "device status has been updated");
    return ESP_OK;
fail:
    return ESP_FAIL;
}

int homie_remove_retained(const char *topic)
{
    return homie_publish(topic, QOS_1, RETAINED, "");
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
    char topic[HOMIE_MAX_MQTT_TOPIC_LEN];
    EventBits_t bit;

    while (1) {
        ESP_LOGD(TAG, "Waiting for HOMIE_MQTT_CONNECTED_BIT to be set");
        bit = xEventGroupWaitBits(*config->event_group,
                HOMIE_MQTT_CONNECTED_BIT,
                NOT_CLEAR_ON_EXIT,
                NOT_WAIT_FOR_ALL_BITS,
                1000 / portTICK_PERIOD_MS);
        if ((bit & HOMIE_MQTT_CONNECTED_BIT) == HOMIE_MQTT_CONNECTED_BIT) {
            break;
        }
    }
    ESP_LOGI(TAG, "Starting the loop in homie_task()");
    while (1)
    {
        rssi = _get_wifi_rssi();
        if ((xEventGroupGetBits(*config->event_group) & HOMIE_MQTT_STATUS_UPDATE_REQUIRED) > 0) {
            if (homie_connected() != ESP_OK) {
                ESP_LOGW(TAG, "homie_task(): homie_connected() failed");
            }
        }

        if (topic_path_to_node_attribute(topic, sizeof(topic), homie_node_name, "uptime") < 0) {
            ESP_LOGW(TAG, "homie_task(): topic_path_to_node_attribute() failed: uptime");
        } else {
            msg_id = homie_publish_int(topic, QOS_1, RETAINED, esp_timer_get_time() / 1000000);
            if (msg_id < 0) {
                ESP_LOGW(TAG, "homie_task(): failed to publish uptime");
            }
        }

        if (topic_path_to_node_attribute(topic, sizeof(topic), homie_node_name, "rssi") < 0) {
            ESP_LOGW(TAG, "homie_task(): topic_path_to_node_attribute() failed: rssi");
        } else {
            msg_id = homie_publish_int(topic, QOS_1, RETAINED, rssi);
            if (msg_id < 0) {
                ESP_LOGW(TAG, "homie_task(): failed to publish rssi");
            }
        }

        if (topic_path_to_node_attribute(topic, sizeof(topic), homie_node_name, "signal") < 0) {
            ESP_LOGW(TAG, "homie_task(): topic_path_to_node_attribute() failed: signal");
        } else {

            /* Translate to "signal" percentage, assuming RSSI range of
             * (-100,-50)
             */
            msg_id = homie_publish_int(topic, QOS_1, RETAINED, _clamp((rssi + 100) * 2, 0, 100));
            if (msg_id < 0) {
                ESP_LOGW(TAG, "homie_task(): failed to publish signal");
            }
        }

        if (topic_path_to_node_attribute(topic, sizeof(topic), homie_node_name, "freeheap") < 0) {
            ESP_LOGW(TAG, "homie_task(): topic_path_to_node_attribute() failed: freeheap");
        } else {
            msg_id = homie_publish_int(topic, QOS_1, RETAINED, esp_get_free_heap_size());
            if (msg_id < 0) {
                ESP_LOGW(TAG, "homie_task(): failed to publish freeheap");
            }
        }
        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}

esp_mqtt_client_handle_t homie_run()
{
    if ((client = mqtt_app_start()) == NULL) {
        ESP_LOGE(TAG, "mqtt_app_start(): failed");
        goto fail;
    }
    ESP_LOGI(TAG, "Starting homie_task");
    if (xTaskCreate(&homie_task, "homie_task", configMINIMAL_STACK_SIZE * 10, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate() failed");
        goto fail;
    }
    return client;
fail:
    return NULL;
}

esp_err_t homie_init(homie_config_t *homie_config)
{
    esp_err_t err = ESP_FAIL;

    if (!homie_config) {
        err = ESP_ERR_INVALID_ARG;
        ESP_LOGE(TAG, "invalid config");
        goto fail;
    }
    if (!homie_config->event_group) {
        err = ESP_ERR_INVALID_ARG;
        ESP_LOGE(TAG, "invalid argument: event_group");
        goto fail;
    }
    config = homie_config;

    mutex_ota = xSemaphoreCreateMutex();
    if (mutex_ota == NULL) {
        err = ESP_FAIL;
        ESP_LOGE(TAG, "xSemaphoreCreateMutex()");
        goto fail;
    }
    err = ESP_OK;
fail:
    return err;
}
