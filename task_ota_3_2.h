#if defined(__TASK_OTA_3_2__H__)
#define __TASK_OTA_3_2__H__

/**
 * @brief Run OTA
 *
 * @param uri URI to the firmware file
 * @param cert_pem content of the certificate
 */
void do_ota(const char *uri, const char *cert_pem);
#endif
