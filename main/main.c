#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_idf_version.h>
#include <max7219.h>
#include <esp_idf_lib_helpers.h>
#include <esp_log.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>

#define HOST HELPER_SPI_HOST_DEFAULT

#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

#define CASCADE_SIZE 4
#define PIN_MOSI 4
#define PIN_CS 5
#define PIN_CLK 6
#define BUTTON_GPIO GPIO_NUM_0

#define TAG "MAX7219"

// static const size_t symbols_size = sizeof(symbols) - sizeof(uint64_t) * CASCADE_SIZE;


void task(void *pvParameter)
{
    // Configure SPI bus
    spi_bus_config_t cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
        .flags = 0
    };
    ESP_ERROR_CHECK(spi_bus_initialize(HOST, &cfg, 0));
    
    // Configure device
    max7219_t dev = {
        .cascade_size = CASCADE_SIZE,
        .digits = 0,
        .mirrored = true
    };
    ESP_ERROR_CHECK(max7219_init_desc(&dev, HOST, MAX7219_MAX_CLOCK_SPEED_HZ, PIN_CS));
    ESP_ERROR_CHECK(max7219_init(&dev));

    set_system_time(2025, 4, 22, 16, 32, 0);
    
    size_t offs = 0;
    bool blink_flag = false;
    
    while (1)
    {
        printf("---------- draw\n");
        max7219_set_brightness(&dev, 0);
        
        for (uint8_t c = 0; c < CASCADE_SIZE; c++) {
            max7219_draw_image_8x8(&dev, c * 8, (uint8_t *)symbols + c * 8 + offs);
            // max7219_set_brightness(&dev, c % 16);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        offs += 8;
        if (offs >= symbols_size + 8)
            offs = 0;
        vTaskDelay(pdMS_TO_TICKS(CONFIG_EXAMPLE_SCROLL_DELAY));
        
        if (offs == symbols_size)
            offs = 0;
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void button_task(void *pvParameter)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    while (1) {
        int level = gpio_get_level(BUTTON_GPIO);
        // Process button state here or send it to another task
        printf("Button state: %d\n", level);
        vTaskDelay(pdMS_TO_TICKS(1000));  // Poll every 1000 ms
    }
}

void app_main()
{
    xTaskCreatePinnedToCore(task, "task", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(button_task, "button_task", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL, APP_CPU_NUM);
}
