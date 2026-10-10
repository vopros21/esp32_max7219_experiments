#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_err.h>
#include <button.h>

#include "buttons.h"

#define BUTTON_GPIO GPIO_NUM_0

// The driver calls back from its esp_timer task; events go through a queue so
// all mode logic runs in the display task.
static QueueHandle_t button_queue;

static void on_button(button_t *btn, button_state_t state)
{
    button_event_t ev;
    if (state == BUTTON_CLICKED)
        ev = EV_CLICK;
    else if (state == BUTTON_PRESSED_LONG)
        ev = EV_LONG;
    else if (state == BUTTON_PRESSED)
        ev = EV_PRESS;
    else if (state == BUTTON_RELEASED)
        ev = EV_RELEASE;
    else
        return;
    xQueueSend(button_queue, &ev, 0);
}

static button_t boot_button = {
    .gpio = BUTTON_GPIO,
    .internal_pull = true,
    .pressed_level = 0,
    .autorepeat = false,
    .callback = on_button,
};

void buttons_init(void)
{
    button_queue = xQueueCreate(8, sizeof(button_event_t));
    ESP_ERROR_CHECK(button_init(&boot_button));
}

bool buttons_receive(button_event_t *ev, TickType_t timeout)
{
    return xQueueReceive(button_queue, ev, timeout) == pdTRUE;
}
