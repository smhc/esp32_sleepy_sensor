#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "driver/uart.h"
#include "mqtt_client.h"
#include "esp_sleep.h"
#include "config_options.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi_station";
static EventGroupHandle_t s_wifi_event_group;
static wifi_config_t wifi_config = {
    .sta = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASS,
    },
};
static esp_netif_ip_info_t ip_info = {
    .ip = {
        .addr = CONFIG_IP_ADDRESS,
    },
    .gw = {
        .addr = CONFIG_GATEWAY_IP_ADDRESS,
    },
    .netmask = {
        .addr = CONFIG_SUBNET_MASK,
    },
};

static void RTC_IRAM_ATTR event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        // ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void RTC_IRAM_ATTR wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_dhcpc_stop(netif);
    esp_netif_set_ip_info(netif, &ip_info);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void RTC_IRAM_ATTR mqtt_app_start_and_send(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = CONFIG_MQTT_BROKER_URI
            }
        }
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
    // char msg[100];
    // sprintf(msg, "From boot to sent time: %ld ms", esp_log_timestamp());
    // Time taken from boot to sent time is typically ~300ms

    esp_mqtt_client_publish(client, CONFIG_MQTT_TOPIC, "1", 0, 1, 0);
    esp_mqtt_client_stop(client);
}

void RTC_IRAM_ATTR app_main() {
    // uart_set_baudrate(0, 115200);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    // ESP_ERROR_CHECK(ret);

    // Connect and send
    wifi_init_sta();
    EventBits_t ebresult = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdTRUE, pdTRUE,
        CONFIG_WAIT_MS / portTICK_PERIOD_MS);
    if (ebresult & WIFI_CONNECTED_BIT) {
        mqtt_app_start_and_send();
    } else {
        ESP_LOGI(TAG, "Failed to connect to AP");
    }

    // Tear down wifi
    esp_wifi_disconnect();
    xEventGroupWaitBits(s_wifi_event_group, WIFI_FAIL_BIT, pdFALSE, pdTRUE, CONFIG_WAIT_MS / portTICK_PERIOD_MS);
    vEventGroupDelete(s_wifi_event_group);

    // And sleep
    esp_deep_sleep_enable_gpio_wakeup(1<<4, ESP_GPIO_WAKEUP_GPIO_HIGH);
    esp_deep_sleep_start();
    // esp_deep_sleep(10000000L);
}