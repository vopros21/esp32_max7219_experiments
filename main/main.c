#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_idf_version.h>
#include <max7219.h>
#include <esp_idf_lib_helpers.h>
#include "alphabet.h"

#define HOST HELPER_SPI_HOST_DEFAULT

#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

#define CASCADE_SIZE 4
#define PIN_MOSI 4
#define PIN_CS 5
#define PIN_CLK 6

// static const size_t symbols_size = sizeof(symbols) - sizeof(uint64_t) * CASCADE_SIZE;


// Note: The display matrix is divided into 8-pixel-wide columns, with valid column indices from 0 to 31.
// To scroll symbols across the display, shift each symbol left by 8 columns, wrapping bits from one symbol
// into the next. For multiple symbols, concatenate their bitmaps into a continuous stream, then extract
// 8x8 segments for each display position. This ensures smooth scrolling and correct alignment across cascaded matrices.
void run_text(max7219_t *dev, const uint64_t val)
{
    uint8_t s[8];
    s[7] = val & 0xFF;
    s[6] = (val >> 8) & 0xFF;
    s[5] = (val >> 16) & 0xFF;
    s[4] = (val >> 24) & 0xFF;
    s[3] = (val >> 32) & 0xFF;
    s[2] = (val >> 40) & 0xFF;
    s[1] = (val >> 48) & 0xFF;
    s[0] = (val >> 56) & 0xFF;

    
    for (int i = 0; i < 8; i++) {
            max7219_set_digit(dev, i, 0);
    }

    int row = 31;
    while (row > -8) {
        for (int j = 7; j >= 0; j--) {
            for (int i = 0; i < 8; i++) {
                if (row <= 23) {
                    max7219_set_digit(dev, row + 8 - i, s[i] >> (8 - j));
                }
                if (row > 0) {
                    max7219_set_digit(dev, row - i, s[i] << j);
                }
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
        for (int i = 0; i < 8; i++) {
            if (row <= 23)
                max7219_set_digit(dev, row + 8 - i, 0);
        }
        row -= 8;
    }
}

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

    // size_t offs = 0;
    // uint8_t row = 27;
    // uint8_t value = 0xC0;
    while (1)
    {
        printf("---------- draw: %d \n", dev.digits);
        max7219_set_brightness(&dev, 0);
        
        // run_text(&dev, symbols[2]);
        run_text(&dev, symbols['#' - 32]);

        // for (uint8_t c = 0; c < CASCADE_SIZE; c++) {
        //     max7219_draw_image_8x8(&dev, c * 8, (uint8_t *)symbols + c * 8 + offs);
        //     // max7219_set_brightness(&dev, c % 16);
        //     vTaskDelay(pdMS_TO_TICKS(200));
        // }
        // offs += 8;
        // if (offs >= symbols_size + 8)
        //     offs = 0;



        // max7219_set_digit(&dev, row, value);
        // vTaskDelay(pdMS_TO_TICKS(200));
        // value >>= 1;
        // if (value == 0x01) {
        //     uint8_t new_row = row - 8 > dev.digits ? 27 : row - 8;
        //     max7219_set_digit(&dev, new_row, 0x80);
        // }
        // if (value == 0) {
        //     value = 0xC0;
        //     max7219_set_digit(&dev, row, 0);
        //     row -= 8;
        //     if (row > dev.digits) {
        //         row = 27;
        //     }
        // }




        // if (offs == symbols_size)
        //     offs = 0;
    }
}

void app_main()
{
    xTaskCreatePinnedToCore(task, "task", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL, APP_CPU_NUM);
}
