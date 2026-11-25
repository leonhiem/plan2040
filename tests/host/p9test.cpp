// tests/host/p9test.cpp — 9P messages (9p/fcall) and framing (9p/frame),
// no hardware: every message type there and back, bad messages refused,
// frames through noise, cut-off frames, garbled bytes. (2026-11-26)
#include "9p/fcall.h"
#include "9p/frame.h"
#include <cstdio>
#include <cstring>

static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)

static void hex(const char *what, const uint8_t *p, unsigned n)
{
    printf("%s (%u):", what, n);
    for (unsigned i = 0; i < n; i++) printf(" %02x", p[i]);
    printf("\n");
}

static uint32_t seed = 1;
static uint8_t rnd(void) { seed = seed * 1103515245 + 12345; return (uint8_t)(seed >> 16); }

// Fcall -> bytes -> Fcall: the same message, said in words both times.
static void roundtrip(const Fcall *f)
{
    uint8_t buf[P9_MSIZE + 64];
    unsigned n = convS2M(f, buf, sizeof buf);
    CHECK(n > 0 && n == sizeS2M(f));
    char a[400], b[400];
    fcallfmt(a, sizeof a, f);
    Fcall g;
    CHECK(convM2S(buf, n, &g) == n);
    fcallfmt(b, sizeof b, &g);
    printf("%-3u %s\n", n, a);
    if (strcmp(a, b)) { printf("FAIL back as: %s\n", b); fails++; }
    if (f->type == Rread || f->type == Twrite) CHECK(g.count == f->count && !memcmp(g.data, f->data, f->count));
    if (f->type == Rstat || f->type == Twstat) CHECK(g.nstat == f->nstat && !memcmp(g.stat, f->stat, f->nstat));
}

static void messages(void)
{
    printf("-- every message type, there and back\n");
    Fcall f;
    Qid q = { 0x1122334455667788ull, 7, QTDIR };
    memset(&f, 0, sizeof f); f.type = Tversion; f.tag = NOTAG; f.msize = P9_MSIZE; f.version = P9_VERSION; roundtrip(&f);
    f.type = Rversion; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Tauth; f.tag = 1; f.afid = 5; f.uname = "leon"; f.aname = ""; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Rauth; f.tag = 1; f.aqid = q; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Tattach; f.tag = 2; f.fid = 0; f.afid = NOFID; f.uname = "alpha"; f.aname = ""; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Rattach; f.tag = 2; f.qid = q; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Rerror; f.tag = 3; f.ename = "ADS1115 (0x48, header bus) not answering"; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Tflush; f.tag = 4; f.oldtag = 3; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Rflush; f.tag = 4; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Twalk; f.tag = 5; f.fid = 0; f.newfid = 1; f.nwname = 2;
    f.wname[0] = "dev"; f.wname[1] = "skintemp"; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Twalk; f.tag = 5; f.fid = 0; f.newfid = 0; roundtrip(&f);   // nwname 0: clone
    memset(&f, 0, sizeof f); f.type = Rwalk; f.tag = 5; f.nwqid = 2; f.wqid[0] = q; f.wqid[1] = q; f.wqid[1].type = QTFILE; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Topen; f.tag = 6; f.fid = 1; f.mode = OREAD; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Ropen; f.tag = 6; f.qid = q; f.iounit = 1024; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Tcreate; f.tag = 7; f.fid = 1; f.name = "log"; f.perm = 0664; f.mode = OWRITE|OTRUNC; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Rcreate; f.tag = 7; f.qid = q; f.iounit = 1024; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Tread; f.tag = 8; f.fid = 1; f.offset = 0x100000000ull; f.count = 1024; roundtrip(&f);
    static const char bin[] = "36.8\n\0\x01\xff end";
    memset(&f, 0, sizeof f); f.type = Rread; f.tag = 8; f.count = sizeof bin; f.data = bin; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Twrite; f.tag = 9; f.fid = 1; f.offset = 5; f.count = sizeof bin; f.data = bin; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Rwrite; f.tag = 9; f.count = sizeof bin; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Tclunk; f.tag = 10; f.fid = 1; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Rclunk; f.tag = 10; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Tremove; f.tag = 11; f.fid = 1; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Rremove; f.tag = 11; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Tstat; f.tag = 12; f.fid = 1; roundtrip(&f);
    static const uint8_t st[] = { 4, 0, 1, 2, 3, 4 };
    memset(&f, 0, sizeof f); f.type = Rstat; f.tag = 12; f.nstat = sizeof st; f.stat = st; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Twstat; f.tag = 13; f.fid = 1; f.nstat = sizeof st; f.stat = st; roundtrip(&f);
    memset(&f, 0, sizeof f); f.type = Rwstat; f.tag = 13; roundtrip(&f);

    // The full 16 names in one walk; and a 1 KB write, our msize exactly.
    memset(&f, 0, sizeof f); f.type = Twalk; f.tag = 14; f.fid = 0; f.newfid = 2; f.nwname = MAXWELEM;
    for (int i = 0; i < MAXWELEM; i++) f.wname[i] = "n";
    roundtrip(&f);
    static char kb[1024]; for (int i = 0; i < 1024; i++) kb[i] = (char)i;
    memset(&f, 0, sizeof f); f.type = Twrite; f.tag = 15; f.fid = 1; f.count = 1024; f.data = kb;
    CHECK(sizeS2M(&f) == 1024 + 23 && sizeS2M(&f) <= P9_MSIZE); roundtrip(&f);   // IOHDRSZ 24 has a spare byte, as in Plan 9
}

