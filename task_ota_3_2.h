#if !defined(__TASK_OTA_3_2__H__)
#define __TASK_OTA_3_2__H__

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
