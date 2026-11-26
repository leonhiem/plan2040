// sptest: /dev/setpoint's range (IEC gap d; the minimum 30.0 since 2026-11-26): 30.0-37.5 C; above 37.5 (up
// to 39.0) only on purpose: UP held 3 s at 37.5 (the buttons repeat every
// 150 ms, adjust writes past the limit each time), or `echo on >
// /dev/setpoint/high` from the shell. The real dev/setpoint.cpp and the
// kernel's files, a fake clock.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdint.h>
#include "kernel/fs.h"
#include "kernel/errstr.h"

uint64_t fake_now;
int fake_gpio[30];
void (*fake_gpio_hook)(unsigned, int);
void dev_setpoint_register(void);

static float get(const char *p) { char b[16]; int fd = fs_open(p); int n = fs_read(fd, b, 15); fs_close(fd); b[n < 0 ? 0 : n] = 0; return strtof(b, 0); }
static const char *word(const char *p) { static char b[16]; int fd = fs_open(p); int n = fs_read(fd, b, 15); fs_close(fd); b[n < 0 ? 0 : n] = 0; char *nl = strchr(b, '\n'); if (nl) *nl = 0; return b; }
static void put(const char *p, const char *v) { int fd = fs_open(p); fs_write(fd, v, strlen(v)); fs_close(fd); }
// UP as adjust does it: read, add 0.1, write, every `ms` milliseconds.
static void up(int times, int ms)
{
    for (int i = 0; i < times; i++) {
        char b[16]; snprintf(b, sizeof b, "%.2f", get("/dev/setpoint") + 0.1f);
        put("/dev/setpoint", b);
        fake_now += (uint64_t)ms * 1000;
    }
}
static void show(const char *what) { printf("%-48s setpoint %.1f, high %s\n", what, get("/dev/setpoint"), word("/dev/setpoint/high")); }

int main()
{
    dev_setpoint_register();
    show("power-up");
    put("/dev/setpoint", "25"); show("echo 25 > /dev/setpoint");
    put("/dev/setpoint", "37"); show("echo 37 > /dev/setpoint");
    up(10, 1000); show("UP pressed 10 times, 1 s apart");
    up(15, 150);  show("UP held 2.25 s at the limit (repeats)");
    fake_now += 1000000;
    up(21, 150);  show("UP held 3.15 s at the limit");
    up(20, 150);  show("UP held on: up to the maximum");
    put("/dev/setpoint", "37.5"); show("echo 37.5 > /dev/setpoint");
    put("/dev/setpoint", "38"); show("echo 38 > /dev/setpoint");
    put("/dev/setpoint/high", "on"); put("/dev/setpoint", "38.5"); show("echo on > /dev/setpoint/high; echo 38.5 > ...");
    put("/dev/setpoint/high", "off"); show("echo off > /dev/setpoint/high");
    return 0;
}
