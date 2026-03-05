
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "driver/gpio.h"


#define SPI3_SLAVE_DATA_READY_PIN_NUM GPIO_NUM_21


#define EVENT_GROUP_SPI3_DATA_READY_EVENT_BIT BIT0
#define SPI3_DATA_READY_TASK_PRIORITY 10
#define SPI3_DATA_READY_TASK_STACK 2048


#if SPI3_DATA_READY_TASK_PRIORITY > configMAX_PRIORITIES
#error "Invalid priority value."
#endif


static const char *TAG = "iot-monitor-publisher";

static EventGroupHandle_t even_group_handler;

const gpio_config_t spi3_slave_data_ready_GPIOConfig = 
{
    .intr_type = GPIO_INTR_DISABLE,
    .mode = GPIO_MODE_INPUT,
    .pin_bit_mask = (1ULL << SPI3_SLAVE_DATA_READY_PIN_NUM),
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .pull_up_en = GPIO_PULLUP_ENABLE
};


static void IRAM_ATTR spi3_slave_data_ready_isr_handler(void *arg)
{
    // set event to run read transaction task
    xEventGroupSetBitsFromISR(even_group_handler, EVENT_GROUP_SPI3_DATA_READY_EVENT_BIT, NULL);
}


static void Spi3_slave_data_ready(void* arg)
{
    ESP_LOGI(TAG, "SPI3 SLAVE --> entry");

    while(1)
    {
        if (EVENT_GROUP_SPI3_DATA_READY_EVENT_BIT & xEventGroupWaitBits(
            even_group_handler,
            EVENT_GROUP_SPI3_DATA_READY_EVENT_BIT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY
        ))
        {
            static uint32_t cnt = 0;
            ESP_LOGI(TAG, "SPI3 SLAVE --> data ready %d", cnt++);
        }
    }
}

   
void app_main(void)
{
    even_group_handler = xEventGroupCreate();

    bool is_setup_success = true 
    && ESP_OK == gpio_config(&spi3_slave_data_ready_GPIOConfig)
    && ESP_OK == gpio_install_isr_service(ESP_INTR_FLAG_LOWMED)
    && ESP_OK == gpio_isr_handler_add(SPI3_SLAVE_DATA_READY_PIN_NUM, spi3_slave_data_ready_isr_handler, NULL)
    && ESP_OK == gpio_intr_disable(SPI3_SLAVE_DATA_READY_PIN_NUM)
    && ESP_OK == gpio_set_intr_type(SPI3_SLAVE_DATA_READY_PIN_NUM, GPIO_INTR_LOW_LEVEL)
    ;

    ESP_LOGI(TAG, "SPI3_SLAVE_DATA_READY_PIN IRQ set with %s", is_setup_success ? "SUCCESS" : "FAILURE");

    while(!is_setup_success){}

    // configMAX_PRIORITIES
    xTaskCreate(Spi3_slave_data_ready, "Spi3_slave_data_ready", SPI3_DATA_READY_TASK_STACK, NULL, SPI3_DATA_READY_TASK_PRIORITY, NULL);


    uint32_t cnt = 0;
    bool is_exec = false;
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        if (!is_exec && cnt > 10)
        {
            is_exec = true;
            ESP_LOGI(TAG, "enable interrupt"); 
            gpio_intr_enable(SPI3_SLAVE_DATA_READY_PIN_NUM);
        }
    }
}
