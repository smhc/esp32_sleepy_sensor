#include "esp_log.h"
#include "esp_http_client.h"

#include "config_options.h"
#include "http_commands.h"

static const char *TAG = "http_commands";

static esp_http_client_config_t RTC_IRAM_ATTR httpconfig = {
    .url = CONFIG_WEB_ADDRESS,
    .method = HTTP_METHOD_GET,
    .timeout_ms = CONFIG_WAIT_MS * 5,
    .keep_alive_enable = false
};

void http_send(void) {
    esp_http_client_handle_t client = esp_http_client_init(&httpconfig);
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        // try again
        err = esp_http_client_perform(client);
    }

    esp_http_client_cleanup(client);
}