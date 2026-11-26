/**
 * 9p/mnt.cpp — the 9P client: the other board's files as /n/<name>
 *
 *   import /dev/eia0            asks the other board its name: /n/beta
 *   import /dev/eia0 /n/bench   or a name of your own
 *   cat /n/beta/dev/rtc         walk + open + read + clunk, one burst
 *   echo on > /n/beta/dev/lamp  walk + open + write + clunk, one burst
 *
 * A mount (kernel/fs.h's mount_t) whose read and write become 9P. The
 * client picks the fids itself, so it sends all requests of an operation
 * at once and waits for all replies together: one round trip per `cat`,
 * ~2 ms at 1 Mbaud. While it waits it keeps answering the other board
 * (link_poll() runs the server too).
 *
 * When the other board doesn't answer within MNT_TIMEOUT_MS, the link is
 * "down": reads and writes fail at once, in words, instead of each
 * waiting out the timeout (rule 1), and the "9p" task tries again every
 * second (Tversion, Tattach) without waiting for anything (rule 2). When
 * the other board answers again: "/n/beta: ok again" on the console. A
 * board that restarted forgets our fids; its "unknown fid" makes the
 * client attach again and repeat the operation once.
 *
 * Plan 9's equivalent is the mount driver (devmnt) plus import(4).
 *
 * Writing a file that isn't there creates it (Tcreate), as `>` does
 * locally; `rm` is Tremove; `ls /n/beta/dev` reads the directory (its
 * entries are Dirs, stat(5)); `unmount /n/beta` undoes the import. One
 * link, one import.
 */
#include "9p/p9.h"
#include "kernel/fs.h"
#include "kernel/errstr.h"
#include "pico/time.h"
#include <cstdio>
#include <cstring>

#define MNT_TIMEOUT_MS 300
#define MNT_RETRY_MS   1000
#define MNT_NPEND      8          // requests in one burst
#define MNT_IOUNIT     (P9_MSIZE - IOHDRSZ)
#define MNT_MAXWRITE   (4 * MNT_IOUNIT)
#define ROOTFID        0

typedef struct {
    uint16_t tag;
    bool     done;
    uint8_t  type;               // the reply's
    char     err[ERRMAX];        // Rerror
    uint16_t nwqid;              // Rwalk
    char    *dest;               // Rread: where the data goes
    int      destlen;
    uint32_t count;              // Rread, Rwrite
    uint32_t msize;              // Rversion
    char     version[8];         // Rversion
} Pend;

static Pend pend[MNT_NPEND];
static int  npend;
static bool active;              // a burst is waiting: replies go to pend[]
static uint16_t nexttag = 1;
static uint32_t nextfid = 1;

static uint8_t cbuf[P9_MSIZE];   // a request being sent
static Fcall   t, rin;           // static: ~400 bytes each, kept off the stack

static char prefix[20];          // "/n/beta"
static char remote[16] = "the other board";
static bool mounted, up;
static uint32_t down_since, next_try;
static int  ping;                // retrying: 1 waits for Rversion, 2 for Rattach
static uint16_t ping_tag;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

static uint16_t newtag(void)
{
    uint16_t x = nexttag++;
    if (nexttag == NOTAG || nexttag == 0) nexttag = 1;
    return x;
}

static uint32_t newfid(void)
{
    uint32_t x = nextfid++;
    if (nextfid == NOFID || nextfid == 0) nextfid = 1;
    return x;
}

/* ── a burst: send all, wait for all ──────────────────────────────── */

static void begin(void)
{
    npend = 0;
    active = true;
}

// Send t (its type and fields filled in) as the next request of the burst.
static bool send(char *dest, int destlen)
{
    if (npend >= MNT_NPEND) { werrstr("too many requests at once"); return false; }
    t.tag = (t.type == Tversion) ? NOTAG : newtag();
    Pend *p = &pend[npend++];
    memset(p, 0, sizeof(*p));
    p->tag = t.tag;
    p->dest = dest;
    p->destlen = destlen;
    unsigned n = convS2M(&t, cbuf, sizeof(cbuf));
    if (!n) { werrstr("can't make a 9P message (type %d)", t.type); return false; }
    return link_send(cbuf, n);
}

static void went_down(void)
{
    if (!up) return;
    up = false;
    down_since = now_ms();
    next_try = down_since + MNT_RETRY_MS;
    ping = 0;
}

