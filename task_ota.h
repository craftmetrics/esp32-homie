/*
   This code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#if !defined(__TASK_OTA__H__)
#define __TASK_OTA__H__

#include <esp_err.h>
#include <esp_http_client.h>

/**
 * @brief Do OTA process
 *
 * @param config HTTP config
 * @return ESP_FAIL on error, ESP_OK on success or no update is required
 */
esp_err_t start_ota(esp_http_client_config_t config);

#endif
