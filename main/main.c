#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_idf_version.h>
#include <max7219.h>
#include <esp_idf_lib_helpers.h>

#define HOST HELPER_SPI_HOST_DEFAULT

#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

#define CASCADE_SIZE 4
#define PIN_MOSI 4
#define PIN_CS 5
#define PIN_CLK 6

static const uint64_t symbols[] = {
    // letters
    0x383838fe7c381000,
    0xc3c3c3c3dbe7c3c3, // M
    0x3c18181818001800, // i
    0x66361E0E1E360600, // k
    0x3c023e221c000000, // e

    0x383838fe7c381000, // arrows
    0x10387cfe38383800,
    0x10307efe7e301000,
    0x1018fcfefc181000,
    0x10387cfefeee4400, // heart
    0x105438ee38541000, // sun

    0x7e1818181c181800, // digits
    0x7e060c3060663c00,
    0x3c66603860663c00,
    0x30307e3234383000,
    0x3c6660603e067e00,
    0x3c66663e06663c00,
    0x1818183030667e00,
    0x3c66663c66663c00,
    0x3c66607c66663c00,
    0x3c66666e76663c00
};
static const size_t symbols_size = sizeof(symbols) - sizeof(uint64_t) * CASCADE_SIZE;

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

    size_t offs = 0;
    while (1)
    {
        printf("---------- draw\n");
        max7219_set_brightness(&dev, 0);

        for (uint8_t c = 0; c < CASCADE_SIZE; c++) {
            max7219_draw_image_8x8(&dev, c * 8, (uint8_t *)symbols + c * 8 + offs);
            // max7219_set_brightness(&dev, c % 16);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        // for (uint8_t c = 0; c < 32; c++)
        // {
        //     max7219_draw_image_8x8(&dev, c, (uint8_t *)symbols + c + offs);
        //     // max7219_set_brightness(&dev, 4);
        //     vTaskDelay(pdMS_TO_TICKS(200));
        // }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_EXAMPLE_SCROLL_DELAY));

        if (++offs == symbols_size)
            offs = 0;
    }
}

void app_main()
{
    xTaskCreatePinnedToCore(task, "task", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL, APP_CPU_NUM);
}
