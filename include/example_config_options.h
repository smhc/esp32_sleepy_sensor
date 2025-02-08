// Rename this file to "config_options.h" and adjust the values to match your setup
#define CONFIG_WIFI_SSID "mywifi"
#define CONFIG_WIFI_PASS "password"
#define CONFIG_IP_ADDRESS ESP_IP4TOADDR(192, 168, 1, 88)
#define CONFIG_GATEWAY_IP_ADDRESS ESP_IP4TOADDR(192, 168, 1, 1)
#define CONFIG_SUBNET_MASK ESP_IP4TOADDR(255, 255, 255, 0)
#define CONFIG_MQTT_BROKER_URI "mqtt://192.168.1.120"
#define CONFIG_MQTT_STATUS_TOPIC "esp32sensor/frontgate1/status"
#define CONFIG_MQTT_WIFIFAIL_TOPIC "esp32sensor/frontgate1/wififailcount"
#define CONFIG_MQTT_MQTTFAIL_TOPIC "esp32sensor/frontgate1/mqttfailcount"
#define CONFIG_MQTT_SLEEPRETRY_TOPIC "esp32sensor/frontgate1/sleepretrycount"
#define CONFIG_MQTT_BOUNCE_TOPIC "esp32sensor/frontgate1/bouncecount"
#define CONFIG_MQTT_BOOT_TOPIC "esp32sensor/frontgate1/bootcount"
#define CONFIG_WEB_ADDRESS "http://192.168.1.80/relay/0?turn=on"
#define CONFIG_WAIT_MS 4000