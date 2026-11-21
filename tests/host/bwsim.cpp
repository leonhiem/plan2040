// bwsim: the whole babywarmer controller (step 3e) on the PC, for two
// hours of simulated time (110 minutes). The real kernel scheduler (task.cpp), shell,
// jobs and the real babywarmer boot script, with every real device and
// program; only the sensors are fakes, fed by a simple thermal model:
//
//   skin' = (heater% * GAIN  -  (skin - room) * LOSS) per second
//   (100 % heats towards room + 20 C, time constant ~8 minutes; a rough
//   model, enough to walk the phases, not babywarmer's real thermal mass)
//
// Story: power-up in manual (10 %), MANUAL pressed at 1 min -> auto:
// boost, coast, pid towards the 35.0 C setpoint; UP pressed twice at
// 40 min (-> 36.0); the sensor falls off the baby at 70 min (reads the
// room); put back at 85 min; the ADS1115 stops answering at 100 min
// (both NTC devices fail: what happens then, see TODO idea 3e).
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdint.h>
#include <unistd.h>
#include "kernel/task.h"
#include "kernel/fs.h"
#include "kernel/prog.h"
#include "drivers/buttons.h"
#include "board.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

uint64_t fake_now = 0;
int fake_gpio[30];
void (*fake_gpio_hook)(uint pin, int value);
uint16_t fake_pwm_level[8][2];
i2c_inst_t *fake_i2c0, *fake_i2c1;
volatile bool button[BUTTON_COUNT];
int getchar_timeout_us(uint32_t) { return -1; }

// ---- the world
static const float ROOM = 24.0f, GAIN = 0.00040f, LOSS = 0.0020f; // 100 % -> room + 20 C
static float skin = 24.0f;       // what's under the sensor
static bool sensor_on_baby = true, adc_dead = false;
static float heater_now(void)
{
    int fd = fs_open("/dev/heater"); char b[16]; int n = fs_read(fd, b, 15); fs_close(fd);
    b[n < 0 ? 0 : n] = 0; return strtof(b, 0);
}

// ---- fake sensor devices (in place of apps/babywarmer/dev/skintemp, ambient, current)
static int skin_read(char *buf, int len)
{
    if (adc_dead) return -1;
    return snprintf(buf, len, "%.1f\n", sensor_on_baby ? skin : ROOM);
}
static int amb_read(char *buf, int len) { return adc_dead ? -1 : snprintf(buf, len, "%.1f\n", ROOM); }
static int cur_read(char *buf, int len)
{   // current flows while the SSR conducts and the relay is energized
    return snprintf(buf, len, "%d\n", (fake_gpio[PIN_SSR] && fake_gpio[PIN_HEATERSAFE]) ? 800 : 5);
}
static const device_t d_skin = {"/dev/skintemp", 0, 0, skin_read, 0};
static const device_t d_amb  = {"/dev/ambient",  0, 0, amb_read,  0};
static const device_t d_cur  = {"/dev/current",  0, 0, cur_read,  0};
static int date_run(const char*, int, int, char**, char *out, int outlen) { return snprintf(out, outlen, "Sun Nov 22 10:00:00 2026\n"); }
static const program_t p_date = {"date", date_run};

void task_shell(void); void jobs_init(void);
#define D(n) void dev_##n##_register(void);
#define P(n) void prog_##n##_register(void);
D(lamp) D(buttons) D(heater) D(relay) D(alarm) D(seg7) D(setpoint) D(heaterauto) D(percent) D(pid) D(phase) D(alarmcheck)
P(cat) P(echo) P(follow) P(toggle) P(adjust) P(select) P(pid) P(phase) P(safelut) P(alarmcheck) P(alarm) P(ledwire) P(monitor)

// The sim's own lines go to `out` (the real stdout); what the shell and
// the jobs print goes to stdout, which is sent to a scratch file while
// the ADS1115 is dead (two failing jobs print "cat: error" every 150 ms)
// and counted instead.
static FILE *out;
static FILE *quiet;
static int saved_stdout = -1;
static void sim_printf_begin(void) { fflush(stdout); }
#define SAY(...) do { sim_printf_begin(); fprintf(out, __VA_ARGS__); fflush(out); } while (0)