static bool wait_all(void)
{
    uint32_t until = now_ms() + MNT_TIMEOUT_MS;
    for (;;) {
        link_poll();
        bool all = true;
        for (int i = 0; i < npend; i++)
            if (!pend[i].done) all = false;
        if (all) { active = false; return true; }
        if ((int32_t)(now_ms() - until) >= 0) {
            active = false;
            werrstr("no answer from %s (%d ms); retrying every second",
                    remote, MNT_TIMEOUT_MS);
            went_down();
            return false;
        }
    }
}

// A send failed (the other board isn't taking bytes): the reason is in
// errstr already (drivers/eia: "CTS off: ...").
static bool send_failed(void)
{
    active = false;
    went_down();
    return false;
}

static bool usable(void)
{
    if (up) return true;
    werrstr("%s: no answer for %lu s; retrying every second", remote,
            (unsigned long)((now_ms() - down_since) / 1000));
    return false;
}

void mnt_reply(uint8_t *msg, unsigned n)
{
    if (convM2S(msg, n, &rin) != n) return;
    if (ping && rin.tag == ping_tag) {               // the background retry
        if (ping == 1 && rin.type == Rversion) {
            memset(&t, 0, sizeof(t));
            t.type = Tattach;
            t.tag = ping_tag = newtag();
            t.fid = ROOTFID;
            t.afid = NOFID;
            char me[16];
            sysname_get(me, sizeof(me));
            t.uname = me;
            t.aname = "";
            unsigned m = convS2M(&t, cbuf, sizeof(cbuf));
            ping = (m && link_send(cbuf, m)) ? 2 : 0;
        } else if (ping == 2 && rin.type == Rattach) {
            ping = 0;
            up = true;
            printf("%s: ok again\n", prefix);
        } else {
            ping = 0;                                // try again in a second
        }
        return;
    }
    if (!active) return;                             // late: its burst gave up
    for (int i = 0; i < npend; i++) {
        Pend *p = &pend[i];
        if (p->done || p->tag != rin.tag) continue;
        p->done = true;
        p->type = rin.type;
        switch (rin.type) {
        case Rerror:
            snprintf(p->err, sizeof(p->err), "%s", rin.ename);
            break;
        case Rwalk:
            p->nwqid = rin.nwqid;
            break;
        case Rread:
            p->count = rin.count < (uint32_t)p->destlen ? rin.count : (uint32_t)p->destlen;
            if (p->count) memcpy(p->dest, rin.data, p->count);
            break;
        case Rwrite:
            p->count = rin.count;
            break;
        case Rversion:
            p->msize = rin.msize;
            snprintf(p->version, sizeof(p->version), "%s", rin.version);
            break;
        }
        return;
    }
}

// The error of reply i, if it is one: true and errstr set.
static bool is_error(int i)
{
    if (pend[i].type != Rerror) return false;
    werrstr("%s", pend[i].err);
    return true;
}

/* ── attaching ─────────────────────────────────────────────────────── */

static bool attach(void)
{
    begin();
    memset(&t, 0, sizeof(t));
    t.type = Tversion;
    t.msize = P9_MSIZE;
    t.version = P9_VERSION;
    if (!send(0, 0)) return send_failed();
    memset(&t, 0, sizeof(t));
    t.type = Tattach;
    t.fid = ROOTFID;
    t.afid = NOFID;
    char me[16];
    sysname_get(me, sizeof(me));
    t.uname = me;
    t.aname = "";
    if (!send(0, 0)) return send_failed();
    up = true;                       // so wait_all() can mark it down
    if (!wait_all()) return false;
    if (is_error(0)) return false;
    if (strcmp(pend[0].version, P9_VERSION) != 0) {
        werrstr("%s doesn't speak 9P2000 (says '%s')", remote, pend[0].version);
        return false;
    }
    if (is_error(1)) return false;
    return true;
}

/* ── the mount ─────────────────────────────────────────────────────── */

// "dev/rtc" -> "dev", "rtc", in tmp. Returns the number of names, or -1.
static int split(const char *name, char *tmp, int tmplen, const char **w)
{
    snprintf(tmp, tmplen, "%s", name);
    int n = 0;
    char *p = tmp;
    while (*p) {
        if (n == MAXWELEM) return -1;
        w[n++] = p;
        char *s = strchr(p, '/');
        if (!s) break;
        *s = '\0';
        p = s + 1;
    }
    return n;
}

static bool walk_ok(int nw, const char **w)
{
    if (is_error(0)) return false;
    if (pend[0].nwqid < nw) {
        werrstr("'%s': file does not exist", w[pend[0].nwqid]);
        return false;
    }
    return true;
}

static bool lost_session(void)
{
    return pend[0].type == Rerror && !strcmp(pend[0].err, "unknown fid");
}

