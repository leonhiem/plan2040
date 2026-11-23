/**
 * apps/babywarmer/dev/ntc.cpp — the NTC table in use, and /dev/ntc/
 *
 * Step 3g, decided with Leon 2026-11-24 (TODO.md idea 3). One table for
 * both NTCs (same sensor, same bridge), from ADS1115 code to temperature
 * (ntclut.h): /dev/skintemp and /dev/ambient look up there.
 *
 *   /dev/ntc/lut     write: a calibration text (points, a bridge, an ntc
 *                    line, see ntclut.h), e.g. `cp /fram/ntc.pts /dev/ntc/lut`:
 *                    the table is made at once (or the write fails with the
 *                    reason), then written to the EEPROM and verified in the
 *                    background, page by page, and put in use when it
 *                    checks out. read: the calibration text of the table in
 *                    use, with what the generator did (`#=` lines): `cp
 *                    /dev/ntc/lut /fram/ntc.pts` gets it back to edit.
 *   /dev/ntc/status  read: which table is in use, or how far a write is.
 *   /dev/ntc/skin    read: the skin NTC's raw ADC code (ntcadc.h): what a
 *   /dev/ntc/room    calibration point needs, next to the reference
 *                    thermometer's reading.
 *
 * The table in use is in RAM (4 KB), so a reading never touches the bus.
 * At boot it's loaded from the EEPROM (~6 KB, ~0.3 s, checksums checked);
 * when the EEPROM holds no valid table (empty, damaged, a write cut off,
 * or no EEPROM on the board), the built-in table is made from
 * NTCLUT_BUILTIN (the bridge and the MF58 datasheet curve): Leon's flash
 * fallback, said once on the console.
 *
 * The EEPROM (24C512), from address 0:
 *     0  header, 64 bytes: "NTCL", version, step, entries, text length,
 *        CRC-32 of text + table, CRC-32 of the header
 *    64  the calibration text, up to NTC_TEXT_MAX bytes
 *  2112  the table, 2048 x int16 (0.01 C), little-endian
 * A write first writes the header invalid, then everything else, the
 * valid header last: a power cut halfway leaves "no valid table" (the
 * built-in one is used), never half a table. Each page waits its ~5 ms
 * on later ticks (drivers/README.md: never wait for the chip): ~1 s for
 * the whole table, the warmer running on meanwhile.
 */
#include "board.h"
#include "kernel/fs.h"
#include "kernel/task.h"
#include "kernel/errstr.h"
#include "ntclut.h"
#include "ntcadc.h"
#include "ntc.h"
#include "pico/stdlib.h"
#include <cstddef>
#include <cstdio>
#include <cstring>
#ifdef EEPROM_I2C_ADDR
#include "drivers/at24c.h"
#endif
#ifdef RTC_I2C_ADDR
#include "drivers/pcf8563.h"
#endif

#define NTC_TEXT_MAX   2048
#define HDR_SIZE       64
#define TEXT_AT        HDR_SIZE
#define TABLE_AT       (TEXT_AT + NTC_TEXT_MAX)
#define IMAGE_SIZE     (TABLE_AT + (int)sizeof(ntclut_t))
#define VERSION        1
#define WRITE_TICK_MS  10
#define PAGE_LIMIT_MS  50       // a page that takes longer: the EEPROM is gone

static ntclut_t active;                 // the table in use
static char     active_text[NTC_TEXT_MAX];
static char     where[96];              // "EEPROM" / "the built-in table (EEPROM: ...)"

// ---- the image: header + text + table, as it sits in the EEPROM --------

typedef struct {
    char     magic[4];
    uint8_t  version, step_log2;
    uint16_t entries, text_len;
    uint32_t crc_data, crc_header;
} header_t;                             // 16 bytes used of HDR_SIZE

static uint32_t crc32(uint32_t crc, const uint8_t *p, int n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}

static void make_header(header_t *h, const char *text, int text_len, const ntclut_t *lut)
{
    memset(h, 0, sizeof(*h));
    memcpy(h->magic, "NTCL", 4);
    h->version = VERSION;
    h->step_log2 = NTCLUT_STEP_LOG2;
    h->entries = NTCLUT_ENTRIES;
    h->text_len = text_len;
    h->crc_data = crc32(crc32(0, (const uint8_t *)text, text_len), (const uint8_t *)lut, sizeof(*lut));
    h->crc_header = crc32(0, (const uint8_t *)h, offsetof(header_t, crc_header));
}

#ifdef EEPROM_I2C_ADDR
// A pending write: the image's pieces, assembled per page.
static ntclut_t pending;
static char     pending_text[NTC_TEXT_MAX];
static header_t pending_hdr;
static bool     hdr_valid_now;          // the header page: invalid first, valid last