static void rd(const char *path, char *b, int len)
{
    int fd = fs_open(path); int n = fd < 0 ? -1 : fs_read(fd, b, len - 1); if (fd >= 0) fs_close(fd);
    if (n < 0) strcpy(b, "?"); else { b[n] = 0; char *nl = strchr(b, '\n'); if (nl) *nl = 0; }
}
static unsigned seg_big; // last 3 digit bytes latched for the big display
static uint64_t sh; static void hook(uint pin, int v)
{ static int sck, lat; if (pin == PIN_SEG_SCK) { if (v && !sck) sh = (sh << 1) | fake_gpio[PIN_SEG_SER]; sck = v; }
  if (pin == PIN_SEG_LATCH) { if (v && !lat) seg_big = (unsigned)(sh >> 32) & 0xffffff; lat = v; } }

static void status(int sec)
{
    char st[12], sp[12], ha[8], pc[12], po[12], ah[8], th[8], tl[8];
    rd("/dev/state", st, sizeof st); rd("/dev/setpoint", sp, sizeof sp); rd("/dev/heaterauto", ha, sizeof ha);
    rd("/dev/percent", pc, sizeof pc); rd("/dev/pidout", po, sizeof po);
    rd("/dev/alarm/heater", ah, sizeof ah); rd("/dev/alarm/temphigh", th, sizeof th); rd("/dev/alarm/templow", tl, sizeof tl);
    SAY("%3d:%02d  auto %-3s %-5s set %-4s skin %5.2f%s heater %5.1f pidout %-6s relay %d buzzer %d  alarms heater %-3s high %-3s low %-3s\n",
           sec / 60, sec % 60, ha, st, sp, skin, sensor_on_baby ? " " : "*", heater_now(), po,
           fake_gpio[PIN_HEATERSAFE], fake_gpio[PIN_ALARM], ah, th, tl);
}

int main()
{
    fake_gpio_hook = hook;
    out = fdopen(dup(1), "w");
    fake_gpio[PIN_ALARM_LED] = 1;
    fs_register(&d_skin); fs_register(&d_amb); fs_register(&d_cur);
    dev_lamp_register(); dev_buttons_register(); dev_heater_register(); dev_relay_register(); dev_alarm_register();
    dev_seg7_register(); dev_setpoint_register(); dev_heaterauto_register(); dev_percent_register();
    dev_pid_register(); dev_phase_register(); dev_alarmcheck_register();
    prog_register(&p_date);
    prog_cat_register(); prog_echo_register(); prog_follow_register(); prog_toggle_register(); prog_adjust_register();
    prog_select_register(); prog_pid_register(); prog_phase_register(); prog_safelut_register();
    prog_alarmcheck_register(); prog_alarm_register(); prog_ledwire_register(); prog_monitor_register();
    jobs_init();
    task_register("shell", task_shell, 30);

    const uint64_t STEP = 10000;  // 10 ms of simulated time per loop
    uint64_t next_sec = 1000000;
    int sec = 0;
    while (sec < 110 * 60) {
        task_run();
        fake_now += STEP;
        if (fake_now < next_sec) continue;
        next_sec += 1000000; sec++;
        // the world, once a second
        skin += heater_now() * GAIN - (skin - ROOM) * LOSS;
        if (sec == 60)       { SAY("--- %d:%02d press MANUAL (-> auto)\n", sec / 60, sec % 60); button[BUTTON_MANUAL] = true; }
        if (sec == 40 * 60)  { SAY("--- 40:00 press UP twice\n"); button[BUTTON_UP] = true; }
        if (sec == 40 * 60 + 1) button[BUTTON_UP] = true;
        if (sec == 70 * 60)  { SAY("--- 70:00 sensor falls off the baby (reads the room)\n"); sensor_on_baby = false; }
        if (sec == 85 * 60)  { SAY("--- 85:00 sensor back on the baby\n"); sensor_on_baby = true; }
        if (sec == 100 * 60) { SAY("--- 100:00 the ADS1115 stops answering\n"); adc_dead = true;
                               fflush(stdout); saved_stdout = dup(1); quiet = tmpfile(); dup2(fileno(quiet), 1); }
        if (sec % 60 == 0 || (sec < 120 && sec % 10 == 0)) status(sec);
    }
    char jb[8]; (void)jb;
    if (quiet) {
        fflush(stdout); dup2(saved_stdout, 1);
        rewind(quiet); char line[128]; int n = 0, errs = 0;
        while (fgets(line, sizeof line, quiet)) { n++; if (!strcmp(line, "cat: error\n")) errs++; }
        SAY("while the ADS1115 was dead: %d lines printed by the jobs, %d of them \"cat: error\"\n", n, errs);
    }
    SAY("big display bytes at the end: %06x\n", seg_big);
    return 0;
}
