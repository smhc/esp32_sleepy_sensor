#ifndef HTTP_COMMANDS_H
#define HTTP_COMMANDS_H

#include "esp_http_client.h"

// Function declarations
void RTC_IRAM_ATTR http_send(const bool isopen);

#endif // HTTP_COMMANDS_H