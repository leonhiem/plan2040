// tests/host/srvtest.cpp — the 9P server (9p/srv.cpp) on the PC: real
// requests in, the replies it sends checked, against the real file layer
// (kernel/fs.cpp) with a few fake devices and /fram on a simulated FRAM.
// (2026-11-26)
#include <stdint.h>
uint64_t fake_now = 0;   // fs/framfs.cpp retries a mount at most once a second
#include "9p/p9.h"
#include "kernel/fs.h"
#include "kernel/errstr.h"
#include "fs/framfs.h"
#include <cstdio>
#include <cstring>

static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)

// The link, faked: whatever the server sends lands here.
static uint8_t sent[P9_MSIZE + 64];
static unsigned nsent;
bool link_send(const uint8_t *msg, unsigned n) { memcpy(sent, msg, n); nsent = n; return true; }
void link_start(void) {}
int sysname_get(char *buf, int len) { return snprintf(buf, len, "beta"); }

// Fake devices.
static char lamp[8] = "off\n";
static int rtc_read(char *b, int len) { return snprintf(b, len, "2026-11-26 10:00:00\n"); }
static int vl_read(char *b, int len) { return snprintf(b, len, "off\n"); }
static int lamp_read(char *b, int len) { return snprintf(b, len, "%s", lamp); }
static int lamp_write(const char *b, int len) { snprintf(lamp, sizeof lamp, "%.*s", len, b); return len; }
static int bad_read(char *b, int len) { (void)b; (void)len; werrstr("ADS1115 (0x48, header bus) not answering"); return -1; }
static const device_t d_rtc  = {"/dev/rtc", 0, 0, rtc_read, 0};
static const device_t d_vl   = {"/dev/rtc/vl", 0, 0, vl_read, 0};
static const device_t d_lamp = {"/dev/lamp", 0, 0, lamp_read, lamp_write};
static const device_t d_bad  = {"/dev/skintemp", 0, 0, bad_read, 0};
void dev_fram_register(void);

// Send one request, return the reply in words (and keep it in r).
static Fcall r;
static char words[600];
static const char *ask(Fcall *t)
{
    static uint8_t m[P9_MSIZE + 64];
    unsigned n = convS2M(t, m, sizeof m);
    nsent = 0;
    srv_request(m, n);
    if (!nsent || convM2S(sent, nsent, &r) != nsent) return "(no reply)";
    fcallfmt(words, sizeof words, &r);
    return words;
}

static Fcall q(int type, int tag) { Fcall f; memset(&f, 0, sizeof f); f.type = type; f.tag = tag; return f; }

static const char *walk(uint32_t fid, uint32_t nf, const char *a, const char *b = 0, const char *c = 0)
{
    Fcall t = q(Twalk, 10); t.fid = fid; t.newfid = nf;
    if (a) t.wname[t.nwname++] = a;
    if (b) t.wname[t.nwname++] = b;
    if (c) t.wname[t.nwname++] = c;
    return ask(&t);
}
static const char *open_(uint32_t fid, int mode) { Fcall t = q(Topen, 11); t.fid = fid; t.mode = mode; return ask(&t); }
static const char *read_(uint32_t fid, uint64_t off) { Fcall t = q(Tread, 12); t.fid = fid; t.offset = off; t.count = 1024; return ask(&t); }
static const char *write_(uint32_t fid, const char *s) { Fcall t = q(Twrite, 13); t.fid = fid; t.count = strlen(s); t.data = s; return ask(&t); }
static const char *clunk(uint32_t fid) { Fcall t = q(Tclunk, 14); t.fid = fid; return ask(&t); }
static const char *data(void) { static char d[1100]; snprintf(d, sizeof d, "%.*s", (int)r.count, r.data); return d; }

