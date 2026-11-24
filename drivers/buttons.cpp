/**
 * drivers/buttons.cpp — button debounce, from picoos's buttons.cpp
 *
 * Same repeating_timer_callback() logic, minus the unused start-button
 * IRQ and timer leftovers picoos still carried from the original warmer.
 */
#include "drivers/buttons.h"
#include "board.h"
#include "pico/stdlib.h"

#define DEBOUNCE_SHORT   5     // timer ticks (5ms each) ignored after an edge
#define TICK_MS          5
#define REPEAT_DELAY_MS  500   // UP/DOWN held this long start repeating ...
#define REPEAT_EVERY_MS  150   // ... a press this often

static const uint button_pin[BUTTON_COUNT] = { BUTTON_PIN_UP,
                                               BUTTON_PIN_DOWN,
                                               BUTTON_PIN_MUTE,
                                               BUTTON_PIN_MANUAL,
                                               BUTTON_PIN_START,
                                               BUTTON_PIN_LAMP };

volatile bool button[BUTTON_COUNT];
volatile uint32_t button_presses[BUTTON_COUNT];
static volatile bool button_pressed[BUTTON_COUNT];
static volatile int  button_cnt[BUTTON_COUNT];
static int held_ms[BUTTON_COUNT];

static bool repeating_timer_callback(struct repeating_timer *t)
{
    (void)t;
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if ((gpio_get(button_pin[i]) == false) && button_pressed[i]==false && button_cnt[i]==0) {
            button_pressed[i]=true; button[i]=true; button_cnt[i]=DEBOUNCE_SHORT; button_presses[i]++;
        } else if((gpio_get(button_pin[i]) == true) && button_pressed[i]==true && button_cnt[i]==0) {
            button_pressed[i]=false; button_cnt[i]=DEBOUNCE_SHORT;
        } else if(button_cnt[i]>0) button_cnt[i]--;

        // Repeat while held: UP and DOWN only.
        if (button_pressed[i] && (i == BUTTON_UP || i == BUTTON_DOWN)) {
            held_ms[i] += TICK_MS;
            if (held_ms[i] >= REPEAT_DELAY_MS + REPEAT_EVERY_MS) {
                button[i] = true;
                button_presses[i]++;
                held_ms[i] = REPEAT_DELAY_MS;
            }
        } else held_ms[i] = 0;
    }
    return true;
}

static struct repeating_timer timer;

void buttons_init(void)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        gpio_init(button_pin[i]);
        gpio_pull_up(button_pin[i]);
    }
    add_repeating_timer_ms(-5, repeating_timer_callback, NULL, &timer);
}
