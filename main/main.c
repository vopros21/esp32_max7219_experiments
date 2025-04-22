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

#include <symbols.c>

#define HOST HELPER_SPI_HOST_DEFAULT

#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

#define CASCADE_SIZE 4
#define PIN_MOSI 4
#define PIN_CS 5
#define PIN_CLK 6

#define TAG "MAX7219"

// static const size_t symbols_size = sizeof(symbols) - sizeof(uint64_t) * CASCADE_SIZE;

uint64_t shift(uint64_t val, bool to_right)
{
    uint8_t original[8];
    
    original[0] = val & 0xFF;
    original[1] = (val >> 8) & 0xFF;
    original[2] = (val >> 16) & 0xFF;
    original[3] = (val >> 24) & 0xFF;
    original[4] = (val >> 32) & 0xFF;
    original[5] = (val >> 40) & 0xFF;
    original[6] = (val >> 48) & 0xFF;
    original[7] = (val >> 56) & 0xFF;
    
    
    uint8_t shifted[8];
    for (int i = 0; i < 8; i++) {
        shifted[i] = to_right ? original[i] << 1 : original[i] >> 1;
    }
    uint64_t result = 
    ((uint64_t)shifted[7] << 56) |
    ((uint64_t)shifted[6] << 48) |
    ((uint64_t)shifted[5] << 40) |
    ((uint64_t)shifted[4] << 32) |
    ((uint64_t)shifted[3] << 24) |
    ((uint64_t)shifted[2] << 16) |
    ((uint64_t)shifted[1] << 8)  |
    ((uint64_t)shifted[0]);
    return result;
}

uint64_t blink(uint64_t val, bool blink_flag, bool is_hour)
{
    uint8_t original[8];
    
    original[0] = val & 0xFF;
    original[1] = (val >> 8) & 0xFF;
    original[2] = (val >> 16) & 0xFF;
    original[3] = (val >> 24) & 0xFF;
    original[4] = (val >> 32) & 0xFF;
    original[5] = (val >> 40) & 0xFF;
    original[6] = (val >> 48) & 0xFF;
    original[7] = (val >> 56) & 0xFF;
    
    
    uint8_t blinked[8];
    for (int i = 0; i < 8; i++) {
        blinked[i] = original[i];
    }
    blinked[2] = blink_flag ? original[2] ^ (is_hour ? 0b10000000 : 0b1) : original[2];
    blinked[3] = blink_flag ? original[3] ^ (is_hour ? 0b10000000 : 0b1) : original[3];
    blinked[5] = blink_flag ? original[5] ^ (is_hour ? 0b10000000 : 0b1) : original[5];
    blinked[6] = blink_flag ? original[6] ^ (is_hour ? 0b10000000 : 0b1) : original[6];
    
    uint64_t result = 
    ((uint64_t)blinked[7] << 56) |
    ((uint64_t)blinked[6] << 48) |
    ((uint64_t)blinked[5] << 40) |
    ((uint64_t)blinked[4] << 32) |
    ((uint64_t)blinked[3] << 24) |
    ((uint64_t)blinked[2] << 16) |
    ((uint64_t)blinked[1] << 8)  |
    ((uint64_t)blinked[0]);
    return result;
}

void get_current_time_porto(uint64_t current_time[4]) {
    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];

    // Get current time
    time(&now);

    // Set timezone to Portugal (WET/WEST)
    setenv("TZ", "WET0WEST,M3.5.0/1,M10.5.0", 1);
    tzset();

    // Convert to local time (Porto)
    localtime_r(&now, &timeinfo);

    // Format and log the date/time string (optional)
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    printf("The current date/time in Porto is: %s\n", strftime_buf);

    int hours = timeinfo.tm_hour;
    int minutes = timeinfo.tm_min;
    printf("The current time is: %02d:%02d\n", hours, minutes);

    // Fill the current_time array with encoded digits
    current_time[0] = digits[hours / 10];
    current_time[1] = shift(digits[hours % 10], 0);
    current_time[2] = shift(digits[minutes / 10], 1);
    current_time[3] = digits[minutes % 10];
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
    
    size_t offs = 0;
    bool blink_flag = false;
    
    while (1)
    {
        printf("---------- draw\n");
        max7219_set_brightness(&dev, 0);
        
        // for (uint8_t c = 0; c < CASCADE_SIZE; c++) {
        //     max7219_draw_image_8x8(&dev, c * 8, (uint8_t *)symbols + c * 8 + offs);
        //     // max7219_set_brightness(&dev, c % 16);
        //     vTaskDelay(pdMS_TO_TICKS(200));
        // }
        // offs += 8;
        // if (offs >= symbols_size + 8)
        //     offs = 0;
        // vTaskDelay(pdMS_TO_TICKS(CONFIG_EXAMPLE_SCROLL_DELAY));
        
        // if (offs == symbols_size)
        //     offs = 0;
        
        
        uint64_t current_time[4];
        get_current_time_porto(current_time);
        
        current_time[1] = blink(current_time[1], blink_flag, 1);
        current_time[2] = blink(current_time[2], blink_flag, 0);
        blink_flag = !blink_flag;
        
        const size_t time_size = sizeof(current_time) - sizeof(uint64_t) * CASCADE_SIZE;
        
        for (uint8_t c = 0; c < CASCADE_SIZE; c++) {
            max7219_draw_image_8x8(&dev, c * 8, (uint8_t *)current_time + c * 8 + offs);
            // max7219_set_brightness(&dev, c % 16);
            // vTaskDelay(pdMS_TO_TICKS(100));
        }
        offs += 8;
        if (offs >= time_size + 8)
        offs = 0;
        // vTaskDelay(pdMS_TO_TICKS(CONFIG_EXAMPLE_SCROLL_DELAY));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main()
{
    xTaskCreatePinnedToCore(task, "task", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL, APP_CPU_NUM);
}
