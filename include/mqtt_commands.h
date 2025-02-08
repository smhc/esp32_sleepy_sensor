#ifndef MQTT_COMMANDS_H
#define MQTT_COMMANDS_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "mqtt_client.h"

// Function declarations
esp_mqtt_client_handle_t RTC_IRAM_ATTR mqtt_app_start(void);
int RTC_IRAM_ATTR mqtt_app_send(esp_mqtt_client_handle_t client, const char *topic, const char* str);
void RTC_IRAM_ATTR mqtt_app_stop(esp_mqtt_client_handle_t client);

#endif // MQTT_COMMANDS_H