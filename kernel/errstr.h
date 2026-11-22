/**
 * kernel/errstr.h — why the last thing failed, in words (Plan 9's errstr)
 *
 * A device, a filesystem or a program that fails says why, at the point
 * where it knows: werrstr("ADS1115 (0x48, header bus) not answering"),
 * then returns -1 as always. Whoever reports the failure (the shell, a
 * job, cat) prints errstr() with it:
 *
 *   cat: /dev/skintemp: ADS1115 (0x48, header bus) not answering
 *
 * instead of "cat: error". Leon's rule (2026-11-23): no cryptic errors;
 * say what failed and where, in words a user understands.
 *
 * One text for the whole system, not one per task: tasks are
 * cooperative, so nothing runs between a failing call and the caller
 * reading errstr(). It's only meaningful right after a failure (-1);
 * kernel/fs.cpp clears it before calling a device and fills in a
 * general text when the device failed without saying why.
 *
 * Credit: the idea and the names are Plan 9's (werrstr(2), errstr(2)),
 * from Bell Labs.
 */
#pragma once

#define ERRMAX 96   // longest reason kept (Plan 9's ERRMAX is 128)

// Set the reason, printf-style. The arguments may point into errstr()'s
// own text (to add context in front: werrstr("%s: %s", path, errstr())).
void werrstr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// The last reason set, "" if none since errstr_clear().
const char *errstr(void);

void errstr_clear(void);