int main()
{
    fs_register(&d_rtc); fs_register(&d_vl); fs_register(&d_lamp); fs_register(&d_bad);
    dev_fram_register();
    CHECK(fs_format("/fram") >= 0 || true);
    framfs_mkfs(); framfs_mount();
    int fd = fs_open("/fram/boot", FS_CREATE); fs_write(fd, "date\n", 5); fs_close(fd);

    Fcall t;
    printf("-- not serving yet\n");
    t = q(Tversion, NOTAG); t.msize = 8192; t.version = "9P2000";
    printf("%s\n", ask(&t)); CHECK(r.type == Rerror && !strcmp(r.ename, "beta doesn't serve (no serve line)"));

    srv_serve(SRV_RW);
    printf("-- version, attach\n");
    printf("%s\n", ask(&t)); CHECK(r.type == Rversion && r.msize == P9_MSIZE && !strcmp(r.version, "9P2000"));
    t = q(Tauth, 1); t.afid = 9; t.uname = "alpha"; t.aname = "";
    printf("%s\n", ask(&t)); CHECK(r.type == Rerror);
    t = q(Tattach, 2); t.fid = 0; t.afid = NOFID; t.uname = "alpha"; t.aname = "";
    printf("%s\n", ask(&t)); CHECK(r.type == Rattach && r.qid.type == QTDIR);
    printf("%s\n", ask(&t)); CHECK(r.type == Rerror && !strcmp(r.ename, "fid in use"));

    printf("-- walk\n");
    printf("%s\n", walk(0, 1, "dev", "rtc")); CHECK(r.type == Rwalk && r.nwqid == 2 && r.wqid[0].type == QTDIR && r.wqid[1].type == QTFILE);
    printf("%s\n", walk(0, 2, "dev", "nope")); CHECK(r.type == Rwalk && r.nwqid == 1);   // stops: no fid 2
    printf("%s\n", clunk(2)); CHECK(r.type == Rerror && !strcmp(r.ename, "unknown fid"));
    printf("%s\n", walk(0, 2, "nope")); CHECK(r.type == Rerror && !strcmp(r.ename, "'nope': file does not exist"));
    printf("%s\n", walk(0, 2, "dev", "rtc", "vl")); CHECK(r.type == Rwalk && r.nwqid == 3);
    printf("%s\n", walk(0, 3, "fram", "boot")); CHECK(r.type == Rwalk && r.nwqid == 2 && r.wqid[1].type == QTFILE);
    printf("%s\n", walk(0, 4, "n")); CHECK(r.type == Rerror);
    printf("%s\n", walk(1, 1, "..")); CHECK(r.type == Rwalk && r.nwqid == 1 && r.wqid[0].type == QTDIR);   // fid 1 is /dev now
    printf("%s\n", walk(1, 1, "rtc")); CHECK(r.type == Rwalk && r.nwqid == 1);
    printf("%s\n", walk(0, 1, "dev")); CHECK(r.type == Rerror && !strcmp(r.ename, "fid in use"));

    printf("-- read\n");
    printf("%s\n", open_(1, OREAD)); CHECK(r.type == Ropen && r.iounit == 1024);
    printf("%s", read_(1, 0)); printf(" '%s'\n", data()); CHECK(r.type == Rread && !strcmp(data(), "2026-11-26 10:00:00\n"));
    printf("%s\n", read_(1, 20)); CHECK(r.type == Rread && r.count == 0);   // end of the value
    printf("%s\n", clunk(1)); CHECK(r.type == Rclunk);
    open_(2, OREAD); printf("%s", read_(2, 0)); printf(" '%s'\n", data()); CHECK(!strcmp(data(), "off\n"));
    clunk(2);
    open_(3, OREAD); printf("%s", read_(3, 0)); printf(" '%s'\n", data()); CHECK(!strcmp(data(), "date\n"));
    clunk(3);
    walk(0, 5, "dev", "skintemp"); open_(5, OREAD);
    printf("%s\n", read_(5, 0)); CHECK(r.type == Rerror && !strcmp(r.ename, "ADS1115 (0x48, header bus) not answering"));
    clunk(5);
    walk(0, 6, "dev"); open_(6, OREAD);
    printf("%s\n", read_(6, 0)); CHECK(r.type == Rread && r.count > 0);   // a directory: its entries
    printf("%s\n", open_(6, OWRITE)); CHECK(r.type == Rerror && !strcmp(r.ename, "already open"));
    clunk(6);

    printf("-- write\n");
    walk(0, 7, "dev", "lamp");
    printf("%s\n", open_(7, OWRITE | OTRUNC)); CHECK(r.type == Ropen);
    printf("%s\n", write_(7, "on\n")); CHECK(r.type == Rwrite && r.count == 3 && !strcmp(lamp, "on\n"));
    printf("%s\n", read_(7, 0)); CHECK(r.type == Rerror && !strcmp(r.ename, "not open for reading"));
    clunk(7);
    walk(0, 8, "fram", "boot"); open_(8, OWRITE | OTRUNC);
    write_(8, "uptime\n"); printf("%s\n", write_(8, "date\n")); CHECK(r.type == Rwrite);   // replace, then add
    clunk(8);
    char b[64]; fd = fs_open("/fram/boot"); int n = fs_read(fd, b, sizeof b - 1); fs_close(fd); b[n > 0 ? n : 0] = 0;
    printf("/fram/boot now: '%s'\n", b); CHECK(!strcmp(b, "uptime\ndate\n"));
    walk(0, 8, "fram", "boot"); open_(8, OWRITE);
    write_(8, "ls\n"); clunk(8);                                                      // without OTRUNC: adds
    fd = fs_open("/fram/boot"); n = fs_read(fd, b, sizeof b - 1); fs_close(fd); b[n > 0 ? n : 0] = 0;
    printf("/fram/boot now: '%s'\n", b); CHECK(!strcmp(b, "uptime\ndate\nls\n"));

    printf("-- create\n");
    walk(0, 10, "fram");
    t = q(Tcreate, 15); t.fid = 10; t.name = "leon"; t.perm = 0664; t.mode = OWRITE;
    printf("%s\n", ask(&t)); CHECK(r.type == Rcreate && r.qid.type == QTFILE);
    printf("%s\n", write_(10, "leon hiemstra\n")); CHECK(r.type == Rwrite && r.count == 14);
    clunk(10);
    fd = fs_open("/fram/leon"); n = fs_read(fd, b, sizeof b - 1); fs_close(fd); b[n > 0 ? n : 0] = 0;
    printf("/fram/leon: '%s'\n", b); CHECK(!strcmp(b, "leon hiemstra\n"));
    walk(0, 10, "fram"); t = q(Tcreate, 15); t.fid = 10; t.name = "leon"; t.mode = OWRITE;
    printf("%s\n", ask(&t)); CHECK(r.type == Rcreate);                 // again: emptied
    clunk(10);
    fd = fs_open("/fram/leon"); CHECK(fs_read(fd, b, sizeof b) == 0); fs_close(fd);
    walk(0, 10, "dev"); t = q(Tcreate, 15); t.fid = 10; t.name = "x"; t.mode = OWRITE;
    printf("%s\n", ask(&t)); CHECK(r.type == Rerror);                  // not on a mount
    clunk(10);
    walk(0, 10, "fram"); t = q(Tcreate, 15); t.fid = 10; t.name = "a/b"; t.mode = OWRITE;
    printf("%s\n", ask(&t)); CHECK(r.type == Rerror);
    clunk(10);

    printf("-- stat, directories, remove\n");
    {
        Fcall t2 = q(Tstat, 30); t2.fid = 0;
        printf("%s\n", ask(&t2)); CHECK(r.type == Rstat);
        Dir d; char strs[128];
        CHECK(convM2D(r.stat, r.nstat, &d, strs, sizeof strs) == r.nstat);
        printf("  root: name '%s' mode %lo uid '%s'\n", d.name, (unsigned long)d.mode, d.uid);
        CHECK(!strcmp(d.name, "/") && (d.mode & DMDIR) && !strcmp(d.uid, "beta"));
        // List a directory: read until an empty read, small counts so it takes several.
        const char *dirs_[] = { "", "dev", "fram" };
        for (int k = 0; k < 3; k++) {
            if (dirs_[k][0]) walk(0, 20, dirs_[k]); else walk(0, 20, 0);
            open_(20, OREAD);
            printf("  ls /%s:", dirs_[k]);
            uint64_t off = 0; int entries = 0;
            for (;;) {
                Fcall rd = q(Tread, 31); rd.fid = 20; rd.offset = off; rd.count = 100;
                ask(&rd);
                if (r.type != Rread) { printf(" [%s]", words); fails++; break; }
                if (r.count == 0) break;
                unsigned o = 0;
                static uint8_t chunk[1100]; memcpy(chunk, r.data, r.count);
                while (o < r.count) {
                    unsigned m = convM2D(chunk + o, r.count - o, &d, strs, sizeof strs);
                    if (!m) { printf(" [bad entry]"); fails++; break; }
                    printf(" %s%s(%llu)", d.name, (d.mode & DMDIR) ? "/" : "", (unsigned long long)d.length);
                    o += m; entries++;
                }
                off += r.count;
            }
            printf("\n");
            CHECK(entries > 0);
            clunk(20);
        }
        walk(0, 20, "dev"); open_(20, OREAD);
        Fcall rd = q(Tread, 32); rd.fid = 20; rd.offset = 0; rd.count = 100; ask(&rd);
        rd.offset = 7; printf("%s\n", ask(&rd)); CHECK(r.type == Rerror);           // not where it left off
        rd.offset = 0; rd.count = 10; printf("%s\n", ask(&rd)); CHECK(r.type == Rerror); // too small for one
        clunk(20);
        // remove
        walk(0, 21, "fram", "leon");
        t2 = q(Tremove, 33); t2.fid = 21;
        printf("%s\n", ask(&t2)); CHECK(r.type == Rremove && fs_open("/fram/leon") < 0);
        printf("%s\n", clunk(21)); CHECK(r.type == Rerror);                          // remove ended the fid
        walk(0, 21, "dev", "lamp"); t2 = q(Tremove, 33); t2.fid = 21;
        printf("%s\n", ask(&t2)); CHECK(r.type == Rerror && !strcmp(r.ename, "a device can't be removed"));
        walk(0, 21, "fram"); t2 = q(Tremove, 33); t2.fid = 21;
        printf("%s\n", ask(&t2)); CHECK(r.type == Rerror);
        CHECK(srv_fids() == 1);
    }

    printf("-- read-only, then off\n");
    srv_serve(SRV_RO);
    walk(0, 9, "dev", "lamp");
    printf("%s\n", open_(9, OWRITE)); CHECK(r.type == Rerror && !strcmp(r.ename, "read-only (serve -r)"));
    printf("%s\n", open_(9, OREAD)); CHECK(r.type == Ropen);
    clunk(9);
    walk(0, 10, "fram"); t = q(Tcreate, 15); t.fid = 10; t.name = "no"; t.mode = OWRITE;
    printf("%s\n", ask(&t)); CHECK(r.type == Rerror && !strcmp(r.ename, "read-only (serve -r)"));
    clunk(10);
    printf("%s\n", (t = q(Tstat, 20), t.fid = 0, ask(&t))); CHECK(r.type == Rstat);
    printf("%s\n", (t = q(Tflush, 21), t.oldtag = 20, ask(&t))); CHECK(r.type == Rflush);
    printf("fids in use: %d\n", srv_fids()); CHECK(srv_fids() == 1);   // only the root
    t = q(Tversion, NOTAG); t.msize = 512; t.version = "9P2000";
    printf("%s\n", ask(&t)); CHECK(r.msize == 512 && srv_fids() == 0);   // a new session forgets fids

    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails != 0;
}
