#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

// Function declarations
void RTC_IRAM_ATTR wifi_init_boot();
bool RTC_IRAM_ATTR connect_wifi();
void RTC_IRAM_ATTR disconnect_wifi();

extern RTC_DATA_ATTR EventGroupHandle_t s_wifi_event_group;

#endif // WIFI_CONNECT_H