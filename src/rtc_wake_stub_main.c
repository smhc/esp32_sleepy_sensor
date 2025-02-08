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
static const char *TAG = "main_stub";

#define GPIO_WAKE          GPIO_NUM_4
#define REED_OPEN_STATE    ESP_GPIO_WAKEUP_GPIO_HIGH
#define REED_CLOSE_STATE   ESP_GPIO_WAKEUP_GPIO_LOW

extern RTC_DATA_ATTR unsigned int wifi_failcount;
extern RTC_DATA_ATTR unsigned int mqtt_failcount;
RTC_DATA_ATTR unsigned int sleep_retry_count = 0;
RTC_DATA_ATTR unsigned int gpio_unstable_count = 0;
RTC_DATA_ATTR unsigned int bootcount = 0;

RTC_DATA_ATTR unsigned int last_reported_cksum = 9999;
RTC_DATA_ATTR unsigned int last_gpio_level = REED_OPEN_STATE;

void RTC_IRAM_ATTR sleep_retry(void) {
    // try again in ~10 minutes
    ESP_LOGI(TAG, "Sleeping for 10 mins");
    sleep_retry_count++;
    disconnect_wifi();
    esp_deep_sleep(600000000L);
    // esp_deep_sleep(900000L);
}

int RTC_IRAM_ATTR sendOpenState(esp_mqtt_client_handle_t mqttclient, int msg_id) {
    int curgpiolevel = gpio_get_level(GPIO_WAKE);
    if (msg_id < 0 || curgpiolevel != last_gpio_level) {
        msg_id = mqtt_app_send(mqttclient, CONFIG_MQTT_STATUS_TOPIC, curgpiolevel == REED_OPEN_STATE ? "open" : "close");
        if (msg_id >= 0) {
            last_gpio_level = curgpiolevel;
        } else {
            mqtt_failcount++;
        }
    }
    return msg_id;
}

void RTC_IRAM_ATTR app_main() {
    // uart_set_baudrate(0, 115200);
    char charbuffer[32];
    bootcount++;

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

        if (last_gpio_level == gpio_get_level(GPIO_WAKE)) {
            // on wake the gpio should have changed from the previous wake.
            // Keep track of fluctuations if it hasn't.
            ESP_LOGD(TAG, "gpio == prev wake state");
            gpio_unstable_count++;
        }

        esp_mqtt_client_handle_t mqttclient = mqtt_app_start();
        if (mqttclient != NULL) {
            // Send the current state of the reed switch, if it has changed
            msg_id = sendOpenState(mqttclient, 0);

            // delay for 1000ms to allow the reed switch to settle and send the status again if it changed
            TickType_t prevWake = xTaskGetTickCount();
            xTaskDelayUntil(&prevWake, 1000 / portTICK_PERIOD_MS);

            int initial_gpio_level = last_gpio_level;
            msg_id = sendOpenState(mqttclient, msg_id);
            if (last_gpio_level != initial_gpio_level) {
                // It bounced, keep track
                ESP_LOGD(TAG, "gpio bounced");
                gpio_unstable_count++;
            }

            if (msg_id >= 0 && bootcount % 5 == 0) {
                // Record boot count every 5 boots
                itoa(bootcount, charbuffer, 10);
                mqtt_app_send(mqttclient, CONFIG_MQTT_BOOT_TOPIC, charbuffer);
            }

            int curr_cksum = mqtt_failcount +
                            wifi_failcount +
                            sleep_retry_count +
                            gpio_unstable_count;

            if (curr_cksum != last_reported_cksum) {
                ESP_LOGD(TAG, "publishing counts...");
                itoa(mqtt_failcount, charbuffer, 10);
                int success = mqtt_app_send(mqttclient, CONFIG_MQTT_MQTTFAIL_TOPIC, charbuffer);
                itoa(wifi_failcount, charbuffer, 10);
                success += mqtt_app_send(mqttclient, CONFIG_MQTT_WIFIFAIL_TOPIC, charbuffer);
                itoa(sleep_retry_count, charbuffer, 10);
                success += mqtt_app_send(mqttclient, CONFIG_MQTT_SLEEPRETRY_TOPIC, charbuffer);
                itoa(gpio_unstable_count, charbuffer, 10);
                success += mqtt_app_send(mqttclient, CONFIG_MQTT_BOUNCE_TOPIC, charbuffer);
                if (success >= 0) {
                    last_reported_cksum = curr_cksum;
                } else {
                    ESP_LOGE(TAG, "Failed publishing counts");
                }
            }

            // Tear down mqtt / wifi
            mqtt_app_stop(mqttclient);
        } else {
            mqtt_failcount++;
            ESP_LOGE(TAG, "mqtt client failed to start...");
        }

        disconnect_wifi();

        // last_gpio_level is the last reported gpio level, so we want to wake on change
        // from that, regardless of the current state. It may mean waking immediately,
        // but that's fine.
        esp_deepsleep_gpio_wake_up_mode_t wake_mode =
            last_gpio_level == REED_OPEN_STATE ? REED_CLOSE_STATE : REED_OPEN_STATE;
        esp_deep_sleep_enable_gpio_wakeup(1 << GPIO_WAKE, wake_mode);

        if (msg_id < 0) {
            ESP_LOGE(TAG, "mqtt message failed, sleep retry...");
            // mqtt message failed, try again in ~10 minutes, or immediately if the pin changes state
            sleep_retry();
            return;
        }

        ESP_LOGI(TAG, "Deep sleep for gpio change");
        esp_deep_sleep_start();
        ESP_LOGE(TAG, "Failed to enter deep sleep with gpio");
        // should never get here
    }
    else {
        ESP_LOGE(TAG, "Failed to connect to AP");
    }
    // try again in 10 minutes
    sleep_retry();
}