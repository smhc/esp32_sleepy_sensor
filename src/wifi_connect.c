#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include <string.h>
#include "esp_private/phy.h"
#include "esp_phy_init.h"

#include "bo_wsc.h"

#include "config_options.h"
#include "wifi_connect.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi_connect";
RTC_DATA_ATTR esp_phy_calibration_data_t cal_data;

EventGroupHandle_t s_wifi_event_group = NULL;
RTC_DATA_ATTR unsigned int wifi_failcount = 0;
RTC_DATA_ATTR StaticEventGroup_t s_wifi_event_group_storage;
RTC_DATA_ATTR esp_netif_t *netif;
RTC_DATA_ATTR wifi_config_t wifi_config = {
    .sta = {
        .ssid = { CONFIG_WIFI_SSID },
        .password = { CONFIG_WIFI_PASS },
        .channel = 0,
    },
};
RTC_DATA_ATTR esp_netif_ip_info_t ip_info = {
    .ip = {
        .addr = 0,
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
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Got event WIFI_EVENT_STA_CONNECTED");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_HOME_CHANNEL_CHANGE) {
        ESP_LOGI(TAG, "Got event WIFI_EVENT_HOME_CHANNEL_CHANGE");
        wifi_event_home_channel_change_t* change = (wifi_event_home_channel_change_t*) event_data;
        ESP_LOGI(TAG, "config channel %d", wifi_config.sta.channel);
        ESP_LOGW(TAG, "old channel: %d, new channel: %d", change->old_chan, change->new_chan);
        if (change->old_chan != 0) {
            wifi_config.sta.channel = change->new_chan;
        }
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

static bool RTC_IRAM_ATTR wifi_init_sta(bool retry) {
    ESP_LOGI(TAG, "Initializing WiFi in station mode");
    netif = esp_netif_create_default_wifi_sta();

    if (ip_info.ip.addr == 0 || retry) {
        // initial attempt or retry, use dhcp
        ESP_LOGI(TAG, "Getting IP addresses from DHCP");
        memset(&ip_info, 0, sizeof(ip_info));
        retry = true;
    } else {
        // use saved ipinfo
        ESP_LOGI(TAG, "Using saved IP info");
        esp_netif_dhcpc_stop(netif);
        esp_netif_set_ip_info(netif, &ip_info);
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    // ESP_LOGD(TAG, "Registering wifi event handler");
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    // ESP_LOGD(TAG, "Registering IP event handler");
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = true; // use the 'fake' nvs stored in RTC mem
    bo_wsc_set(cfg.osi_funcs);
    ESP_LOGD(TAG, "Initialise wifi cfg, channel %d", wifi_config.sta.channel);
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    if (retry) {
        ESP_LOGD(TAG, "Setting STA mode");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
        ESP_LOGW(TAG, "Retry set wifi config, after channel %d", wifi_config.sta.channel);
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t ebresult = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT|WIFI_FAIL_BIT, pdTRUE, pdFALSE,
        (CONFIG_WAIT_MS * (retry ? 10 : 1)) / portTICK_PERIOD_MS);
    if (!ebresult || !(ebresult & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Failed connection, stopping wifi");
        esp_wifi_stop();
        esp_netif_destroy_default_wifi(netif);
        xEventGroupClearBits(s_wifi_event_group, 0xFF);
        return false;
    }
    ESP_LOGW(TAG, "Finish Cfg channel %d", wifi_config.sta.channel);
    return true;
}

bool connect_wifi() {
    bool result = wifi_init_sta(false);
    if (!result) {
        wifi_failcount++;
        // try again, using DHCP
        ESP_LOGI(TAG, "Failed to connect, retrying with DHCP");
        result = wifi_init_sta(true);
    }
    return result;
}

void disconnect_wifi() {
    esp_wifi_disconnect();
    xEventGroupWaitBits(s_wifi_event_group, WIFI_FAIL_BIT, pdTRUE, pdTRUE, (CONFIG_WAIT_MS * 3) / portTICK_PERIOD_MS);
    if (netif) {
        esp_wifi_stop();
        esp_netif_destroy_default_wifi(netif);
        netif = NULL;
    }
}

void wifi_init_boot() {
    ESP_LOGD(TAG, "wifi init boot");
    memset(&s_wifi_event_group_storage, 0, sizeof(StaticEventGroup_t));
    s_wifi_event_group = xEventGroupCreateStatic(&s_wifi_event_group_storage);
}