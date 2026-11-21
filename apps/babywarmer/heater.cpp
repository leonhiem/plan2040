/**
 * apps/babywarmer/heater.cpp — low-level heater output only
 *
 * From picoos @ 89c16e5 (heater.cpp). plan2040: pins from board.h,
 * constants from heater.h, and heater_pwm_init() (the PWM setup picoos
 * did in setup_gpios()). The rest unchanged. Original header (tpo_apply()
 * is registered by dev/heater.cpp now, not by main()):
 *
 * The PID controller, the phase state machine (task_pidctrl), the
 * feed-forward tables, and the current-sense safety check
 * (heater_check_task/task_check) are gone -- this is the experimental
 * line, ripped out on purpose. What's left is tpo_apply(): the actual
 * hardware primitive that turns a 0-100 power value into SSR on/off
 * timing and a PWM bargraph level. dev/heater.cpp's heater_set_power()
 * sets that value; tpo_apply() itself is registered as a task from
 * main() to actually apply it on a schedule.
 */
#include "heater.h"
#include "board.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"

void heater_pwm_init(void)
{
    // From picoos's setup_gpios(), unchanged. board_pins_init() held the
    // pin low as a plain output until now.
    gpio_set_function(PIN_SSR_BARGRPH, GPIO_FUNC_PWM);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&config, 1); // fastest divider available
    pwm_config_set_wrap(&config, PWM_WRAP_VAL); // ~3kHz
    pwm_init(PWM_SLICE_NUM, &config, true);

    pwm_set_chan_level(PWM_SLICE_NUM, PWM_CHAN_A, 0);
    pwm_set_enabled(PWM_SLICE_NUM, true);
}

static float heaterpower = 0.0f; // 0-100; set via heater_set_power()

void heater_set_power(float pct)
{
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    heaterpower = pct;
}

float heater_get_power(void)
{
    return heaterpower;
}

/* ═══════════════════════════════════════════════════
   tpo_apply()
   Time-Proportional Output — call once per TICK_MS.
   Converts heaterpower (0–100) to SSR on/off pattern
   over a TPO_PERIOD_MS window.
   ═══════════════════════════════════════════════════ */
void tpo_apply(void)
{
    static int tick = 0;

    int on_ticks = (int)((heaterpower / 100.0f) * TPO_TICKS + 0.5f);
    gpio_put(PIN_SSR, (tick < on_ticks) ? 1 : 0);
    if (++tick >= TPO_TICKS) tick = 0;

    // update PWM bargraph display
    float hp = heaterpower * (float)PWM_WRAP_VAL / 100.0;
    pwm_set_chan_level(PWM_SLICE_NUM, PWM_CHAN_A, (uint16_t)(hp));
}
