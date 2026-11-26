/**
 * 9p/fcall.cpp — 9P2000 messages <-> Fcall (see fcall.h)
 *
 * After Plan 9's convM2S.c and convS2M.c (Bell Labs), written anew in
 * the plain C++ subset. Field layout from Plan 9's intro(5) and the
 * section-5 manual pages:
 *
 *   Tversion msize[4] version[s]        Rversion msize[4] version[s]
 *   Tauth    afid[4] uname[s] aname[s]  Rauth    aqid[13]
 *   Tattach  fid[4] afid[4] uname[s] aname[s]   Rattach qid[13]
 *                                       Rerror   ename[s]
 *   Tflush   oldtag[2]                  Rflush
 *   Twalk    fid[4] newfid[4] nwname[2] nwname*(wname[s])
 *                                       Rwalk    nwqid[2] nwqid*(qid[13])
 *   Topen    fid[4] mode[1]             Ropen    qid[13] iounit[4]
 *   Tcreate  fid[4] name[s] perm[4] mode[1]     Rcreate qid[13] iounit[4]
 *   Tread    fid[4] offset[8] count[4]  Rread    count[4] data[count]
 *   Twrite   fid[4] offset[8] count[4] data[count]  Rwrite count[4]
 *   Tclunk   fid[4]                     Rclunk
 *   Tremove  fid[4]                     Rremove
 *   Tstat    fid[4]                     Rstat    stat[n]
 *   Twstat   fid[4] stat[n]             Rwstat
 *
 * stat[n] is n[2] followed by n bytes (which begin with the stat's own
 * size[2]: two lengths in a row, as in Plan 9).
 */
#include "9p/fcall.h"
#include <cstdio>
#include <cstring>

#define BIT8SZ  1
#define BIT16SZ 2
#define BIT32SZ 4
#define BIT64SZ 8
#define HDRSZ   (BIT32SZ + BIT8SZ + BIT16SZ)   // size type tag

/* ── receiving ─────────────────────────────────────────────────────── */

// A string: len[2] bytes. Moved down one byte, over the second byte of
// its length, so it can end in a nul where its last byte was (Plan 9's
// gstring). Returns the position after it, or 0 past the end.
static uint8_t *gstring(uint8_t *p, uint8_t *ep, const char **s)
{
    if (ep - p < BIT16SZ) return 0;
    unsigned n = GBIT16(p);
    p += BIT16SZ - 1;
    if ((unsigned long)(ep - p) < 1ul + n) return 0;
    memmove(p, p + 1, n);
    p[n] = '\0';
    *s = (const char *)p;
    return p + n + 1;
}

static uint8_t *gqid(uint8_t *p, uint8_t *ep, Qid *q)
{
    if (ep - p < QIDSZ) return 0;
    q->type = GBIT8(p);
    q->vers = GBIT32(p + 1);
    q->path = GBIT64(p + 5);
    return p + QIDSZ;
}

// Fixed-size fields: check room first, then take them. Compared as a
// count, never as p + n (a count from the wire can be up to 4 G).
#define NEED(n) do { if (p == 0 || (unsigned long)(ep - p) < (unsigned long)(n)) return 0; } while (0)