// len 0: does it exist? (kernel/fs.cpp asks that before opening a file)
static int mnt_read(const char *name, char *buf, int len)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!usable()) return -1;
        char tmp[FS_NAME_MAX];
        const char *w[MAXWELEM];
        int nw = split(name, tmp, sizeof(tmp), w);
        if (nw < 0) { werrstr("more than %d names in the path", MAXWELEM); return -1; }
        uint32_t fid = newfid();

        begin();
        memset(&t, 0, sizeof(t));
        t.type = Twalk; t.fid = ROOTFID; t.newfid = fid; t.nwname = nw;
        for (int i = 0; i < nw; i++) t.wname[i] = w[i];
        if (!send(0, 0)) return send_failed(), -1;
        if (len > 0) {
            memset(&t, 0, sizeof(t));
            t.type = Topen; t.fid = fid; t.mode = OREAD;
            if (!send(0, 0)) return send_failed(), -1;
            memset(&t, 0, sizeof(t));
            t.type = Tread; t.fid = fid; t.offset = 0;
            t.count = len < MNT_IOUNIT ? len : MNT_IOUNIT;
            if (!send(buf, len)) return send_failed(), -1;
        }
        memset(&t, 0, sizeof(t));
        t.type = Tclunk; t.fid = fid;
        if (!send(0, 0)) return send_failed(), -1;
        if (!wait_all()) return -1;

        if (lost_session() && attempt == 0) {      // it restarted: attach again
            if (!attach()) return -1;
            continue;
        }
        if (!walk_ok(nw, w)) return -1;
        if (len == 0) return 0;
        if (is_error(1) || is_error(2)) return -1;
        return (int)pend[2].count;
    }
    return -1;
}

// One burst: walk to w[0..nw), then open it (create == 0) or create the
// file `create` in it, write buf in up to 4 pieces, clunk. Returns the
// number of writes sent, or -1 (errstr says why).
static int write_burst(const char **w, int nw, const char *create,
                       const char *buf, int len, bool append)
{
    uint32_t fid = newfid();
    begin();
    memset(&t, 0, sizeof(t));
    t.type = Twalk; t.fid = ROOTFID; t.newfid = fid; t.nwname = nw;
    for (int i = 0; i < nw; i++) t.wname[i] = w[i];
    if (!send(0, 0)) return send_failed(), -1;
    memset(&t, 0, sizeof(t));
    if (create) {
        t.type = Tcreate; t.fid = fid; t.name = create; t.perm = 0664; t.mode = OWRITE;
    } else {
        t.type = Topen; t.fid = fid; t.mode = OWRITE | (append ? 0 : OTRUNC);
    }
    if (!send(0, 0)) return send_failed(), -1;
    int off = 0, nwrites = 0;
    do {                                        // at least one, even of 0 bytes
        int c = len - off < MNT_IOUNIT ? len - off : MNT_IOUNIT;
        memset(&t, 0, sizeof(t));
        t.type = Twrite; t.fid = fid; t.offset = off; t.count = c; t.data = buf + off;
        if (!send(0, 0)) return send_failed(), -1;
        off += c;
        nwrites++;
    } while (off < len);
    memset(&t, 0, sizeof(t));
    t.type = Tclunk; t.fid = fid;
    if (!send(0, 0)) return send_failed(), -1;
    if (!wait_all()) return -1;
    return nwrites;
}

static int write_total(int nwrites)
{
    if (is_error(1)) return -1;
    int total = 0;
    for (int i = 0; i < nwrites; i++) {
        if (is_error(2 + i)) return -1;
        total += pend[2 + i].count;
    }
    return total;
}

// `>` replaces (OTRUNC), `>>` adds; a file that isn't there yet is
// created (Tcreate) in the directory above it, as `>` does locally.
static int mnt_write(const char *name, const char *buf, int len, bool append)
{
    if (len > MNT_MAXWRITE) { werrstr("more than %d bytes in one write", MNT_MAXWRITE); return -1; }
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!usable()) return -1;
        char tmp[FS_NAME_MAX];
        const char *w[MAXWELEM];
        int nw = split(name, tmp, sizeof(tmp), w);
        if (nw < 0) { werrstr("more than %d names in the path", MAXWELEM); return -1; }

        int nwrites = write_burst(w, nw, 0, buf, len, append);
        if (nwrites < 0) return -1;
        if (lost_session() && attempt == 0) {
            if (!attach()) return -1;
            continue;
        }
        if (nw >= 2 && pend[0].type == Rwalk && pend[0].nwqid == nw - 1) {
            nwrites = write_burst(w, nw - 1, w[nw - 1], buf, len, append);
            if (nwrites < 0) return -1;
            if (!walk_ok(nw - 1, w)) return -1;
            return write_total(nwrites);
        }
        if (!walk_ok(nw, w)) return -1;
        return write_total(nwrites);
    }
    return -1;
}

