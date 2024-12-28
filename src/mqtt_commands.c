#include "esp_log.h"
#include "mqtt_client.h"

#include "config_options.h"
#include "mqtt_commands.h"
#include "wifi_connect.h"

static const char *TAG = "mqtt_commands";

#define MQTT_CONNECT_BIT   BIT2
#define MQTT_FAIL_BIT      BIT3
#define MQTT_PUBLISHED_BIT BIT4

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

esp_mqtt_client_handle_t mqtt_app_start(void) {
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
    EventBits_t ebresult = xEventGroupWaitBits(s_wifi_event_group, MQTT_CONNECT_BIT, pdTRUE, pdTRUE,
            (CONFIG_WAIT_MS * 5) / portTICK_PERIOD_MS);
    if (ebresult && (ebresult & MQTT_CONNECT_BIT)) {
        return client;
    } else {
        return NULL;
    }
}

int mqtt_app_send(esp_mqtt_client_handle_t client, const char* str) {
    int msg_id = esp_mqtt_client_publish(client, CONFIG_MQTT_TOPIC, str, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGI(TAG, "Failed to publish message\n");
    } else {
        EventBits_t ebresult = xEventGroupWaitBits(s_wifi_event_group, MQTT_PUBLISHED_BIT, pdTRUE, pdTRUE,
            (CONFIG_WAIT_MS * 5) / portTICK_PERIOD_MS);
        if (!ebresult || !(ebresult & MQTT_PUBLISHED_BIT)) {
            ESP_LOGI(TAG, "Failed to wait for published message");
            return -1;
        }
    }
    return msg_id;
}

void mqtt_app_stop(esp_mqtt_client_handle_t client) {
    // esp_mqtt_client_disconnect(client);
    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
}