#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "mqtt_client.h"
#include "esp_sleep.h"
#include "config_options.h"
#include "esp_http_client.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MQTT_CONNECT_BIT   BIT2
#define MQTT_FAIL_BIT      BIT3
#define MQTT_PUBLISHED_BIT BIT4
#define GPIO_WAKE          GPIO_NUM_4

RTC_DATA_ATTR int wake_count;
RTC_FAST_ATTR time_t last_wake_time;
RTC_FAST_ATTR int msg_type;
RTC_FAST_ATTR int last_message_id;

static RTC_DATA_ATTR const char *TAG = "wifi_station";
static RTC_DATA_ATTR EventGroupHandle_t s_wifi_event_group;
static RTC_DATA_ATTR esp_netif_t *netif;
static RTC_DATA_ATTR wifi_config_t wifi_config = {
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
static esp_http_client_config_t config = {
    .url = CONFIG_WEB_ADDRESS
};

static void RTC_IRAM_ATTR event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Got event WIFI_EVENT_STA_CONNECTED");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Got event WIFI_EVENT_STA_DISCONNECTED");
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got event IP_EVENT_STA_GOT_IP");
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "old ip: %lu", (ip_info.ip.addr));
        ESP_LOGI(TAG, "new ip: %lu", (event->ip_info.ip.addr));
        if ((ip_info.ip.addr & ip_info.netmask.addr) != (event->ip_info.ip.addr & event->ip_info.netmask.addr)) {
            ESP_LOGI(TAG, "storing new ip:" IPSTR, IP2STR(&event->ip_info.ip));
            memcpy(&ip_info, &event->ip_info, sizeof(ip_info));
        }
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else {
        ESP_LOGI(TAG, "Got wifi event %s, %ld", event_base, event_id);
    }
}

void RTC_IRAM_ATTR wifi_init_sta(bool retry) {
    ESP_LOGI(TAG, "Initializing WiFi in station mode");

    if (ip_info.ip.addr == 0 || retry) {
        // initial attempt or retry, use dhcp
        ESP_LOGI(TAG, "Getting IP address from DHCP");
    } else {
        // use saved ipinfo
        esp_netif_dhcpc_stop(netif);
        esp_netif_set_ip_info(netif, &ip_info);
    }

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

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void RTC_IRAM_ATTR mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        xEventGroupSetBits(s_wifi_event_group, MQTT_CONNECT_BIT);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        last_message_id = event->msg_id;
        xEventGroupSetBits(s_wifi_event_group, MQTT_PUBLISHED_BIT);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        xEventGroupSetBits(s_wifi_event_group, MQTT_FAIL_BIT);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    default:
        break;
    }
}

esp_mqtt_client_handle_t RTC_IRAM_ATTR mqtt_app_start(void) {
    static RTC_RODATA_ATTR esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = CONFIG_MQTT_BROKER_URI
            }
        }
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    return client;
}

int RTC_IRAM_ATTR mqtt_app_send(esp_mqtt_client_handle_t client, char* str) {
    int msg_id = esp_mqtt_client_publish(client, CONFIG_MQTT_TOPIC, str, 0, 0, 0);
    if (msg_id < 0) {
        ESP_LOGI(TAG, "Failed to publish message\n");
    }
    return msg_id;
}

void RTC_IRAM_ATTR mqtt_app_stop(esp_mqtt_client_handle_t client) {
    // esp_mqtt_client_disconnect(client);
    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
}

void RTC_IRAM_ATTR http_send(void) {
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

bool RTC_IRAM_ATTR tryconnect(bool retry) {
    netif = esp_netif_create_default_wifi_sta();
    wifi_init_sta(retry);
    EventBits_t ebresult = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdTRUE, pdTRUE,
        (CONFIG_WAIT_MS * (retry ? 1 : 5)) / portTICK_PERIOD_MS);
    if (!ebresult || !(ebresult & WIFI_CONNECTED_BIT)) {
        ESP_LOGI(TAG, "Failed connection, destroying netif");
        esp_wifi_stop();
        esp_netif_destroy_default_wifi(netif);
        xEventGroupClearBits(s_wifi_event_group, 0xFF);
        return false;
    }
    return true;
}

bool RTC_IRAM_ATTR connect_wifi() {
    bool result = tryconnect(false);
    if (!result) {
        // try again, using DHCP
        ESP_LOGI(TAG, "Failed to connect, retrying with DHCP");
        result = tryconnect(true);
    }
    return result;
}

void RTC_IRAM_ATTR app_main() {
    uart_set_baudrate(0, 115200);
    ESP_LOGI(TAG, "Initializing ...");
    esp_sleep_wakeup_cause_t wc = esp_sleep_get_wakeup_cause();
    if (wc != ESP_SLEEP_WAKEUP_GPIO) {
        gpio_set_direction(GPIO_WAKE, GPIO_MODE_INPUT);
    }

    int wakegpiolevel = gpio_get_level(GPIO_WAKE);
    ESP_LOGI(TAG, "GPIO level: %d", wakegpiolevel);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Connect and send
    if (connect_wifi()) {
        esp_mqtt_client_handle_t client = mqtt_app_start();
        xEventGroupWaitBits(s_wifi_event_group, MQTT_CONNECT_BIT, pdTRUE, pdTRUE, CONFIG_WAIT_MS / portTICK_PERIOD_MS);

        int curgpiolevel = gpio_get_level(GPIO_WAKE);
        mqtt_app_send(client, wakegpiolevel ? "open" : "close");
        if (wakegpiolevel != curgpiolevel) {
            mqtt_app_send(client, curgpiolevel ? "open(q)" : "close(q)");
        }
        mqtt_app_stop(client);

        // Tear down wifi
        esp_wifi_disconnect();
        xEventGroupWaitBits(s_wifi_event_group, WIFI_FAIL_BIT, pdFALSE, pdTRUE, CONFIG_WAIT_MS / portTICK_PERIOD_MS);
        vEventGroupDelete(s_wifi_event_group);

        esp_err_t res;
        do {
            esp_deepsleep_gpio_wake_up_mode_t wake_mode;
            if (curgpiolevel == 0) {
                wake_mode = ESP_GPIO_WAKEUP_GPIO_HIGH;
            } else {
                wake_mode = ESP_GPIO_WAKEUP_GPIO_LOW;
            }
            // And sleep
            esp_deep_sleep_enable_gpio_wakeup(1<<GPIO_WAKE, wake_mode);
            // esp_deep_sleep_start();
            res = esp_deep_sleep_try_to_start();
            curgpiolevel = gpio_get_level(GPIO_WAKE);
            ESP_LOGI(TAG, "Failed to enter deep sleep with gpio");
        } while (res != ESP_OK);
        ESP_LOGI(TAG, "Failed to sleep");
        esp_deep_sleep(10000000L);
    }
    else {
        ESP_LOGI(TAG, "Failed to connect to AP");
        esp_deep_sleep(10000000L);
    }
}