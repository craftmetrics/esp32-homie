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

#ifndef CM_ESP32_HOMIE_H
#define CM_ESP32_HOMIE_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <mqtt_client.h>

#define HOMIE_MAX_MQTT_TOPIC_LEN            (CONFIG_HOMIE_MAX_MQTT_TOPIC_LEN)
#define HOMIE_MAX_MQTT_DATA_LEN             (CONFIG_HOMIE_MAX_MQTT_DATA_LEN)
#define HOMIE_MAX_MQTT_CLIENT_ID_LEN        (CONFIG_HOMIE_MAX_MQTT_CLIENT_ID_LEN)
#define HOMIE_MAX_MQTT_BASE_TOPIC_LEN       (CONFIG_HOMIE_MAX_MQTT_BASE_TOPIC_LEN)
#define HOMIE_MAX_DEVICE_NAME_LEN           (CONFIG_HOMIE_MAX_DEVICE_NAME_LEN)
#define HOMIE_MAX_FIRMWARE_NAME_LEN         (CONFIG_HOMIE_MAX_FIRMWARE_NAME_LEN)
#define HOMIE_MAX_FIRMWARE_VERSION_LEN      (CONFIG_HOMIE_MAX_FIRMWARE_VERSION_LEN)
#define HOMIE_MAX_NODE_LISTS_LEN            (CONFIG_HOMIE_MAX_NODE_LISTS_LEN)
#define HOMIE_MAX_LOG_MESSAGE_LEN           (CONFIG_HOMIE_MAX_LOG_MESSAGE_LEN)

#define HOMIE_MQTT_CONNECTED_BIT BIT0
#define HOMIE_MQTT_STATUS_UPDATE_REQUIRED BIT1

/**
 * Homie client configuration.
 *
 * `mqtt_config` must have `client_id` set.
 *
 * `lwt` related configurations in mqtt_config will be overriden by the
 * library.
 */
typedef struct
{
    esp_mqtt_client_config_t mqtt_config;                   //!< MQTT configuration
    char device_name[HOMIE_MAX_DEVICE_NAME_LEN];            //!< Descriptive device name
    char base_topic[HOMIE_MAX_MQTT_BASE_TOPIC_LEN];         //!< Device base topic, usually `homie/unique_id`.
    char firmware_name[HOMIE_MAX_FIRMWARE_NAME_LEN];        //!< Firmware name
    char firmware_version[HOMIE_MAX_FIRMWARE_VERSION_LEN];  //!< Firmware version
    bool ota_enabled;                                       //!< Enable or disable OTA
    bool reboot_enabled;                                    //!< Enable or disable `reboot` command
    esp_http_client_config_t http_config;                   //!< HTTP config to download firmware
    esp_err_t (*mqtt_handler)(esp_mqtt_event_handle_t);     //!< Pointer to MQTT event handler. Set NULL if not used.
    void (*ota_status_handler)(int);                        //!< ota_status_handler
    EventGroupHandle_t *event_group;                        //!< Event group handle
    char node_lists[HOMIE_MAX_NODE_LISTS_LEN];              //!< comma-separated string of nodes
    void (*init_handler)();                                 //!< Pointer to a function that runs during `init` state. Typically, used to describe nodes in node_lists ($name, $type, and $properties).
} homie_config_t;

/**
 * @brief Initialize Homie client
 *
 * This function should be called before any other Homie functions.
 *
 * @param[in] homie_config Homie client configuration
 * @return ESP_ERR_INVALID_ARG if config is invalid, ESP_FAIL on other errors,
 *         ESP_OK on success.
 */
esp_err_t homie_init(homie_config_t *homie_config);

/**
 * @brief Start the Homie client
 * @return MQTT client handle if no errors occured. Otherwise, NULL.
 */
esp_mqtt_client_handle_t homie_run();

/**
 * @brief Subscribe to a topic under base topic
 *
 * @param subtopic Topic name to subscribe to
 * @param qos QoS. 0, 1, or 2
 * @return -1 on error, message ID on success.
 */
