// alarmtest: the alarm job's two sounds (prog/alarm.cpp, 2026-11-26): a
// condition "on" sounds the buzzer continuously; a condition after -w at
// "warn" (the skin 1 C off the setpoint, safe mode) beeps 0.3 s every 5 s;
// one before -w at "warn", or one not listed (the room sensor), stays
// quiet. MUTE silences both for a minute, never the LED. The real job, dev/alarm.cpp, a fake clock and
// GPIO; the job run every 150 ms, as the job system does.
#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/fs.h"
#include "kernel/prog.h"
#include "board.h"
#include "drivers/buttons.h"

uint64_t fake_now = 0;
int fake_gpio[30];
void (*fake_gpio_hook)(unsigned, int);
volatile bool button[BUTTON_COUNT];
volatile uint32_t button_presses[BUTTON_COUNT];
void dev_alarm_register(void);
void prog_alarm_register(void);

static void put(const char *p, const char *v) { int fd = fs_open(p); fs_write(fd, v, strlen(v)); fs_close(fd); }

// Run the job for `ms`, printing the buzzer as a strip: one character per
// 150 ms tick, '#' sounding, '.' quiet; and the LED at the end.
static void run(const char *what, int ms)
{
    static char a0[] = "/dev/alarm/heater", a1[] = "/dev/alarm/check", a2[] = "-w",
                a3[] = "/dev/alarm/sensor", a4[] = "/dev/alarm/temphigh", a5[] = "/dev/alarm/templow";
    char *argv[] = { a0, a1, a2, a3, a4, a5 };
    const program_t *p = prog_find("alarm");
    char out[32], strip[200];
    int n = 0;
    for (int t = 0; t < ms; t += 150) {
        p->run("", 0, 6, argv, out, sizeof(out));
        if (n < (int)sizeof(strip) - 1) strip[n++] = fake_gpio[PIN_ALARM] ? '#' : '.';
        fake_now += 150000;
    }
    strip[n] = 0;
    printf("%-44s %s  LED %s\n", what, strip, fake_gpio[PIN_ALARM_LED] ? "off" : "lit");
}

int main()
{
    dev_alarm_register();
    prog_alarm_register();
    printf("alarm /dev/alarm/heater /dev/alarm/check -w /dev/alarm/sensor /dev/alarm/temphigh /dev/alarm/templow\n");
    printf("(one character per 150 ms: # buzzer sounding, . quiet)\n\n");
    run("all off", 3000);
    put("/dev/alarm/templow", "warn: skin 1.1 C below the setpoint");
    run("templow warn: beeps", 12000);
    put("/dev/alarm/templow", "on: skin 2.1 C below the setpoint");
    run("templow on: continuous", 3000);
    put("/dev/alarm/templow", "warn: skin 1.8 C below the setpoint");
    run("back to warn: beeps again", 6000);
    button[BUTTON_MUTE] = true;
    run("MUTE: quiet, LED lit", 6000);
    fake_now += 54000000;
    run("a minute after MUTE: beeps again at once", 6000);
    put("/dev/alarm/templow", "off");
    run("cleared", 3000);
    put("/dev/alarm/check", "warn: (a warning before -w)");
    run("a warn before -w: quiet", 6000);
    put("/dev/alarm/check", "off");
    put("/dev/alarm/room", "warn: room sensor: open or shorted?");
    run("room warn (not listed): quiet", 6000);
    put("/dev/alarm/room", "off");
    put("/dev/alarm/sensor", "warn: skin fell to 34.6 C in 3 s (sensor off the baby?)");
    run("sensor warn (safe, after -w): beeps", 6000);
    put("/dev/alarm/sensor", "on: no temperature sensors");
    run("sensor on (none): continuous", 3000);
    put("/dev/alarm/sensor", "off");
    put("/dev/alarm/heater", "on: no current while switched on");
    put("/dev/alarm/temphigh", "warn: skin 1.1 C above the setpoint");
    run("heater on + temphigh warn: continuous", 3000);
    return 0;
}