unsigned convM2S(uint8_t *ap, unsigned nap, Fcall *f)
{
    if (nap < HDRSZ) return 0;
    unsigned size = GBIT32(ap);
    if (size != nap) return 0;
    uint8_t *p = ap + BIT32SZ, *ep = ap + size;

    memset(f, 0, sizeof(*f));
    f->type = GBIT8(p);  p += BIT8SZ;
    f->tag  = GBIT16(p); p += BIT16SZ;

    switch (f->type) {
    case Tversion: case Rversion:
        NEED(BIT32SZ);
        f->msize = GBIT32(p); p += BIT32SZ;
        p = gstring(p, ep, &f->version);
        break;
    case Tauth:
        NEED(BIT32SZ);
        f->afid = GBIT32(p); p += BIT32SZ;
        p = gstring(p, ep, &f->uname);
        if (p) p = gstring(p, ep, &f->aname);
        break;
    case Rauth:
        p = gqid(p, ep, &f->aqid);
        break;
    case Tattach:
        NEED(2 * BIT32SZ);
        f->fid  = GBIT32(p); p += BIT32SZ;
        f->afid = GBIT32(p); p += BIT32SZ;
        p = gstring(p, ep, &f->uname);
        if (p) p = gstring(p, ep, &f->aname);
        break;
    case Rattach:
        p = gqid(p, ep, &f->qid);
        break;
    case Rerror:
        p = gstring(p, ep, &f->ename);
        break;
    case Tflush:
        NEED(BIT16SZ);
        f->oldtag = GBIT16(p); p += BIT16SZ;
        break;
    case Twalk:
        NEED(2 * BIT32SZ + BIT16SZ);
        f->fid    = GBIT32(p); p += BIT32SZ;
        f->newfid = GBIT32(p); p += BIT32SZ;
        f->nwname = GBIT16(p); p += BIT16SZ;
        if (f->nwname > MAXWELEM) return 0;
        for (int i = 0; i < f->nwname && p; i++)
            p = gstring(p, ep, &f->wname[i]);
        break;
    case Rwalk:
        NEED(BIT16SZ);
        f->nwqid = GBIT16(p); p += BIT16SZ;
        if (f->nwqid > MAXWELEM) return 0;
        for (int i = 0; i < f->nwqid && p; i++)
            p = gqid(p, ep, &f->wqid[i]);
        break;
    case Topen:
        NEED(BIT32SZ + BIT8SZ);
        f->fid  = GBIT32(p); p += BIT32SZ;
        f->mode = GBIT8(p);  p += BIT8SZ;
        break;
    case Ropen: case Rcreate:
        p = gqid(p, ep, &f->qid);
        NEED(BIT32SZ);
        f->iounit = GBIT32(p); p += BIT32SZ;
        break;
    case Tcreate:
        NEED(BIT32SZ);
        f->fid = GBIT32(p); p += BIT32SZ;
        p = gstring(p, ep, &f->name);
        NEED(BIT32SZ + BIT8SZ);
        f->perm = GBIT32(p); p += BIT32SZ;
        f->mode = GBIT8(p);  p += BIT8SZ;
        break;
    case Tread:
        NEED(BIT32SZ + BIT64SZ + BIT32SZ);
        f->fid    = GBIT32(p); p += BIT32SZ;
        f->offset = GBIT64(p); p += BIT64SZ;
        f->count  = GBIT32(p); p += BIT32SZ;
        break;
    case Rread:
        NEED(BIT32SZ);
        f->count = GBIT32(p); p += BIT32SZ;
        NEED(f->count);
        f->data = (const char *)p; p += f->count;
        break;
    case Twrite:
        NEED(BIT32SZ + BIT64SZ + BIT32SZ);
        f->fid    = GBIT32(p); p += BIT32SZ;
        f->offset = GBIT64(p); p += BIT64SZ;
        f->count  = GBIT32(p); p += BIT32SZ;
        NEED(f->count);
        f->data = (const char *)p; p += f->count;
        break;
    case Rwrite:
        NEED(BIT32SZ);
        f->count = GBIT32(p); p += BIT32SZ;
        break;
    case Tclunk: case Tremove: case Tstat:
        NEED(BIT32SZ);
        f->fid = GBIT32(p); p += BIT32SZ;
        break;
    case Rstat:
        NEED(BIT16SZ);
        f->nstat = GBIT16(p); p += BIT16SZ;
        NEED(f->nstat);
        f->stat = p; p += f->nstat;
        break;
    case Twstat:
        NEED(BIT32SZ + BIT16SZ);
        f->fid   = GBIT32(p); p += BIT32SZ;
        f->nstat = GBIT16(p); p += BIT16SZ;
        NEED(f->nstat);
        f->stat = p; p += f->nstat;
        break;
    case Rflush: case Rclunk: case Rremove: case Rwstat:
        break;
    default:
        return 0;   // Terror, or not a 9P2000 message at all
    }
    if (p == 0 || p != ep) return 0;
    return size;
}

/* ── sending ───────────────────────────────────────────────────────── */

static unsigned slen(const char *s)
{
    return s ? (unsigned)strlen(s) : 0;
}

// BIT16SZ + the string, or a huge number for one that can't be sent.
static uint64_t ssize(const char *s)
{
    unsigned n = slen(s);
    return n > 0xFFFF ? 0x100000000ull : BIT16SZ + n;
}

