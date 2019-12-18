#if !defined(__CONNECT_ESP8266__H__)
#define __CONNECT_ESP8266__H__

#include <esp_err.h>
#include <esp_event.h>

esp_err_t wifi_event_handler(void *ctx, system_event_t *event);
void wifi_init(void);

#endif
