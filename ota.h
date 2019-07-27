
#define HOMIE_OTA_MAX_URL_LEN (256)

void ota_init(char *url, const char *cert_pem, void (*status_handler)(int));
