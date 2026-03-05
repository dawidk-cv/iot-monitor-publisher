#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "esp_intr_alloc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"


#define SPI3_SLAVE_DATA_READY_PIN_NUM GPIO_NUM_21


#define EVENT_GROUP_SPI3_DATA_READY_EVENT_BIT BIT0
#define SPI3_DATA_READY_TASK_PRIORITY 10
#define SPI3_DATA_READY_TASK_STACK 6144


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

const spi_bus_config_t spi_config = {
    .mosi_io_num = GPIO_NUM_23,
    .miso_io_num = GPIO_NUM_19,
    .sclk_io_num = GPIO_NUM_18,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .data4_io_num = -1,
    .data5_io_num = -1,
    .data6_io_num = -1,
    .data7_io_num = -1,
    .data_io_default_level = false,
    .max_transfer_sz = 0,
    .flags = SPICOMMON_BUSFLAG_MASTER       
        | SPICOMMON_BUSFLAG_SCLK
        | SPICOMMON_BUSFLAG_MOSI
        | SPICOMMON_BUSFLAG_MISO
        | SPICOMMON_BUSFLAG_NATIVE_PINS,
    .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
    .intr_flags = ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM
};

// |        |               |
// | ------ | ------------- |
// | GPIO5  | **CS**        |
// | GPIO18 | **SCLK**      |
// | GPIO19 | **MISO**      |
// | GPIO21 | **STM32 IRQ** |
// | GPIO23 | **MOSI**      |

const spi_device_interface_config_t STM32_spi_device_config = 
{
    .command_bits = 0,
    .address_bits = 0,
    .dummy_bits = 0,
    .mode = 0, /* (CPOL, CPHA) == (0, 0) */
    .clock_source = SPI_CLK_SRC_DEFAULT,
    .duty_cycle_pos = 0,
    .cs_ena_pretrans = 0,
    .cs_ena_posttrans = 0,
    .clock_speed_hz = 1000000, /* 1 MHz */
    .input_delay_ns = 0,
    .sample_point = SPI_SAMPLING_POINT_PHASE_0,
    .spics_io_num = -1,
    .flags = 0,
    .queue_size = 4,
    .pre_cb = NULL,
    .post_cb = NULL
};

spi_device_handle_t STM32_spi_device_handle;

uint8_t *rx_buf;

spi_transaction_t spi_slave_read_trans_desc = 
{
    .flags = 0,
    .cmd = 0,
    .addr = 0,
    .length = 512*8,
    .rxlength = 512*8,
    .override_freq_hz = 0,
    .user = NULL, 
    .tx_buffer = NULL,
    .rx_buffer = NULL
};


static void IRAM_ATTR spi3_slave_data_ready_isr_handler(void *arg)
{
    // set event to run read transaction task
    gpio_intr_disable(SPI3_SLAVE_DATA_READY_PIN_NUM);
    xEventGroupSetBitsFromISR(even_group_handler, EVENT_GROUP_SPI3_DATA_READY_EVENT_BIT, NULL);
    // NULL --> maybe switch now
}

void print_buffer(uint8_t *buf, size_t len)
{
    char line[128]; // wiersz tekstu do ESP_LOGI
    for (size_t i = 0; i < len; i += 16) {
        int offset = 0;
        offset += sprintf(line + offset, "%04X: ", (unsigned int)i); // opcjonalny offset
        for (size_t j = 0; j < 16 && (i + j) < len; j++) {
            offset += sprintf(line + offset, "%02X ", buf[i + j]);
            if ((j + 1) % 8 == 0) offset += sprintf(line + offset, " "); // dodatkowa spacja co 8 bajtów
        }
        ESP_LOGI(TAG, "%s", line);
    }
}


