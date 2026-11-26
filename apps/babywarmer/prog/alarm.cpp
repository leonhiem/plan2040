/**
 * apps/babywarmer/prog/alarm.cpp — alarm <condition> ... [-w <condition> ...]
 *
 *   alarm /dev/alarm/heater /dev/alarm/sensor /dev/alarm/check -w /dev/alarm/temphigh /dev/alarm/templow &
 *
 * Two sounds for two priorities (as IEC 60601-1-8's high and low):
 *   - any condition reading "on" sounds the buzzer continuously;
 *   - a condition after -w reading "warn" (the skin 1 C off the setpoint)
 *     beeps: BEEP_MS every BEEP_EVERY_MS. Conditions before -w are quiet
 *     at "warn" (the room sensor: CHK only).
 * The alarm LED is lit for both (/dev/alarm on, or muted between beeps).
 * MUTE silences the buzzer for MUTE_MS (1 minute), never the LED
 * (/dev/alarm muted). A press arms the mute even before an alarm; it ends
 * when its minute is up or the alarm clears, so it never carries over to
 * the next alarm. Every job tick (150 ms), so MUTE answers at once.
 * Reads the MUTE button's flag directly, as adjust and toggle do.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "drivers/buttons.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>

#define MUTE_MS       60000 // matches babywarmer's own MUTE_INTERVAL_TIME (1 min)
#define BEEP_MS       300   // a warning's beep ...
#define BEEP_EVERY_MS 5000  // ... this often

static bool muted = false;
static bool have_muted_time = false;
static absolute_time_t muted_time;
static bool beeping;                 // a warning is beeping
static absolute_time_t beep_start;

// 2 "on", 1 "warn", 0 otherwise (or unreadable).
static int level_of(const char *path)
{
    int fd = fs_open(path);
    if (fd < 0) return 0;
    char buf[8];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n >= 2 && strncmp(buf, "on", 2) == 0) return 2;
    if (n >= 4 && strncmp(buf, "warn", 4) == 0) return 1;
    return 0;
}

static void write_alarm(const char *v)
{
    int fd = fs_open("/dev/alarm");
    if (fd < 0) return;
    fs_write(fd, v, strlen(v));
    fs_close(fd);
}

static int alarmctl_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    if (argc < 1) return -1;

    bool on = false, warn = false, beep_list = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0) { beep_list = true; continue; }
        int lv = level_of(argv[i]);
        if (lv == 2) on = true;
        else if (lv == 1 && beep_list) warn = true;
    }

    // MUTE: read and clear the flag; it arms even before an alarm.
    if (button[BUTTON_MUTE]) {
        button[BUTTON_MUTE] = false;
        muted = true;
        muted_time = make_timeout_time_ms(MUTE_MS);
        have_muted_time = true;
    }

    if (!on && !warn) {
        muted = false;
        beeping = false;
        write_alarm("off");
        return snprintf(out, outlen, "off\n");
    }

    if (muted && have_muted_time && absolute_time_diff_us(get_absolute_time(), muted_time) <= 0) {
        muted = false; // snooze elapsed
        beeping = false; // a warning beeps again at once
    }

    if (on) {
        beeping = false;
        write_alarm(muted ? "muted" : "on");
        return snprintf(out, outlen, muted ? "muted\n" : "on\n");
    }

    // A warning: a beep now and every BEEP_EVERY_MS, the LED lit throughout.
    if (!beeping) { beeping = true; beep_start = get_absolute_time(); }
    int64_t ms = absolute_time_diff_us(beep_start, get_absolute_time()) / 1000;
    bool beep = !muted && ms % BEEP_EVERY_MS < BEEP_MS;
    write_alarm(beep ? "on" : "muted");
    return snprintf(out, outlen, muted ? "muted\n" : "warn\n");
}

static const program_t prog_alarmctl = {"alarm", alarmctl_run};

void prog_alarm_register(void)
{
    prog_register(&prog_alarmctl);
}