unsigned sizeS2M(const Fcall *f)
{
    uint64_t n = HDRSZ;   // 64 bits: a sum of impossible sizes can't wrap
    switch (f->type) {
    case Tversion: case Rversion: n += BIT32SZ + ssize(f->version); break;
    case Tauth:    n += BIT32SZ + ssize(f->uname) + ssize(f->aname); break;
    case Rauth:    n += QIDSZ; break;
    case Tattach:  n += 2 * BIT32SZ + ssize(f->uname) + ssize(f->aname); break;
    case Rattach:  n += QIDSZ; break;
    case Rerror:   n += ssize(f->ename); break;
    case Tflush:   n += BIT16SZ; break;
    case Twalk:
        if (f->nwname > MAXWELEM) return 0;
        n += 2 * BIT32SZ + BIT16SZ;
        for (int i = 0; i < f->nwname; i++) n += ssize(f->wname[i]);
        break;
    case Rwalk:
        if (f->nwqid > MAXWELEM) return 0;
        n += BIT16SZ + f->nwqid * QIDSZ;
        break;
    case Topen:    n += BIT32SZ + BIT8SZ; break;
    case Ropen: case Rcreate: n += QIDSZ + BIT32SZ; break;
    case Tcreate:  n += BIT32SZ + ssize(f->name) + BIT32SZ + BIT8SZ; break;
    case Tread:    n += BIT32SZ + BIT64SZ + BIT32SZ; break;
    case Rread:    n += BIT32SZ + f->count; break;
    case Twrite:   n += BIT32SZ + BIT64SZ + BIT32SZ + f->count; break;
    case Rwrite:   n += BIT32SZ; break;
    case Tclunk: case Tremove: case Tstat: n += BIT32SZ; break;
    case Rstat:    n += BIT16SZ + f->nstat; break;
    case Twstat:   n += BIT32SZ + BIT16SZ + f->nstat; break;
    case Rflush: case Rclunk: case Rremove: case Rwstat: break;
    default:       return 0;
    }
    return n >= 0x7FFFFFFF ? 0 : (unsigned)n;
}

static uint8_t *pstring(uint8_t *p, const char *s)
{
    unsigned n = slen(s);
    PBIT16(p, n); p += BIT16SZ;
    if (n) memcpy(p, s, n);
    return p + n;
}

static uint8_t *pqid(uint8_t *p, const Qid *q)
{
    PBIT8(p, q->type);
    PBIT32(p + 1, q->vers);
    PBIT64(p + 5, q->path);
    return p + QIDSZ;
}

unsigned convS2M(const Fcall *f, uint8_t *ap, unsigned nap)
{
    unsigned size = sizeS2M(f);
    if (size == 0 || size > nap) return 0;

    uint8_t *p = ap;
    PBIT32(p, size);     p += BIT32SZ;
    PBIT8(p, f->type);   p += BIT8SZ;
    PBIT16(p, f->tag);   p += BIT16SZ;

    switch (f->type) {
    case Tversion: case Rversion:
        PBIT32(p, f->msize); p += BIT32SZ;
        p = pstring(p, f->version);
        break;
    case Tauth:
        PBIT32(p, f->afid); p += BIT32SZ;
        p = pstring(p, f->uname);
        p = pstring(p, f->aname);
        break;
    case Rauth:
        p = pqid(p, &f->aqid);
        break;
    case Tattach:
        PBIT32(p, f->fid);  p += BIT32SZ;
        PBIT32(p, f->afid); p += BIT32SZ;
        p = pstring(p, f->uname);
        p = pstring(p, f->aname);
        break;
    case Rattach:
        p = pqid(p, &f->qid);
        break;
    case Rerror:
        p = pstring(p, f->ename);
        break;
    case Tflush:
        PBIT16(p, f->oldtag); p += BIT16SZ;
        break;
    case Twalk:
        PBIT32(p, f->fid);    p += BIT32SZ;
        PBIT32(p, f->newfid); p += BIT32SZ;
        PBIT16(p, f->nwname); p += BIT16SZ;
        for (int i = 0; i < f->nwname; i++) p = pstring(p, f->wname[i]);
        break;
    case Rwalk:
        PBIT16(p, f->nwqid); p += BIT16SZ;
        for (int i = 0; i < f->nwqid; i++) p = pqid(p, &f->wqid[i]);
        break;
    case Topen:
        PBIT32(p, f->fid); p += BIT32SZ;
        PBIT8(p, f->mode); p += BIT8SZ;
        break;
    case Ropen: case Rcreate:
        p = pqid(p, &f->qid);
        PBIT32(p, f->iounit); p += BIT32SZ;
        break;
    case Tcreate:
        PBIT32(p, f->fid);  p += BIT32SZ;
        p = pstring(p, f->name);
        PBIT32(p, f->perm); p += BIT32SZ;
        PBIT8(p, f->mode);  p += BIT8SZ;
        break;
    case Tread:
        PBIT32(p, f->fid);    p += BIT32SZ;
        PBIT64(p, f->offset); p += BIT64SZ;
        PBIT32(p, f->count);  p += BIT32SZ;
        break;
    case Rread:
        PBIT32(p, f->count); p += BIT32SZ;
        if (f->count) memmove(p, f->data, f->count);
        p += f->count;
        break;
    case Twrite:
        PBIT32(p, f->fid);    p += BIT32SZ;
        PBIT64(p, f->offset); p += BIT64SZ;
        PBIT32(p, f->count);  p += BIT32SZ;
        if (f->count) memmove(p, f->data, f->count);
        p += f->count;
        break;
    case Rwrite:
        PBIT32(p, f->count); p += BIT32SZ;
        break;
    case Tclunk: case Tremove: case Tstat:
        PBIT32(p, f->fid); p += BIT32SZ;
        break;
    case Rstat:
        PBIT16(p, f->nstat); p += BIT16SZ;
        if (f->nstat) memmove(p, f->stat, f->nstat);
        p += f->nstat;
        break;
    case Twstat:
        PBIT32(p, f->fid);   p += BIT32SZ;
        PBIT16(p, f->nstat); p += BIT16SZ;
        if (f->nstat) memmove(p, f->stat, f->nstat);
        p += f->nstat;
        break;
    default:   // Rflush, Rclunk, Rremove, Rwstat: nothing more
        break;
    }
    return (unsigned)(p - ap) == size ? size : 0;
}