static void Spi3_slave_data_ready(void* arg)
{
    ESP_LOGI(TAG, "Spi3_slave_data_ready entry");

    spi_transaction_t *trans_desc;

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
            esp_err_t err = spi_device_queue_trans(STM32_spi_device_handle, &spi_slave_read_trans_desc, portMAX_DELAY);
            if (ESP_OK != err)
            {
                ESP_LOGI(TAG, "spi_device_queue_trans error %d", err);
            }
            else
            {
                ESP_LOGI(TAG, "spi transaction start");
                err = spi_device_get_trans_result(STM32_spi_device_handle, &trans_desc, portMAX_DELAY);
            }

            if (ESP_OK != err)
            {
                ESP_LOGI(TAG, "spi_device_get_trans_result error %d", err);
            }
            else
            {
                ESP_LOGI(TAG, "spi transaction stop");
                // print_buffer(trans_desc->rx_buffer, 512);
                // vTaskDelay(pdMS_TO_TICKS(500));
                uint8_t* buffer = (uint8_t*)(trans_desc->rx_buffer);
                if (buffer[0] == 0xAA && buffer[511] == 0x55)
                {
                    uint8_t cnt = buffer[1];
                    static uint8_t prev_cnt = 0;
                    if (prev_cnt + 1 != cnt)
                    {
                        ESP_LOGI(TAG, "lost cnt: %d --> %d", prev_cnt, cnt);
                    }
                    
                    prev_cnt = cnt;
                }



                // uint16_t it = 0;
                // for (uint16_t it = 0; it < 512; ++it)
                // {
                //     uint8_t byte = ((uint8_t*)(trans_desc->rx_buffer))[it];
                //     if (byte == 0xAA)
                //     {

                //     }
                // }
            }

        }
        gpio_intr_enable(SPI3_SLAVE_DATA_READY_PIN_NUM);
    }
}

   
void app_main(void)
{
    rx_buf = heap_caps_malloc(512, MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    memset(rx_buf, 0, 512);

    ESP_LOGI(TAG, "buffer allocated");
    // print_buffer(rx_buf, 512);
    // vTaskDelay(pdMS_TO_TICKS(500));

    spi_slave_read_trans_desc.rx_buffer = rx_buf;

    ESP_LOGI(TAG, "rx_buf allocated with %s", rx_buf != NULL ? "SUCCESS" : "FAILURE");

    // abort(); in the future
    while(NULL == rx_buf){}

    even_group_handler = xEventGroupCreate();

    /* stm32 data ready gpio irq configuring */
    bool is_setup_success = true 
    && ESP_OK == gpio_config(&spi3_slave_data_ready_GPIOConfig)
    && ESP_OK == gpio_install_isr_service(ESP_INTR_FLAG_LOWMED)
    && ESP_OK == gpio_isr_handler_add(SPI3_SLAVE_DATA_READY_PIN_NUM, spi3_slave_data_ready_isr_handler, NULL)
    && ESP_OK == gpio_intr_disable(SPI3_SLAVE_DATA_READY_PIN_NUM)
    && ESP_OK == gpio_set_intr_type(SPI3_SLAVE_DATA_READY_PIN_NUM, GPIO_INTR_LOW_LEVEL)
    ;

    ESP_LOGI(TAG, "SPI3_SLAVE_DATA_READY_PIN IRQ set with %s", is_setup_success ? "SUCCESS" : "FAILURE");

    while(!is_setup_success){}

    xTaskCreate(Spi3_slave_data_ready, "Spi3_slave_data_ready", SPI3_DATA_READY_TASK_STACK, NULL, SPI3_DATA_READY_TASK_PRIORITY, NULL);

    /* SPI configuration */

    is_setup_success = true 
        && ESP_OK == spi_bus_initialize(SPI3_HOST, &spi_config, SPI_DMA_CH_AUTO)
        && ESP_OK == spi_bus_add_device(SPI3_HOST, &STM32_spi_device_config, &STM32_spi_device_handle)
    ;

    ESP_LOGI(TAG, "SPI configure with %s", is_setup_success ? "SUCCESS" : "FAILURE");

    while(!is_setup_success){}

    ESP_LOGI(TAG, "gpio_intr_enable");
    gpio_intr_enable(SPI3_SLAVE_DATA_READY_PIN_NUM);

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
