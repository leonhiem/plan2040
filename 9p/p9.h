/**
 * 9p/p9.h — 9P between boards: the link, the server, the client
 *
 * One link, /dev/eia0 (drivers/eia), carries 9P both ways: this board's
 * client talks to the other board's server, and the other way round,
 * over the same wire. Requests (T-messages) have even type numbers and
 * replies (R-messages) odd ones, so every frame that arrives goes to the
 * right side: T to the server (9p/srv.cpp), R to the client
 * (9p/mnt.cpp). See plan2040_ideas/IDEAS.md, section 1.
 *
 *   serve                 this board answers 9P on /dev/eia0 (serve -r: read-only)
 *   import /dev/eia0      the other board's files appear as /n/<its name>
 *   cat /n/beta/dev/rtc   ... read like local ones
 *
 * Nothing is served until `serve` says so (decided 2026-11-24): a board
 * is never open to the network by accident. Until `serve` or `import`
 * runs, /dev/eia0 stays a plain serial port.
 *
 * Plain C++ subset.
 */
#pragma once

#include <stdint.h>
#include "9p/frame.h"

// The link (9p/link.cpp).
void link_init(void);                           // registers the "9p" task
void link_start(void);                          // take /dev/eia0 for 9P
bool link_started(void);
void link_poll(void);                           // frames in -> server / client
bool link_send(const uint8_t *msg, unsigned n); // false, reason in errstr
const Framer *link_framer(void);                // counters for /dev/9p

// The server (9p/srv.cpp).
#define SRV_OFF 0
#define SRV_RW  1
#define SRV_RO  2
void srv_serve(int how);
int  srv_serving(void);
int  srv_fids(void);                             // in use
void srv_request(uint8_t *msg, unsigned n);      // a T-message arrived

// The client (9p/mnt.cpp).
int  mnt_import(const char *mountpoint, char *msg, int msglen); // 0, or -1 (msg says why)
void mnt_reply(uint8_t *msg, unsigned n);        // an R-message arrived
void mnt_tick(void);                             // retry a link that went quiet
int  mnt_status(char *buf, int len);

// dev/sysname.cpp: this board's name.
int sysname_get(char *buf, int len);
