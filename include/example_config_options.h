// Rename this file to "config_options.h" and adjust the values to match your setup
#define CONFIG_WIFI_SSID "mywifi"
#define CONFIG_WIFI_PASS "password"
#define CONFIG_IP_ADDRESS ESP_IP4TOADDR(192, 168, 1, 88)
#define CONFIG_GATEWAY_IP_ADDRESS ESP_IP4TOADDR(192, 168, 1, 1)
#define CONFIG_SUBNET_MASK ESP_IP4TOADDR(255, 255, 255, 0)
#define CONFIG_MQTT_BROKER_URI "mqtt://192.168.1.120"
#define CONFIG_MQTT_TOPIC "esp32sensor/door1"
#define CONFIG_WAIT_MS 500