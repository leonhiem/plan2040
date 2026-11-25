/**
 * 9p/link.cpp — 9P frames over /dev/eia0, both directions (see p9.h)
 *
 * link_poll() takes what the UART received, puts it through the framer,
 * and hands each whole, checked message to the server (a request) or the
 * client (a reply). It runs from the "9p" task every millisecond, and
 * from inside the client while it waits for a reply: so a board waiting
 * for an answer still answers the other board, and two boards reading
 * each other at the same moment can't get stuck waiting for each other
 * (decided 2026-11-24, "wait, but keep serving").
 */
#include "9p/p9.h"
#include "drivers/eia.h"
#include "kernel/task.h"
#include "kernel/errstr.h"

static Framer rx;
static bool started;
static bool polling;   // link_poll() is running: don't enter it twice

static uint8_t txframe[FRAME_MAX(P9_MSIZE)];

void link_start(void)
{
    if (started) return;
    frame_init(&rx);
    eia_claim("9P");
    started = true;
}

bool link_started(void) { return started; }

const Framer *link_framer(void) { return &rx; }

bool link_send(const uint8_t *msg, unsigned n)
{
    unsigned f = frame_encode(msg, n, txframe, sizeof(txframe));
    if (f == 0) { werrstr("message too long for a frame (%u bytes)", n); return false; }
    return eia_write((const char *)txframe, (int)f) == (int)f;
}

void link_poll(void)
{
    if (!started || polling) return;
    polling = true;
    static char b[64];
    int n;
    while ((n = eia_read(b, sizeof(b))) > 0) {
        for (int i = 0; i < n; i++) {
            unsigned m = frame_rx(&rx, (uint8_t)b[i]);
            if (m < 7) continue;            // nothing yet, or too short for 9P
            if ((rx.buf[4] & 1) == 0) srv_request(rx.buf, m);
            else                      mnt_reply(rx.buf, m);
        }
    }
    polling = false;
}

static void task_9p(void)
{
    link_poll();
    mnt_tick();
}

void link_init(void)
{
    task_register("9p", task_9p, 1);
}
