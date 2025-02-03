#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_sleep.h"

#include "config_options.h"
#include "wifi_connect.h"
#include "mqtt_commands.h"
// #include "http_commands.h"

static const char *TAG = "main_stub";

#define GPIO_WAKE          GPIO_NUM_4
#define REED_OPEN_STATE    ESP_GPIO_WAKEUP_GPIO_HIGH
#define REED_CLOSE_STATE   ESP_GPIO_WAKEUP_GPIO_LOW

int RTC_IRAM_ATTR stillopen = 0;

void RTC_IRAM_ATTR sleep_retry(void) {
    // try again in ~15 minutes
    ESP_LOGI(TAG, "Sleeping for 15 mins");
    disconnect_wifi();
    esp_deep_sleep(900000000L);
    // esp_deep_sleep(900000L);
}

void RTC_IRAM_ATTR app_main() {
    // uart_set_baudrate(0, 115200);

    // ESP_LOGI(TAG, "Initializing ...");
    esp_sleep_wakeup_cause_t wc = esp_sleep_get_wakeup_cause();
    if (wc != ESP_SLEEP_WAKEUP_GPIO && wc != ESP_SLEEP_WAKEUP_TIMER) {
        gpio_set_direction(GPIO_WAKE, GPIO_MODE_INPUT);
        gpio_pulldown_dis(GPIO_WAKE);
        gpio_pullup_dis(GPIO_WAKE);
        wifi_init_boot();
        esp_deep_sleep_disable_rom_logging();
    }
    xEventGroupClearBits(s_wifi_event_group, 0xFF);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (connect_wifi()) {
        int msg_id = -1;

        int curgpiolevel = gpio_get_level(GPIO_WAKE);
        // http_send(curgpiolevel == REED_OPEN_STATE);

        esp_mqtt_client_handle_t mqttclient = mqtt_app_start();
        if (mqttclient != NULL) {
            // defer checking the pin until we're connected to allow bounce to settle
            curgpiolevel = gpio_get_level(GPIO_WAKE);
            msg_id = mqtt_app_send(mqttclient, curgpiolevel == REED_OPEN_STATE ? "open" : "close");
            if (msg_id < 0) {
                ESP_LOGE(TAG, "mqtt send failed...");
                // try one more time
                msg_id = mqtt_app_send(mqttclient, curgpiolevel == REED_OPEN_STATE ? "open" : "close");
            }
        }

        // Tear down mqtt / wifi
        if (mqttclient != NULL) {
            mqtt_app_stop(mqttclient);
        }
        
        disconnect_wifi();

        esp_deepsleep_gpio_wake_up_mode_t wake_mode =
            curgpiolevel == REED_OPEN_STATE ? REED_CLOSE_STATE : REED_OPEN_STATE;
        esp_deep_sleep_enable_gpio_wakeup(1 << GPIO_WAKE, wake_mode);

        if (msg_id < 0) {
            ESP_LOGE(TAG, "mqtt message failed, trying again in 15 minutes...");
            // mqtt message failed, try again in ~15 minutes, or immediately if the pin changes state
            sleep_retry();
            return;
        }

        // And sleep. We may immediately wake if the pin has changed state
        // if open, also wake every 8 minutes to resend light on signal
        if (curgpiolevel == REED_OPEN_STATE) {
            // sleep for ~8 minutes, or pin changes state
            ESP_LOGI(TAG, "Reed open. Deep sleep for timer or gpio change");
            esp_deep_sleep(480000000L);
        } else {
            // sleep until the pin changes state
            ESP_LOGI(TAG, "Reed closed. Deep sleep for gpio change");
            esp_deep_sleep_start();
        }
        ESP_LOGE(TAG, "Failed to enter deep sleep with gpio");
        // should never get here
    }
    else {
        ESP_LOGE(TAG, "Failed to connect to AP");
    }
    // try again in 15 minutes
    sleep_retry();
}