static void exact_bytes(void)
{
    printf("-- the bytes themselves\n");
    Fcall f; memset(&f, 0, sizeof f);
    f.type = Tversion; f.tag = NOTAG; f.msize = P9_MSIZE; f.version = P9_VERSION;
    uint8_t buf[64];
    unsigned n = convS2M(&f, buf, sizeof buf);
    hex("Tversion", buf, n);
    static const uint8_t want[] = { 0x13,0,0,0, 0x64, 0xff,0xff, 0x18,0x04,0,0, 6,0, '9','P','2','0','0','0' };
    CHECK(n == sizeof want && !memcmp(buf, want, n));

    memset(&f, 0, sizeof f); f.type = Twalk; f.tag = 1; f.fid = 0; f.newfid = 1; f.nwname = 2;
    f.wname[0] = "dev"; f.wname[1] = "rtc";
    n = convS2M(&f, buf, sizeof buf);
    hex("Twalk dev rtc", buf, n);
    // In place: the strings get their nul by moving down over their length.
    Fcall g; CHECK(convM2S(buf, n, &g) == n);
    CHECK(!strcmp(g.wname[0], "dev") && !strcmp(g.wname[1], "rtc"));
    CHECK(g.wname[0] > (const char *)buf && g.wname[1] < (const char *)buf + n);
}

static void bad_messages(void)
{
    printf("-- bad messages are refused\n");
    Fcall f; memset(&f, 0, sizeof f);
    f.type = Twalk; f.tag = 1; f.fid = 0; f.newfid = 1; f.nwname = 2; f.wname[0] = "dev"; f.wname[1] = "rtc";
    uint8_t good[64], buf[64];
    unsigned n = convS2M(&f, good, sizeof good);
    Fcall g;
    int refused = 0;
    for (unsigned k = 0; k < n; k++) {          // every message cut short, size field fixed up
        memcpy(buf, good, k); PBIT32(buf, k);
        if (k < 4 || convM2S(buf, k, &g) == 0) refused++;
    }
    printf("cut short: %d of %u refused\n", refused, n); CHECK(refused == (int)n);
    memcpy(buf, good, n); buf[n] = 0; PBIT32(buf, n + 1);
    CHECK(convM2S(buf, n + 1, &g) == 0);        // a byte left over
    memcpy(buf, good, n);
    CHECK(convM2S(buf, n - 1, &g) == 0);        // size says more than there is
    memcpy(buf, good, n); buf[4] = Terror;
    CHECK(convM2S(buf, n, &g) == 0);            // Terror doesn't exist
    memcpy(buf, good, n); buf[4] = 99;
    CHECK(convM2S(buf, n, &g) == 0);            // not 9P2000
    memcpy(buf, good, n); PBIT16(buf + 15, 17);
    CHECK(convM2S(buf, n, &g) == 0);            // nwname 17
    memcpy(buf, good, n); PBIT16(buf + 17, 200);
    CHECK(convM2S(buf, n, &g) == 0);            // a string longer than the message
    // A Tread whose count says 4 GB: harmless, it's only a number; an
    // Rread claiming 4 GB of data in a short message is refused.
    memset(&f, 0, sizeof f); f.type = Rread; f.count = 3; f.data = "abc";
    n = convS2M(&f, buf, sizeof buf); PBIT32(buf + 7, 0xFFFFFFFFu);
    CHECK(convM2S(buf, n, &g) == 0);
    // Sending: too many names, no room.
    memset(&f, 0, sizeof f); f.type = Twalk; f.nwname = 17;
    CHECK(sizeS2M(&f) == 0 && convS2M(&f, buf, sizeof buf) == 0);
    memset(&f, 0, sizeof f); f.type = Rerror; f.ename = "no room for this text";
    CHECK(convS2M(&f, buf, 10) == 0);
    memset(&f, 0, sizeof f); f.type = Terror;
    CHECK(convS2M(&f, buf, sizeof buf) == 0);
}

