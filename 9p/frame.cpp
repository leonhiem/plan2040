/**
 * 9p/frame.cpp — framing + CRC for 9P over a byte link (see frame.h)
 *
 * COBS after Stuart Cheshire and Mary Baker, "Consistent Overhead Byte
 * Stuffing" (SIGCOMM 1997): the data is cut at every 0x00; each piece is
 * sent as a code byte (its length + 1) and its bytes, the 0x00 itself
 * left out. A code of 0xFF means 254 bytes and no 0x00 after them.
 */
#include "9p/frame.h"
#include <cstring>

uint16_t crc16(const uint8_t *p, unsigned n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)(*p++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* ── encoding ──────────────────────────────────────────────────────── */

typedef struct {
    uint8_t *out;
    unsigned max, o;     // room, next free position
    unsigned code_at;    // where the current piece's code byte goes
    uint8_t  code;       // the current piece's length + 1
    bool     full;
} Enc;

static void put(Enc *e, uint8_t c)
{
    if (e->o >= e->max) { e->full = true; return; }
    e->out[e->o++] = c;
}

static void piece_end(Enc *e)
{
    if (e->full) return;   // code_at may be past the end: write nothing more
    e->out[e->code_at] = e->code;
    e->code_at = e->o;
    put(e, 0);          // the next piece's code byte, filled in later
    e->code = 1;
}

static void cobs_byte(Enc *e, uint8_t c)
{
    if (c == 0) { piece_end(e); return; }
    put(e, c);
    if (++e->code == 0xFF) piece_end(e);
}

unsigned frame_encode(const uint8_t *msg, unsigned n, uint8_t *out, unsigned max)
{
    if (max < 3) return 0;
    Enc e = { out, max, 0, 0, 1, false };
    put(&e, 0);                       // the frame starts
    e.code_at = e.o;
    put(&e, 0);                       // first code byte
    for (unsigned i = 0; i < n && !e.full; i++)
        cobs_byte(&e, msg[i]);
    uint16_t crc = crc16(msg, n);
    cobs_byte(&e, (uint8_t)crc);
    cobs_byte(&e, (uint8_t)(crc >> 8));
    if (e.full) return 0;
    out[e.code_at] = e.code;
    put(&e, 0);                       // the frame ends
    return e.full ? 0 : e.o;
}

/* ── decoding ──────────────────────────────────────────────────────── */

// COBS back to the data, in place (the output never overtakes the
// input). The bytes between two 0x00s. Returns the length, or -1.
static int cobs_decode(uint8_t *b, unsigned n)
{
    unsigned i = 0, o = 0;
    while (i < n) {
        uint8_t code = b[i++];
        if (code == 0) return -1;
        for (unsigned k = 1; k < code; k++) {
            if (i >= n) return -1;    // the piece is cut short
            b[o++] = b[i++];
        }
        if (code < 0xFF && i < n) b[o++] = 0;
    }
    return (int)o;
}

void frame_init(Framer *f)
{
    memset(f, 0, sizeof(*f));
}

unsigned frame_rx(Framer *f, uint8_t c)
{
    if (c != 0) {
        if (f->n < FRAME_BUF) f->buf[f->n++] = c;
        else f->overflow = true;
        return 0;
    }
    // 0x00: a frame ends here (or starts: an empty frame is skipped).
    unsigned n = f->n;
    bool overflow = f->overflow;
    f->n = 0;
    f->overflow = false;
    if (n == 0) return 0;
    if (overflow) { f->too_long++; return 0; }
    int len = cobs_decode(f->buf, n);
    if (len < 2) { f->bad_cobs++; return 0; }
    unsigned m = (unsigned)len - 2;
    uint16_t crc = (uint16_t)(f->buf[m] | (f->buf[m + 1] << 8));
    if (crc16(f->buf, m) != crc) { f->bad_crc++; return 0; }
    f->frames++;
    return m;
}
