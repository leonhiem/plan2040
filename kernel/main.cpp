/**
 * kernel/main.cpp — plan2040's start: the same for every board
 *
 * plan2040 is the next version of picoos (github.com/leonhiem/picoos),
 * continued on the cpu_rp2040 board and renamed 2026-11-21 (README, "The
 * name: plan2040"). What a firmware contains is chosen by its config,
 * configs/<name>.cmake: the board (boards/<board>/: pins, board_init()),
 * and the lists of devices (dev/) and programs (prog/). The build turns
 * those lists into conf.cpp, whose conf_register() calls each one's
 * dev_<name>_register() / prog_<name>_register() in the config's order,
 * the way Plan 9's mkdevc builds a kernel's device table from its conf.
 *
 * Same rule as always: a device owns hardware, a program only ever
 * touches /dev paths.
 */

#include <pico/stdlib.h>
#include <cstdio>
#include "hardware/watchdog.h"
#include "kernel/task.h"
#include "kernel/jobs.h"
#include "board.h"

void conf_register(void); // conf.cpp, generated from the config
void task_shell(void);    // kernel/shell.cpp

int main()
{
    stdio_init_all();
    board_init();

    // Give a USB terminal a moment to attach so the banner isn't lost.
    absolute_time_t wait = make_timeout_time_ms(3000);
    while (!stdio_usb_connected() && !time_reached(wait))
        sleep_ms(50);
    sleep_ms(200);

    printf(watchdog_caused_reboot() ? "Rebooted by watchdog\n" : "Clean boot\n");

    watchdog_enable(0x7fffff, 1); // 8 seconds (is max)

    // Populate /dev, then bin/ (both from the config), then the shell
    // that lets you poke at it.
    conf_register();
    jobs_init();

    task_register("shell", task_shell, 30); // 30ms: responsive to typing

    while (1) {
        task_run();
        watchdog_update();
    }
    return 0;
}