static void image_bytes(int off, uint8_t *buf, int n)
{
    for (int i = 0; i < n; i++, off++) {
        uint8_t b = 0;
        if (off < HDR_SIZE) {
            if (off < (int)sizeof(header_t)) {
                header_t h = pending_hdr;
                if (!hdr_valid_now) memset(h.magic, 0, 4);
                b = ((const uint8_t *)&h)[off];
            }
        } else if (off < TABLE_AT) {
            int k = off - TEXT_AT;
            b = k < pending_hdr.text_len ? (uint8_t)pending_text[k] : 0;
        } else {
            b = ((const uint8_t *)&pending)[off - TABLE_AT];
        }
        buf[i] = b;
    }
}

static bool load_eeprom(void)
{
    header_t h;
    if (!eeprom_read(0, (uint8_t *)&h, sizeof(h))) { snprintf(where, sizeof(where), "the built-in table (%s)", errstr()); return false; }
    bool blank = true;
    for (unsigned i = 0; i < sizeof(h); i++) if (((uint8_t *)&h)[i] != 0xff) blank = false;
    if (blank) { snprintf(where, sizeof(where), "the built-in table (the EEPROM is empty)"); return false; }
    if (memcmp(h.magic, "NTCL", 4) || h.crc_header != crc32(0, (const uint8_t *)&h, offsetof(header_t, crc_header))
        || h.version != VERSION || h.step_log2 != NTCLUT_STEP_LOG2 || h.entries != NTCLUT_ENTRIES
        || h.text_len >= NTC_TEXT_MAX) {
        snprintf(where, sizeof(where), "the built-in table (no valid table in the EEPROM)");
        return false;
    }
    // Straight into the table in use (no second 6 KB of RAM): when it doesn't
    // check out, the caller makes the built-in one over it.
    if (!eeprom_read(TEXT_AT, (uint8_t *)active_text, h.text_len) ||
        !eeprom_read(TABLE_AT, (uint8_t *)&active, sizeof(active))) {
        snprintf(where, sizeof(where), "the built-in table (%s)", errstr());
        return false;
    }
    if (crc32(crc32(0, (const uint8_t *)active_text, h.text_len), (const uint8_t *)&active, sizeof(active)) != h.crc_data) {
        snprintf(where, sizeof(where), "the built-in table (the EEPROM's table is damaged: checksum)");
        return false;
    }
    active_text[h.text_len] = '\0';
    snprintf(where, sizeof(where), "the EEPROM table");
    return true;
}

// ---- writing in the background ------------------------------------------

enum { W_IDLE, W_WRITE, W_WAIT, W_VERIFY };
static int      wstate = W_IDLE;
static int      woff;                   // next byte of the image to write / verify
static int      wstep;                  // 0: header invalid, 1: the rest, 2: header valid
static absolute_time_t wstarted;
static char     wresult[96] = "";       // the last write's outcome

static void write_failed(const char *why)
{
    snprintf(wresult, sizeof(wresult), "write failed: %s; still using %s", why, where);
    printf("ntc: %s\n", wresult);
    wstate = W_IDLE;
}

static void write_task(void)
{
    uint8_t page[EEPROM_PAGE], back[EEPROM_PAGE];
    switch (wstate) {
    case W_WRITE: {
        int n = EEPROM_PAGE - woff % EEPROM_PAGE;
        if (woff + n > IMAGE_SIZE) n = IMAGE_SIZE - woff;
        image_bytes(woff, page, n);
        if (!eeprom_page_start(woff, page, n)) { write_failed(errstr()); return; }
        wstarted = get_absolute_time();
        wstate = W_WAIT;
        return;
    }
    case W_WAIT: {
        if (!eeprom_ready()) {
            if (absolute_time_diff_us(wstarted, get_absolute_time()) > PAGE_LIMIT_MS * 1000)
                write_failed("the EEPROM didn't finish a page (gone?)");
            return;
        }
        // next: step 0 is just the first page; step 1 runs from page 1 to the end;
        // step 2 is the first page again, now with the valid header.
        if (wstep == 0) { wstep = 1; woff = EEPROM_PAGE; }
        else if (wstep == 1) {
            woff += EEPROM_PAGE - woff % EEPROM_PAGE;
            if (woff >= IMAGE_SIZE) { wstep = 2; woff = 0; hdr_valid_now = true; }
        } else { wstate = W_VERIFY; woff = 0; return; }
        wstate = W_WRITE;
        return;
    }
    case W_VERIFY: {
        int n = EEPROM_PAGE;
        if (woff + n > IMAGE_SIZE) n = IMAGE_SIZE - woff;
        image_bytes(woff, page, n);
        if (!eeprom_read(woff, back, n)) { write_failed(errstr()); return; }
        if (memcmp(page, back, n)) {
            char why[48];
            snprintf(why, sizeof(why), "verify: byte 0x%04x reads back wrong", woff);
            write_failed(why);
            return;
        }
        woff += n;
        if (woff < IMAGE_SIZE) return;
        memcpy(&active, &pending, sizeof(active));
        memcpy(active_text, pending_text, pending_hdr.text_len);
        active_text[pending_hdr.text_len] = '\0';
        snprintf(where, sizeof(where), "the EEPROM table");
        snprintf(wresult, sizeof(wresult), "the new table is written, verified and in use");
        printf("ntc: %s\n", wresult);
        wstate = W_IDLE;
        return;
    }
    default:
        return;
    }
}
#endif

