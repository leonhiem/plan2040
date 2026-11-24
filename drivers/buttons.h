/**
 * drivers/buttons.h — debounced push buttons (same six as babywarmer/picoos)
 *
 * A 5ms repeating timer samples every button; a press sets that
 * button's sticky button[] flag, which stays set until a consumer
 * (dev/buttons.cpp's devices, prog/adjust.cpp) reads and clears it.
 *
 * UP and DOWN repeat while held: after REPEAT_DELAY_MS (0.5 s), a press
 * every REPEAT_EVERY_MS (0.15 s), so a setpoint in 0.1 C steps (IEC
 * 60601-2-21 wants steps of at most 0.2 C) moves 2 C in ~3 s. The other
 * buttons never repeat: a held MANUAL must not flip the mode back and
 * forth.
 */
#pragma once
#include <stdint.h>

#define BUTTON_UP     0
#define BUTTON_DOWN   1
#define BUTTON_MUTE   2
#define BUTTON_MANUAL 3
#define BUTTON_START  4
#define BUTTON_LAMP   5
#define BUTTON_COUNT  6

extern volatile bool button[BUTTON_COUNT];

// Every press counted (repeats included), never cleared: a reader that
// only wants to know "was it pressed since I last looked" compares counts,
// without taking the press away from the button[] consumer (the guard
// sees MUTE this way; the `alarm` job takes button[BUTTON_MUTE]).
extern volatile uint32_t button_presses[BUTTON_COUNT];

// GPIO setup + start the debounce timer.
void buttons_init(void);