// rm: walk + remove in one burst (a remove also ends the fid).
static int mnt_remove(const char *name)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!usable()) return -1;
        char tmp[FS_NAME_MAX];
        const char *w[MAXWELEM];
        int nw = split(name, tmp, sizeof(tmp), w);
        if (nw < 0) { werrstr("more than %d names in the path", MAXWELEM); return -1; }
        uint32_t fid = newfid();
        begin();
        memset(&t, 0, sizeof(t));
        t.type = Twalk; t.fid = ROOTFID; t.newfid = fid; t.nwname = nw;
        for (int i = 0; i < nw; i++) t.wname[i] = w[i];
        if (!send(0, 0)) return send_failed(), -1;
        memset(&t, 0, sizeof(t));
        t.type = Tremove; t.fid = fid;
        if (!send(0, 0)) return send_failed(), -1;
        if (!wait_all()) return -1;
        if (lost_session() && attempt == 0) {
            if (!attach()) return -1;
            continue;
        }
        if (!walk_ok(nw, w)) return -1;
        if (is_error(1)) return -1;
        return 0;
    }
    return -1;
}

/* ── listing a directory ───────────────────────────────────────────── */

// The whole directory is read at index 0 (walk, open, reads until an
// empty one, clunk) and kept here; ls then asks for entry 1, 2, ...
static uint8_t dirbuf[2048];
static unsigned dirlen;

// One request on its own, waited for.
static bool one(char *dest, int destlen)
{
    begin();
    if (!send(dest, destlen)) return send_failed();
    return wait_all();
}

static int fetch_dir(const char *dir)
{
    char tmp[FS_NAME_MAX];
    const char *w[MAXWELEM];
    int nw = dir[0] ? split(dir, tmp, sizeof(tmp), w) : 0;
    if (nw < 0) { werrstr("more than %d names in the path", MAXWELEM); return -1; }
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!usable()) return -1;
        uint32_t fid = newfid();
        dirlen = 0;
        begin();
        memset(&t, 0, sizeof(t));
        t.type = Twalk; t.fid = ROOTFID; t.newfid = fid; t.nwname = nw;
        for (int i = 0; i < nw; i++) t.wname[i] = w[i];
        if (!send(0, 0)) return send_failed(), -1;
        memset(&t, 0, sizeof(t));
        t.type = Topen; t.fid = fid; t.mode = OREAD;
        if (!send(0, 0)) return send_failed(), -1;
        if (!wait_all()) return -1;
        if (lost_session() && attempt == 0) {
            if (!attach()) return -1;
            continue;
        }
        if (!walk_ok(nw, w) || is_error(1)) return -1;
        bool ok = true;
        for (;;) {
            unsigned room = sizeof(dirbuf) - dirlen;
            if (room < STATFIXLEN) break;          // full: list what fits
            memset(&t, 0, sizeof(t));
            t.type = Tread; t.fid = fid; t.offset = dirlen;
            t.count = room < MNT_IOUNIT ? room : MNT_IOUNIT;
            if (!one((char *)dirbuf + dirlen, room)) return -1;
            if (is_error(0)) { ok = false; break; }
            if (pend[0].count == 0) break;
            dirlen += pend[0].count;
        }
        char why[ERRMAX];
        snprintf(why, sizeof(why), "%s", errstr());
        memset(&t, 0, sizeof(t));
        t.type = Tclunk; t.fid = fid;
        one(0, 0);                                 // its answer doesn't matter
        if (!ok) { werrstr("%s", why); return -1; }
        return 0;
    }
    return -1;
}

static int mnt_listdir(const char *dir, int index, char *name, int namelen, int *size)
{
    if (index == 0 && fetch_dir(dir) < 0) return -1;
    unsigned off = 0;
    for (int i = 0; off < dirlen; i++) {
        Dir d;
        char strs[96];
        unsigned n = convM2D(dirbuf + off, dirlen - off, &d, strs, sizeof(strs));
        if (n == 0) return -1;
        if (i == index) {
            snprintf(name, namelen, "%s%s", d.name, (d.mode & DMDIR) ? "/" : "");
            *size = (int)d.length;
            return 0;
        }
        off += n;
    }
    return -1;                                     // past the end
}