// Feed bytes into a framer; collect what comes out.
static int feed(Framer *fr, const uint8_t *p, unsigned n, uint8_t *got, unsigned *gotn)
{
    int msgs = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned m = frame_rx(fr, p[i]);
        if (m) { memcpy(got, fr->buf, m); *gotn = m; msgs++; }
    }
    return msgs;
}

static void framing(void)
{
    printf("-- framing\n");
    const uint8_t check[] = "123456789";
    printf("crc16(\"123456789\") = %04x\n", crc16(check, 9));
    CHECK(crc16(check, 9) == 0x29B1);

    uint8_t frame[FRAME_MAX(P9_MSIZE)], got[FRAME_BUF];
    unsigned gotn = 0;
    const uint8_t m1[] = { 0x11, 0x22, 0x00, 0x33 };
    unsigned n = frame_encode(m1, sizeof m1, frame, sizeof frame);
    hex("frame of 11 22 00 33", frame, n);
    Framer fr; frame_init(&fr);
    CHECK(feed(&fr, frame, n, got, &gotn) == 1 && gotn == 4 && !memcmp(got, m1, 4));

    // Many lengths and contents: all zeros, no zeros, runs around COBS's
    // 254, random; up to our largest message. (Not 0 bytes: frame_rx's 0
    // means "no message", and no 9P message is shorter than 7 bytes.)
    static uint8_t msg[P9_MSIZE];
    int ok = 0, tried = 0;
    for (unsigned len = 1; len <= P9_MSIZE; len += (len < 600 ? 1 : 7)) {
        for (int kind = 0; kind < 4; kind++) {
            for (unsigned i = 0; i < len; i++)
                msg[i] = kind == 0 ? 0 : kind == 1 ? (uint8_t)(1 + i % 255) : kind == 2 ? 0x55 : rnd();
            n = frame_encode(msg, len, frame, sizeof frame);
            CHECK(n > 0 && n <= FRAME_MAX(len));
            gotn = 0; tried++;
            if (feed(&fr, frame, n, got, &gotn) == 1 && gotn == len && !memcmp(got, msg, len)) ok++;
            else printf("FAIL: length %u kind %d\n", len, kind), fails++;
        }
    }
    printf("there and back: %d of %d\n", ok, tried);

    // Frames run together (one 0x00 between them is enough), with noise
    // before, a frame cut off in the middle, and a garbled byte.
    uint8_t stream[4096]; unsigned s = 0;
    for (int i = 0; i < 50; i++) stream[s++] = rnd();                // noise, some zeros in it
    const uint8_t a[] = "first", b[] = "second", c[] = "third", d[] = "fourth";
    n = frame_encode(a, 5, frame, sizeof frame); memcpy(stream + s, frame, n); s += n;
    n = frame_encode(b, 6, frame, sizeof frame); memcpy(stream + s, frame, n / 2); s += n / 2;  // cut off
    n = frame_encode(c, 5, frame, sizeof frame); memcpy(stream + s, frame, n); s += n;
    n = frame_encode(d, 6, frame, sizeof frame); frame[3] ^= 0x10; memcpy(stream + s, frame, n); s += n; // garbled
    n = frame_encode(a, 5, frame, sizeof frame); memcpy(stream + s, frame + 1, n - 1); s += n - 1; // shares the 0x00
    frame_init(&fr);
    int got_msgs = 0; char seen[64] = "";
    for (unsigned i = 0; i < s; i++) {
        unsigned m = frame_rx(&fr, stream[i]);
        if (m) { got_msgs++; strncat(seen, (const char *)fr.buf, m); strcat(seen, " "); }
    }
    printf("through noise: %d messages: %s(frames %lu bad_crc %lu bad_cobs %lu too_long %lu)\n",
           got_msgs, seen, (unsigned long)fr.frames, (unsigned long)fr.bad_crc,
           (unsigned long)fr.bad_cobs, (unsigned long)fr.too_long);
    CHECK(!strcmp(seen, "first third first "));

    // Too long: more bytes than any message, then a good frame.
    frame_init(&fr);
    for (int i = 0; i < FRAME_BUF + 100; i++) frame_rx(&fr, 0x41);
    n = frame_encode(c, 5, frame, sizeof frame);
    CHECK(feed(&fr, frame, n, got, &gotn) == 1 && gotn == 5 && fr.too_long == 1);
    // No room to encode.
    CHECK(frame_encode(m1, 4, frame, 6) == 0);
    CHECK(frame_encode(m1, 4, frame, 9) == 9);
}

int main()
{
    messages();
    exact_bytes();
    bad_messages();
    framing();
    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails != 0;
}