/* ── Dir: stat(5) ─────────────────────────────────────────────────── */

unsigned sizeD2M(const Dir *d)
{
    uint64_t n = STATFIXLEN - 4 * BIT16SZ;
    n += ssize(d->name) + ssize(d->uid) + ssize(d->gid) + ssize(d->muid);
    return n > 0xFFFF + BIT16SZ ? 0 : (unsigned)n;
}

unsigned convD2M(const Dir *d, uint8_t *buf, unsigned nbuf)
{
    unsigned ss = sizeD2M(d);
    if (ss == 0 || ss > nbuf) return 0;
    uint8_t *p = buf;
    PBIT16(p, ss - BIT16SZ); p += BIT16SZ;
    PBIT16(p, d->type);      p += BIT16SZ;
    PBIT32(p, d->dev);       p += BIT32SZ;
    p = pqid(p, &d->qid);
    PBIT32(p, d->mode);      p += BIT32SZ;
    PBIT32(p, d->atime);     p += BIT32SZ;
    PBIT32(p, d->mtime);     p += BIT32SZ;
    PBIT64(p, d->length);    p += BIT64SZ;
    p = pstring(p, d->name);
    p = pstring(p, d->uid);
    p = pstring(p, d->gid);
    p = pstring(p, d->muid);
    return (unsigned)(p - buf) == ss ? ss : 0;
}

// One string of a Dir, copied out with a nul. Returns the position after
// it in buf, or 0.
static const uint8_t *cstring(const uint8_t *p, const uint8_t *ep, const char **s,
                              char **strs, char *se)
{
    if (ep - p < BIT16SZ) return 0;
    unsigned n = GBIT16(p);
    p += BIT16SZ;
    if ((unsigned long)(ep - p) < n || (unsigned long)(se - *strs) < n + 1ul) return 0;
    memcpy(*strs, p, n);
    (*strs)[n] = '\0';
    *s = *strs;
    *strs += n + 1;
    return p + n;
}

unsigned convM2D(const uint8_t *buf, unsigned nbuf, Dir *d, char *strs, unsigned nstrs)
{
    if (nbuf < STATFIXLEN) return 0;
    unsigned size = GBIT16(buf) + BIT16SZ;
    if (size < STATFIXLEN || size > nbuf) return 0;
    const uint8_t *p = buf + BIT16SZ, *ep = buf + size;
    d->type   = GBIT16(p); p += BIT16SZ;
    d->dev    = GBIT32(p); p += BIT32SZ;
    d->qid.type = GBIT8(p);
    d->qid.vers = GBIT32(p + 1);
    d->qid.path = GBIT64(p + 5);
    p += QIDSZ;
    d->mode   = GBIT32(p); p += BIT32SZ;
    d->atime  = GBIT32(p); p += BIT32SZ;
    d->mtime  = GBIT32(p); p += BIT32SZ;
    d->length = GBIT64(p); p += BIT64SZ;
    char *se = strs + nstrs;
    p = cstring(p, ep, &d->name, &strs, se);
    if (p) p = cstring(p, ep, &d->uid, &strs, se);
    if (p) p = cstring(p, ep, &d->gid, &strs, se);
    if (p) p = cstring(p, ep, &d->muid, &strs, se);
    return (p && p == ep) ? size : 0;
}