int homie_subscribe(const char *subtopic, const int qos);

/**
 * @brief Publish a message
 *
 * @param[in] subtopic Topic path under base_topic
 * @param[in] qos QoS level
 * @param[in] retain Retain flag
 * @param[in] payload The message
 * @return -1 on failure, return value of esp_mqtt_client_publish() on success.
 */
int homie_publish(const char *subtopic, int qos, int retain, const char *payload);

/**
 * @brief Publish a message with formart string
 *
 * @param[in] subtopic Topic path under base_topic
 * @param[in] qos QoS level
 * @param[in] retain Retain flag
 * @param[in] format Format string
 * @param[in] va_list Arguments for format string
 * @return -1 on failure, return value of esp_mqtt_client_publish() on success.
 */
int homie_publishf(const char *subtopic, int qos, int retain, const char *format, ...);

/**
 * @brief Publish an integer
 *
 * @param[in] subtopic Topic path under base_topic
 * @param[in] qos QoS level
 * @param[in] retain Retain flag
 * @param[in] payload the integer
 * @return -1 on failure, return value of esp_mqtt_client_publish() on success.
 */
int homie_publish_int(const char *subtopic, int qos, int retain, const int payload);

/**
 * @brief Publish a boolen value
 *
 * @param[in] subtopic Topic path under base_topic
 * @param[in] qos QoS level
 * @param[in] retain Retain flag
 * @param[in] payload the boolean value
 * @return -1 on failure, return value of esp_mqtt_client_publish() on success.
 */
int homie_publish_bool(const char *subtopic, int qos, int retain, const bool payload);

/**
 * @brief Create MQTT topic
 *
 * @param[out] topic Pointer to MQTT topic to be created
 * @param[in] subtopic Topic path under base_topic
 * @param[in] topic_size Size of topic
 * @return ESP_OK if no error, else ESP_FAIL
 */
esp_err_t homie_mktopic(char *topic, const char *subtopic, const size_t topic_size);

/**
 * @brief Remove a retained message
 *
 * @param[in] topic Topic name to remove retained message
 * @return MQTT message ID on success, -1 on error
 */

int homie_remove_retained(const char *topic);

/**
 * @brief Get MAC address from base MAC address
 *
 * @param[out] mac_string Buffer to copy the MAC address
 * @param[in] len Size of mac_string
 * @param[in] sep Whether if `:` should be included as a separater.
 */
esp_err_t homie_get_mac(char *mac_string, size_t len, bool sep);

typedef struct
{
    esp_mqtt_client_handle_t mqtt_client;       //!< MQTT client
    EventGroupHandle_t mqtt_event_group;        //!< Event group handle of MQTT
    int qos;                                    //!< QoS
    int retain;                                 //! 1 for retained, 0 for not retained
    QueueHandle_t queue;                        //! Queue handle of log queue
    char topic[HOMIE_MAX_MQTT_TOPIC_LEN];       //! MQTT topic
    uint16_t stack_size;                        //! Size of the task
    UBaseType_t priority;                       //! Priority of the task
    TickType_t wait_tick_receive;               //! Tick to wait to receive message from the queue
    TickType_t wait_tick_send;                  //! Tick to wait to send the message to the queue
} homie_log_mqtt_config_t;

typedef struct
{
    char payload[HOMIE_MAX_LOG_MESSAGE_LEN];        //! The log message
} homie_log_message_t;

/**
 * @brief Initialize MQTT logger
 *
 * @param[in] homie_log_mqtt_config The logger config
 *
 * @return ESP_OK if no error, else ESP_FAIL
 */
esp_err_t log_mqtt_init(homie_log_mqtt_config_t *homie_log_mqtt_config);

/**
 * @brief Start the MQTT logger
 * @return The task handle of the MQTT logger
 */
TaskHandle_t log_mqtt_start();

/**
 * @brief Stop the MQTT logger.
 *
 * Restore the default logger.
 *
 */
void log_mqtt_stop();

#endif // CM_ESP32_HOMIE_H
