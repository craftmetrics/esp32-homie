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

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "mqtt_client.h"

#define HOMIE_MAX_TOPIC_LEN (512)
#define HOMIE_MAX_MQTT_URI_LEN (64)
#define HOMIE_MAX_MQTT_USERNAME_LEN (32)
#define HOMIE_MAX_MQTT_PASSWORD_LEN (32)
#define HOMIE_MAX_CLIENT_ID_LEN (32)
#define HOMIE_MAX_DEVICE_NAME_LEN (32)
#define HOMIE_MAX_BASE_TOPIC_LEN (32)
#define HOMIE_MAX_FIRMWARE_NAME_LEN (32)
#define HOMIE_MAX_FIRMWARE_VERSION_LEN (8)
#define HOMIE_MQTT_CONNECTED_BIT BIT0
#define HOMIE_MQTT_STATUS_UPDATE_REQUIRED BIT1

/**
 * Homie client configuration
 */
typedef struct
{
    char mqtt_uri[HOMIE_MAX_MQTT_URI_LEN];                  //!< MQTT URI string
    char mqtt_username[HOMIE_MAX_MQTT_USERNAME_LEN];        //!< User name
    char mqtt_password[HOMIE_MAX_MQTT_PASSWORD_LEN];        //!< Password
    char client_id[HOMIE_MAX_CLIENT_ID_LEN];                //!< MQTT client ID. If not defined, the base MAC address iof the device is used (in the form of `aabbccddeeff`).
    char device_name[HOMIE_MAX_DEVICE_NAME_LEN];            //!< Device name
    char base_topic[HOMIE_MAX_BASE_TOPIC_LEN];              //!< Root topic
    char firmware_name[HOMIE_MAX_FIRMWARE_NAME_LEN];        //!< Firmware name
    char firmware_version[HOMIE_MAX_FIRMWARE_VERSION_LEN];  //!< Firmware version
    bool ota_enabled;                                       //!< Enable or disable OTA
    const char *cert_pem;                                   //!< TLS certificate
    void (*msg_handler)(char *, char *);                    //!< msg_handler
    void (*connected_handler)();                            //!< connected_handler
    void (*ota_status_handler)(int);                        //!< ota_status_handler
    EventGroupHandle_t *event_group;                        //!< Event group handle
    uint16_t stack_size;                                    //!< Stack size of MQTT client
} homie_config_t;

/**
 * @brief Initialize Homie client
 *
 * @param[in] config Homie client configuration
 * @return MQTT client handle if no errors occured. Otherwise, NULL.
 */
esp_mqtt_client_handle_t homie_init(homie_config_t *config);

void homie_subscribe(const char *subtopic);

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
 * @return ESP_OK if no error, else ESP_FAIL
 */
esp_err_t homie_mktopic(char *topic, const char *subtopic);

#endif // CM_ESP32_HOMIE_H
