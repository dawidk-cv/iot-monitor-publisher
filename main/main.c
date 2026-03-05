#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "sdkconfig.h"


static const char *TAG = "iot-monitor-publisher";

void app_main(void)
{
    uint32_t cnt = 0;
    while (1) {
        ESP_LOGI(TAG, "Infinity loop: %d", cnt++);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
