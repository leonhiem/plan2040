// btntest: drivers/buttons.cpp, the real debounce and hold-to-repeat, with
// a consumer polling every 150 ms as the `adjust` job does (read and clear
// the flag). UP/DOWN repeat while held (after 0.5 s, every 0.15 s); the
// other buttons never repeat. The setpoint in 0.1 C steps, from 35.0.
#include <cstdio>
#include <stdint.h>
#include "drivers/buttons.h"
#include "board.h"
#include "pico/stdlib.h"

uint64_t fake_now;
int fake_gpio[30];
void (*fake_gpio_hook)(uint, int);
repeating_timer_callback_t fake_timer_cb;

// Holds a button for ms milliseconds (then releases it for 300 ms), with
// the 5 ms timer and a 150 ms consumer; returns how many presses it saw.
static int hold(int pin, int b, int ms)
{
    int presses = 0;
    for (int t = 0; t < ms + 300; t += 5) {
        fake_gpio[pin] = t < ms ? 0 : 1;              // active low
        fake_timer_cb(0);
        if (t % 150 == 0 && button[b]) { button[b] = false; presses++; }
    }
    return presses;
}

int main()
{
    for (int i = 0; i < 30; i++) fake_gpio[i] = 1;
    buttons_init();
    struct { const char *what; int pin, b, ms; } c[] = {
        { "UP, a short press (0.1 s)", BUTTON_PIN_UP,     BUTTON_UP,     100 },
        { "UP, held 0.4 s",            BUTTON_PIN_UP,     BUTTON_UP,     400 },
        { "UP, held 1 s",              BUTTON_PIN_UP,     BUTTON_UP,     1000 },
        { "UP, held 3 s",              BUTTON_PIN_UP,     BUTTON_UP,     3000 },
        { "DOWN, held 3 s",            BUTTON_PIN_DOWN,   BUTTON_DOWN,   3000 },
        { "MANUAL, held 3 s",          BUTTON_PIN_MANUAL, BUTTON_MANUAL, 3000 },
        { "MUTE, held 3 s",            BUTTON_PIN_MUTE,   BUTTON_MUTE,   3000 },
    };
    for (auto &k : c) {
        int n = hold(k.pin, k.b, k.ms);
        if (k.b == BUTTON_UP || k.b == BUTTON_DOWN)
            printf("%-28s %2d steps: 35.0 -> %.1f C\n", k.what, n, 35.0 + (k.b == BUTTON_UP ? 0.1 : -0.1) * n);
        else
            printf("%-28s %2d press%s\n", k.what, n, n == 1 ? "" : "es");
    }
    return 0;
}