// ---- the files -------------------------------------------------------------

static int lut_read(char *buf, int len)
{
    return snprintf(buf, len, "%s", active_text);
}

static int lut_write(const char *buf, int len)
{
#ifdef EEPROM_I2C_ADDR
    if (wstate != W_IDLE) { werrstr("still writing the last table (cat /dev/ntc/status)"); return -1; }
    // Made straight into the pending table (only RAM: the table in use stays
    // until the new one is written and verified).
    ntclut_t *t = &pending;
    char *text = pending_text;
#else
    static ntclut_t made;                       // no EEPROM: not straight into the table in use,
    static char made_text[NTC_TEXT_MAX];        // a failure mustn't break it
    ntclut_t *t = &made;
    char *text = made_text;
#endif
    if (!ntclut_make(buf, len, t, text, NTC_TEXT_MAX - 40)) return -1;   // errstr: why
    int n = strlen(text);
#ifdef RTC_I2C_ADDR
    rtc_time now;
    char date[24];
    if (rtc_get(&now) && !now.vl && rtc_format(date, sizeof(date), &now) > 0)
        n += snprintf(text + n, NTC_TEXT_MAX - n, "#= written %s\n", date);
#endif
#ifdef EEPROM_I2C_ADDR
    make_header(&pending_hdr, pending_text, n, &pending);
    hdr_valid_now = false;
    wstep = 0;
    woff = 0;
    snprintf(wresult, sizeof(wresult), "writing");
    wstate = W_WRITE;
    return len;
#else
    memcpy(&active, t, sizeof(active));         // no EEPROM: in use until a reboot
    memcpy(active_text, text, n + 1);
    snprintf(where, sizeof(where), "a new table (no EEPROM on this board: until a reboot)");
    return len;
#endif
}

static int status_read(char *buf, int len)
{
#ifdef EEPROM_I2C_ADDR
    if (wstate != W_IDLE)
        return snprintf(buf, len, "writing to the EEPROM: %d of %d bytes%s; in use: %s\n",
                        woff, IMAGE_SIZE, wstate == W_VERIFY ? " verified" : "", where);
    if (wresult[0] && strcmp(wresult, "writing"))
        return snprintf(buf, len, "in use: %s (%s)\n", where, wresult);
#endif
    return snprintf(buf, len, "in use: %s\n", where);
}

static int code_read(int ch, char *buf, int len)
{
    int16_t code;
    if (!ntcadc_get(ch, &code)) return -1;
    return snprintf(buf, len, "%d\n", code);
}
static int skin_read(char *buf, int len) { return code_read(NTC_SKIN, buf, len); }
static int room_read(char *buf, int len) { return code_read(NTC_ROOM, buf, len); }

static const device_t dev_ntc_lut    = { "/dev/ntc/lut",    0, 0, lut_read,    lut_write };
static const device_t dev_ntc_status = { "/dev/ntc/status", 0, 0, status_read, 0 };
static const device_t dev_ntc_skin   = { "/dev/ntc/skin",   0, 0, skin_read,   0 };
static const device_t dev_ntc_room   = { "/dev/ntc/room",   0, 0, room_read,   0 };

bool ntc_temp(int16_t code, float *t)
{
    return ntclut_lookup(&active, code, t);
}

static void builtin(void)
{
    if (!ntclut_make(NTCLUT_BUILTIN, strlen(NTCLUT_BUILTIN), &active, active_text, sizeof(active_text))) {
        // Can't happen with the built-in text; say so loudly if it ever does.
        printf("ntc: the built-in table failed: %s\n", errstr());
    }
}

void dev_ntc_register(void)
{
    snprintf(where, sizeof(where), "the built-in table (no EEPROM on this board)");
#ifdef EEPROM_I2C_ADDR
    if (!load_eeprom()) builtin();
    task_register("ntcw", write_task, WRITE_TICK_MS);
#else
    builtin();
#endif
    printf("ntc: using %s\n", where);
    fs_register(&dev_ntc_lut);
    fs_register(&dev_ntc_status);
    fs_register(&dev_ntc_skin);
    fs_register(&dev_ntc_room);
}
