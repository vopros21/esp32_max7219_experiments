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

#define TAG "MAX7219"

// current local time
// time_t now;
// char strftime_buf[64];
// struct tm timeinfo;

// time(&now);
// // Set timezone to China Standard Time
// setenv("TZ", "CST-8", 1);
// tzset();

// localtime_r(&now, &timeinfo);
// strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
// ESP_LOGI(TAG, "The current date/time in Shanghai is: %s", strftime_buf);

// static const uint64_t symbols[] = {
//     // letters
//     0x383838fe7c381000,
//     0xc3c3c3c3dbe7c3c3, // M
//     0x3c18181818001800, // i
//     0x66361E0E1E360600, // k
//     0x3c023e221c000000, // e

//     0x383838fe7c381000, // arrows
//     0x10387cfe38383800,
//     0x10307efe7e301000,
//     0x1018fcfefc181000,
//     0x10387cfefeee4400, // heart
//     0x105438ee38541000, // sun

//     0x7e1818181c181800, // digits
//     0x7e060c3060663c00,
//     0x3c66603860663c00,
//     0x30307e3234383000,
//     0x3c6660603e067e00,
//     0x3c66663e06663c00,
//     0x1818183030667e00,
//     0x3c66663c66663c00,
//     0x3c66607c66663c00,
//     0x3c66666e76663c00
// };

static const uint64_t digits[] = {
    0x3c66666e76663c00, // 0
    0x7e1818181c181800, // 1
    0x7e060c3060663c00, // 2
    0x3c66603860663c00, // 3
    0x30307e3234383000, // 4
    0x3c6660603e067e00, // 5
    0x3c66663e06663c00, // 6
    0x1818183030667e00, // 7
    0x3c66663c66663c00, // 8
    0x3c66607c66663c00 // 9
    
};

// static const size_t symbols_size = sizeof(symbols) - sizeof(uint64_t) * CASCADE_SIZE;

uint64_t shift_right(uint64_t val)
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
        shifted[i] = original[i] << 1;
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

uint64_t shift_left(uint64_t val)
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
        shifted[i] = original[i] >> 1;
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

uint64_t blink_hour(uint64_t val, bool blink_flag)
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
    blinked[2] = blink_flag ? original[2] ^ 0b10000000 : original[2];
    blinked[3] = blink_flag ? original[3] ^ 0b10000000 : original[3];
    blinked[5] = blink_flag ? original[5] ^ 0b10000000 : original[5];
    blinked[6] = blink_flag ? original[6] ^ 0b10000000 : original[6];

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

uint64_t blink_minutes(uint64_t val, bool blink_flag)
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
    blinked[2] = blink_flag ? original[2] ^ 0b1 : original[2];
    blinked[3] = blink_flag ? original[3] ^ 0b1 : original[3];
    blinked[5] = blink_flag ? original[5] ^ 0b1 : original[5];
    blinked[6] = blink_flag ? original[6] ^ 0b1 : original[6];

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


        time_t now;
        char strftime_buf[64];
        struct tm timeinfo;
        
        time(&now);
        // Set timezone to Portugal
        setenv("TZ", "WET0WEST,M3.5.0/1,M10.5.0", 1);
        tzset();
        
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "The current date/time in Porto is: %s", strftime_buf);

        int hours = timeinfo.tm_hour;
        int minutes = timeinfo.tm_min;
        ESP_LOGI(TAG, "The current time is: %02d:%02d", hours, minutes);

        uint64_t current_time[4];
        current_time[0] = digits[hours / 10];
        current_time[1] = shift_left(digits[hours % 10]);
        current_time[2] = shift_right(digits[minutes / 10]);
        current_time[3] = digits[minutes % 10];

        current_time[1] = blink_hour(current_time[1], blink_flag);
        current_time[2] = blink_minutes(current_time[2], blink_flag);
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