/* ── a message in words ────────────────────────────────────────────── */

static const char *tname(int t)
{
    static const char *names[] = {
        "Tversion", "Rversion", "Tauth", "Rauth", "Tattach", "Rattach",
        "Terror", "Rerror", "Tflush", "Rflush", "Twalk", "Rwalk",
        "Topen", "Ropen", "Tcreate", "Rcreate", "Tread", "Rread",
        "Twrite", "Rwrite", "Tclunk", "Rclunk", "Tremove", "Rremove",
        "Tstat", "Rstat", "Twstat", "Rwstat",
    };
    return (t >= Tversion && t <= Rwstat) ? names[t - Tversion] : "?";
}

static int qidfmt(char *b, int len, const Qid *q)
{
    return snprintf(b, len, "(%llx %lu %02x)", (unsigned long long)q->path,
                    (unsigned long)q->vers, q->type);
}

int fcallfmt(char *buf, int len, const Fcall *f)
{
    int n = snprintf(buf, len, "%s tag %u", tname(f->type), f->tag);
#define ADD(...) do { if (n < len) n += snprintf(buf + n, len - n, __VA_ARGS__); } while (0)
#define ADDQID(q) do { if (n < len) n += qidfmt(buf + n, len - n, q); } while (0)
    switch (f->type) {
    case Tversion: case Rversion: ADD(" msize %lu version '%s'", (unsigned long)f->msize, f->version ? f->version : ""); break;
    case Tauth:   ADD(" afid %lu uname '%s' aname '%s'", (unsigned long)f->afid, f->uname ? f->uname : "", f->aname ? f->aname : ""); break;
    case Rauth:   ADD(" aqid "); ADDQID(&f->aqid); break;
    case Tattach: ADD(" fid %lu afid %ld uname '%s' aname '%s'", (unsigned long)f->fid,
                      f->afid == NOFID ? -1L : (long)f->afid, f->uname ? f->uname : "", f->aname ? f->aname : ""); break;
    case Rattach: ADD(" qid "); ADDQID(&f->qid); break;
    case Rerror:  ADD(" ename '%s'", f->ename ? f->ename : ""); break;
    case Tflush:  ADD(" oldtag %u", f->oldtag); break;
    case Twalk:
        ADD(" fid %lu newfid %lu nwname %u", (unsigned long)f->fid, (unsigned long)f->newfid, f->nwname);
        for (int i = 0; i < f->nwname && i < MAXWELEM; i++) ADD(" '%s'", f->wname[i] ? f->wname[i] : "");
        break;
    case Rwalk:
        ADD(" nwqid %u", f->nwqid);
        for (int i = 0; i < f->nwqid && i < MAXWELEM; i++) { ADD(" "); ADDQID(&f->wqid[i]); }
        break;
    case Topen:   ADD(" fid %lu mode %u", (unsigned long)f->fid, f->mode); break;
    case Ropen: case Rcreate: ADD(" qid "); ADDQID(&f->qid); ADD(" iounit %lu", (unsigned long)f->iounit); break;
    case Tcreate: ADD(" fid %lu name '%s' perm %lo mode %u", (unsigned long)f->fid, f->name ? f->name : "", (unsigned long)f->perm, f->mode); break;
    case Tread:   ADD(" fid %lu offset %llu count %lu", (unsigned long)f->fid, (unsigned long long)f->offset, (unsigned long)f->count); break;
    case Rread:   ADD(" count %lu", (unsigned long)f->count); break;
    case Twrite:  ADD(" fid %lu offset %llu count %lu", (unsigned long)f->fid, (unsigned long long)f->offset, (unsigned long)f->count); break;
    case Rwrite:  ADD(" count %lu", (unsigned long)f->count); break;
    case Tclunk: case Tremove: case Tstat: ADD(" fid %lu", (unsigned long)f->fid); break;
    case Rstat:   ADD(" nstat %u", f->nstat); break;
    case Twstat:  ADD(" fid %lu nstat %u", (unsigned long)f->fid, f->nstat); break;
    default: break;
    }
#undef ADD
#undef ADDQID
    return n < len ? n : len - 1;
}