static int mnt_list(int index, char *name, int namelen, int *size)
{
    return mnt_listdir("", index, name, namelen, size);
}

static int mnt_format(void)
{
    werrstr("another board's filesystem can't be formatted from here");
    return -1;
}

static int mnt_df(int *free_bytes, int *total_bytes)
{
    (void)free_bytes; (void)total_bytes;
    werrstr("df over 9P: not yet");
    return -1;
}

static const mount_t mnt = {
    prefix, mnt_read, mnt_write, mnt_remove, mnt_list, mnt_format, mnt_df, 0, 0, mnt_listdir
};

/* ── import, the retry, status ─────────────────────────────────────── */

static bool valid_name(const char *s)
{
    int n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    }
    return n > 0 && n < (int)sizeof(remote);
}

int mnt_import(const char *mountpoint, char *msg, int msglen)
{
    if (mounted) { snprintf(msg, msglen, "already imported as %s", prefix); return -1; }
    const char *given = 0;
    if (mountpoint) {
        if (strncmp(mountpoint, "/n/", 3) != 0 || !valid_name(mountpoint + 3)) {
            snprintf(msg, msglen, "%s: want /n/<name> (letters, digits, '-', at most 15)", mountpoint);
            return -1;
        }
        given = mountpoint + 3;
        snprintf(remote, sizeof(remote), "%s", given);
    }
    link_start();
    bool ok = attach();
    char name[16];
    if (given) {
        snprintf(name, sizeof(name), "%s", given);
    } else {
        if (!ok) {
            if (!up) snprintf(msg, msglen, "no answer (%d ms): is the other board on, and does it run serve?", MNT_TIMEOUT_MS);
            else     snprintf(msg, msglen, "%s", errstr());
            up = false;
            return -1;
        }
        char v[32];
        int n = mnt_read("dev/sysname", v, sizeof(v) - 1);
        if (n <= 0) {
            snprintf(msg, msglen, "can't read its /dev/sysname: %s", n < 0 ? errstr() : "empty");
            return -1;
        }
        v[n] = '\0';
        int k = 0;
        while (k < n && v[k] != '\n' && v[k] != ' ' && k < (int)sizeof(name) - 1) { name[k] = v[k]; k++; }
        name[k] = '\0';
        if (!valid_name(name)) { snprintf(msg, msglen, "its name '%s' won't do for /n/", name); return -1; }
        snprintf(remote, sizeof(remote), "%s", name);
    }
    snprintf(prefix, sizeof(prefix), "/n/%s", name);
    fs_mount(&mnt);
    mounted = true;
    if (ok) {
        snprintf(msg, msglen, "%s\n", prefix);
    } else {
        up = true;
        went_down();
        snprintf(msg, msglen, "%s: no answer yet; retrying every second\n", prefix);
    }
    return 0;
}

void mnt_tick(void)
{
    if (!mounted || up) return;
    uint32_t now = now_ms();
    if ((int32_t)(now - next_try) < 0) return;
    next_try = now + MNT_RETRY_MS;
    memset(&t, 0, sizeof(t));                  // Tversion, then Tattach in mnt_reply
    t.type = Tversion;
    t.tag = ping_tag = NOTAG;
    t.msize = P9_MSIZE;
    t.version = P9_VERSION;
    unsigned m = convS2M(&t, cbuf, sizeof(cbuf));
    ping = (m && link_send(cbuf, m)) ? 1 : 0;
}

int mnt_unmount(const char *mountpoint, char *msg, int msglen)
{
    if (!mounted || strcmp(mountpoint, prefix) != 0) {
        snprintf(msg, msglen, "%s: not imported%s%s", mountpoint, mounted ? "; imported is " : "",
                 mounted ? prefix : "");
        return -1;
    }
    if (up) {                                      // tell the server: the root fid goes
        memset(&t, 0, sizeof(t));
        t.type = Tclunk; t.fid = ROOTFID;
        one(0, 0);
    }
    fs_unmount(prefix);
    mounted = false;
    up = false;
    ping = 0;
    snprintf(msg, msglen, "%s unmounted\n", prefix);
    snprintf(remote, sizeof(remote), "the other board");
    return 0;
}

int mnt_status(char *buf, int len)
{
    if (!mounted) return snprintf(buf, len, "import: nothing imported\n");
    if (up) return snprintf(buf, len, "import: %s up\n", prefix);
    return snprintf(buf, len, "import: %s down for %lu s, retrying every second\n",
                    prefix, (unsigned long)((now_ms() - down_since) / 1000));
}
