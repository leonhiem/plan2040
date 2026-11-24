/**
 * apps/babywarmer/prog/alarm.cpp — alarm <condition> [<condition> ...]
 *
 *   alarm /dev/alarm/heater /dev/alarm/temphigh /dev/alarm/templow /dev/alarm/sensor &
 *
 * Any condition reading "on" sounds the buzzer and lights the alarm LED
 * (/dev/alarm on). MUTE silences the buzzer for MUTE_MS (1 minute), never
 * the LED (/dev/alarm muted). A press arms the mute even before an alarm;
 * it ends when its minute is up or the alarm clears, so it never carries
 * over to the next alarm. Every job tick (150 ms), so MUTE answers at once.
 * Reads the MUTE button's flag directly, as adjust and toggle do.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "drivers/buttons.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>

#define MUTE_MS 60000 // matches babywarmer's own MUTE_INTERVAL_TIME (1 min)

static bool muted = false;
static bool have_muted_time = false;
static absolute_time_t muted_time;

static bool read_on(const char *path)
{
    int fd = fs_open(path);
    if (fd < 0) return false;
    char buf[4];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    return n >= 2 && strncmp(buf, "on", 2) == 0;
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

    bool active = false;
    for (int i = 0; i < argc; i++) {
        if (read_on(argv[i])) { active = true; break; }
    }

    // MUTE: read and clear the flag; it arms even before an alarm.
    if (button[BUTTON_MUTE]) {
        button[BUTTON_MUTE] = false;
        muted = true;
        muted_time = make_timeout_time_ms(MUTE_MS);
        have_muted_time = true;
    }

    if (!active) {
        muted = false;
        write_alarm("off");
        return snprintf(out, outlen, "off\n");
    }

    if (muted && have_muted_time && absolute_time_diff_us(get_absolute_time(), muted_time) <= 0) {
        muted = false; // snooze elapsed
    }

    write_alarm(muted ? "muted" : "on");
    return snprintf(out, outlen, muted ? "muted\n" : "on\n");
}

static const program_t prog_alarmctl = {"alarm", alarmctl_run};

void prog_alarm_register(void)
{
    prog_register(&prog_alarmctl);
}
