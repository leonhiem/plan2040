/**
 * 9p/frame.h — 9P messages over a byte link (a UART): framing + CRC
 *
 * 9P expects a link that delivers every message whole, in order (Plan 9
 * ran it over TCP and IL). A UART doesn't promise that: a board restarts
 * mid-message, a wire is pulled, a byte is garbled. So each message goes
 * in a frame:
 *
 *   0x00 | COBS( 9P message | crc[2] ) | 0x00
 *
 * - COBS (Consistent Overhead Byte Stuffing; Cheshire and Baker, 1997)
 *   rewrites the bytes so that 0x00 never occurs inside: 0x00 marks the
 *   frame's ends, and a receiver that lost track (it started listening
 *   mid-frame, bytes were lost) is back in step at the next 0x00. Costs
 *   1 byte per 254, at least 1.
 * - crc: CRC-16/CCITT-FALSE (polynomial 0x1021, start 0xFFFF; of
 *   "123456789": 0x29B1), little-endian like 9P, over the 9P message.
 *   A frame whose CRC is wrong is dropped, counted, and not answered;
 *   the client notices by its timeout.
 * - The 9P bytes inside are untouched: over TCP later, the same message
 *   goes without the frame.
 *
 * Plain C++ subset.
 */
#pragma once

#include <stdint.h>
#include "9p/fcall.h"

// The longest frame for an n-byte message: 2 delimiters, the CRC, COBS's
// overhead (1 code byte per 254, plus one).
#define FRAME_MAX(n) ((n) + 2 + ((n) + 2) / 254 + 1 + 2)

uint16_t crc16(const uint8_t *p, unsigned n);

// Frame msg[0..n) into out. Returns the frame's length, or 0 if out
// (max bytes) is too small.
unsigned frame_encode(const uint8_t *msg, unsigned n, uint8_t *out, unsigned max);

// Taking frames apart, one received byte at a time.
#define FRAME_BUF (P9_MSIZE + 2 + (P9_MSIZE + 2) / 254 + 2)

typedef struct {
    uint8_t  buf[FRAME_BUF];  // the frame so far; the message after a frame_rx() > 0
    unsigned n;
    bool     overflow;        // this frame is too long: dropped at its end
    // Counters, for a status file later:
    uint32_t frames;          // good messages
    uint32_t bad_crc;         // a frame arrived, its CRC was wrong
    uint32_t bad_cobs;        // not valid COBS (a garbled byte), or shorter than a CRC
    uint32_t too_long;        // longer than FRAME_BUF
} Framer;

void frame_init(Framer *f);

// Feed one byte. Returns the length of a complete, checked message now
// in f->buf (valid until the next call), or 0. (An empty message can't be
// told from "nothing yet"; no 9P message is shorter than 7 bytes.)
unsigned frame_rx(Framer *f, uint8_t c);
