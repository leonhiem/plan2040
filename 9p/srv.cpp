/**
 * 9p/srv.cpp — the 9P server: this board's files, for the other board
 *
 * plan2040's namespace is flat: devices are whole names ("/dev/rtc",
 * "/dev/buttons/lamp"), mounts are prefixes ("/fram") with files below.
 * 9P walks one name at a time through directories, so the server makes
 * the directories up from the names: "/" holds "dev" and "fram", "/dev"
 * holds everything that starts with "/dev/". A fid is just the path it
 * stands for; walking joins names onto it, and the file layer
 * (kernel/fs.h) does the rest, exactly as for a local `cat`.
 *
 * Reading a device: it has no offset, a read gives a fresh value. A read
 * at offset 0 takes the value and remembers its length in the fid; a read
 * past that length is the end of the file (0 bytes). Plan 9's own device
 * files (/dev/time) work the same way, and it's what `cat` expects.
 *
 * Writing a file on /fram: opened with OTRUNC (`>`), the first write
 * replaces the file and later ones add to it; without OTRUNC (`>>`) every
 * write adds. A 1 KB file arrives in one Twrite (msize 1048), so /fram's
 * "old or new, never half" holds across the link.
 *
 * Errors in words: an Rerror carries the local errstr, without its own
 * path in front (the client puts the remote path there):
 *   cat: /n/beta/dev/rtc: RTC (0x51, local bus) not answering
 *
 * Creating a file (Tcreate) works on a mount (/fram).
 *
 * Not yet (step 5): directory listings, stat, remove, and walking
 * into this board's own /n mounts (hops through a board).
 */
#include "9p/p9.h"
#include "kernel/fs.h"
#include "kernel/errstr.h"
#include <cstdio>
#include <cstring>

#define SRV_NFID   8
#define SRV_PATH   64
#define SRV_IOUNIT (P9_MSIZE - IOHDRSZ)   // 1024: data in one Tread/Twrite

typedef struct {
    bool     used;
    uint32_t fid;
    char     path[SRV_PATH];   // "" is the root
    bool     isdir;
    bool     open;
    uint8_t  mode;             // Topen's
    bool     wrote;            // a write happened on this open
    uint32_t vlen;             // length of the value read at offset 0
} Sfid;

static Sfid fids[SRV_NFID];
static int serving = SRV_OFF;
static uint32_t msize = P9_MSIZE;

static uint8_t rbuf[P9_MSIZE];      // the reply
// The request and the reply being made, static: an Fcall is ~400 bytes,
// and the server can run deep in the stack (inside a client's wait, see
// 9p/link.cpp). One request at a time: link_poll() never runs twice.
static Fcall t_in, r;
static char    data[SRV_IOUNIT];    // a value read for a Tread

void srv_serve(int how) { serving = how; if (how != SRV_OFF) link_start(); }
int  srv_serving(void)  { return serving; }

int srv_fids(void)
{
    int n = 0;
    for (int i = 0; i < SRV_NFID; i++) n += fids[i].used;
    return n;
}

/* ── the namespace, as directories ─────────────────────────────────── */

static bool starts_dir(const char *name, const char *dir)   // name is below dir
{
    int n = strlen(dir);
    return strncmp(name, dir, n) == 0 && name[n] == '/';
}

static bool is_device(const char *path)
{
    for (int i = 0; i < fs_count(); i++)
        if (!strcmp(fs_name(i), path)) return true;
    return false;
}

static bool is_mount(const char *path)
{
    for (int i = 0; i < fs_mount_count(); i++)
        if (!strcmp(fs_mount_name(i), path)) return true;
    return false;
}

// A made-up directory: some device or mount lies below it.
static bool has_below(const char *path)
{
    for (int i = 0; i < fs_count(); i++)
        if (starts_dir(fs_name(i), path)) return true;
    for (int i = 0; i < fs_mount_count(); i++)
        if (starts_dir(fs_mount_name(i), path)) return true;
    return false;
}

// A file on a mount: "/fram/boot".
static bool is_mount_file(const char *path)
{
    for (int i = 0; i < fs_mount_count(); i++) {
        const char *m = fs_mount_name(i);
        if (!starts_dir(path, m)) continue;
        const char *name = path + strlen(m) + 1;
        char n[FS_NAME_MAX];
        int size;
        for (int k = 0; fs_list(m, k, n, sizeof(n), &size) >= 0; k++)
            if (!strcmp(n, name)) return true;
    }
    return false;
}

// 0: nothing there; 1: a file; 2: a directory.
static int kind(const char *path)
{
    if (path[0] == '\0') return 2;
    if (!strcmp(path, "/n") || starts_dir(path, "/n")) return 0;   // hops: not yet
    if (is_device(path)) return 1;
    if (is_mount(path) || has_below(path)) return 2;
    if (is_mount_file(path)) return 1;
    return 0;
}

static Qid qidof(const char *path, bool dir)
{
    uint64_t h = 0xcbf29ce484222325ull;              // FNV-1a, 64 bits
    for (const char *p = path; *p; p++) { h ^= (uint8_t)*p; h *= 0x100000001b3ull; }
    Qid q;
    q.path = h;
    q.vers = 0;
    q.type = dir ? QTDIR : QTFILE;
    return q;
}

/* ── replies ───────────────────────────────────────────────────────── */

static void reply(Fcall *f)
{
    unsigned n = convS2M(f, rbuf, sizeof(rbuf));
    if (n) link_send(rbuf, n);
}

static void rerror(uint16_t tag, const char *why)
{
    memset(&r, 0, sizeof(r));
    r.type = Rerror;
    r.tag = tag;
    r.ename = why;
    reply(&r);
}

// The local errstr without the local path in front ("/dev/rtc: RTC ...
// not answering" -> "RTC ... not answering").
static void rerror_errstr(uint16_t tag, const char *what)
{
    const char *e = errstr();
    if (e[0] == '/') {
        const char *c = strstr(e, ": ");
        if (c) e = c + 2;
    }
    rerror(tag, e[0] ? e : what);
}

static Sfid *findfid(uint32_t fid)
{
    for (int i = 0; i < SRV_NFID; i++)
        if (fids[i].used && fids[i].fid == fid) return &fids[i];
    return 0;
}

static Sfid *newfid(uint32_t fid)
{
    for (int i = 0; i < SRV_NFID; i++)
        if (!fids[i].used) {
            memset(&fids[i], 0, sizeof(fids[i]));
            fids[i].used = true;
            fids[i].fid = fid;
            return &fids[i];
        }
    return 0;
}

/* ── the requests ──────────────────────────────────────────────────── */

static void s_version(Fcall *t)
{
    memset(fids, 0, sizeof(fids));                    // a new session
    memset(&r, 0, sizeof(r));
    r.type = Rversion;
    r.tag = t->tag;
    msize = t->msize < P9_MSIZE ? t->msize : P9_MSIZE;
    r.msize = msize;
    r.version = strncmp(t->version, "9P2000", 6) == 0 ? P9_VERSION : "unknown";
    reply(&r);
}

static void s_attach(Fcall *t)
{
    if (findfid(t->fid)) { rerror(t->tag, "fid in use"); return; }
    if (t->afid != NOFID) { rerror(t->tag, "no authentication required"); return; }
    Sfid *f = newfid(t->fid);
    if (!f) { rerror(t->tag, "too many open files (8)"); return; }
    f->path[0] = '\0';
    f->isdir = true;
    memset(&r, 0, sizeof(r));
    r.type = Rattach;
    r.tag = t->tag;
    r.qid = qidof("", true);
    reply(&r);
}

static void s_walk(Fcall *t)
{
    Sfid *f = findfid(t->fid);
    if (!f) { rerror(t->tag, "unknown fid"); return; }
    if (f->open) { rerror(t->tag, "walk from an open fid"); return; }
    Sfid *nf = 0;
    if (t->newfid != t->fid) {
        if (findfid(t->newfid)) { rerror(t->tag, "fid in use"); return; }
    }

    char path[SRV_PATH];
    strcpy(path, f->path);
    bool dir = f->isdir;
    memset(&r, 0, sizeof(r));
    r.type = Rwalk;
    r.tag = t->tag;
    const char *why = "file does not exist";
    for (int i = 0; i < t->nwname; i++) {
        const char *w = t->wname[i];
        char next[SRV_PATH];
        if (!strcmp(w, "..")) {
            strcpy(next, path);
            char *s = strrchr(next, '/');
            if (s) *s = '\0';
        } else {
            if (!w[0] || !strcmp(w, ".") || strchr(w, '/')) { why = "not a valid name"; break; }
            if (snprintf(next, sizeof(next), "%s/%s", path, w) >= (int)sizeof(next)) { why = "name too long"; break; }
        }
        int k = kind(next);
        if (k == 0) {
            if (!strcmp(next, "/n") || starts_dir(next, "/n"))
                why = "not served (hops through a board: not yet)";
            break;
        }
        strcpy(path, next);
        dir = (k == 2);
        r.wqid[r.nwqid++] = qidof(path, dir);
    }
    if (r.nwqid == 0 && t->nwname > 0) {
        char msg[ERRMAX];
        snprintf(msg, sizeof(msg), "'%s': %s", t->wname[0], why);
        rerror(t->tag, msg);
        return;
    }
    if (r.nwqid == t->nwname) {               // all the way: the fid exists
        nf = (t->newfid == t->fid) ? f : newfid(t->newfid);
        if (!nf) { rerror(t->tag, "too many open files (8)"); return; }
        strcpy(nf->path, path);
        nf->isdir = dir;
    }
    reply(&r);   // fewer qids than names: the walk stopped there, no new fid
}

static void s_open(Fcall *t)
{
    Sfid *f = findfid(t->fid);
    if (!f) { rerror(t->tag, "unknown fid"); return; }
    if (f->open) { rerror(t->tag, "already open"); return; }
    int rw = t->mode & 3;
    bool writing = (rw == OWRITE || rw == ORDWR || (t->mode & OTRUNC));
    if (writing && f->isdir) { rerror(t->tag, "is a directory"); return; }
    if (writing && serving == SRV_RO) { rerror(t->tag, "read-only (serve -r)"); return; }
    f->open = true;
    f->mode = t->mode;
    f->wrote = false;
    f->vlen = 0;
    memset(&r, 0, sizeof(r));
    r.type = Ropen;
    r.tag = t->tag;
    r.qid = qidof(f->path, f->isdir);
    r.iounit = SRV_IOUNIT;
    reply(&r);
}

static void s_read(Fcall *t)
{
    Sfid *f = findfid(t->fid);
    if (!f) { rerror(t->tag, "unknown fid"); return; }
    if (!f->open || (f->mode & 3) == OWRITE) { rerror(t->tag, "not open for reading"); return; }
    if (f->isdir) { rerror(t->tag, "listing a directory over 9P: not yet"); return; }

    int n = 0;
    if (t->offset == 0 || t->offset < f->vlen) {
        int fd = fs_open(f->path);
        if (fd < 0) { rerror_errstr(t->tag, "can't open"); return; }
        n = fs_read(fd, data, sizeof(data));
        fs_close(fd);
        if (n < 0) { rerror_errstr(t->tag, "read failed"); return; }
        if (t->offset == 0) f->vlen = n;
    }
    uint32_t off = (uint32_t)t->offset;
    uint32_t count = 0;
    if (off < (uint32_t)n) {
        count = n - off;
        if (count > t->count) count = t->count;
        uint32_t max = msize - IOHDRSZ;
        if (count > max) count = max;
    }
    memset(&r, 0, sizeof(r));
    r.type = Rread;
    r.tag = t->tag;
    r.count = count;
    r.data = count ? data + off : data;
    reply(&r);
}

static void s_write(Fcall *t)
{
    Sfid *f = findfid(t->fid);
    if (!f) { rerror(t->tag, "unknown fid"); return; }
    if (!f->open || (f->mode & 3) == OREAD) { rerror(t->tag, "not open for writing"); return; }
    if (serving == SRV_RO) { rerror(t->tag, "read-only (serve -r)"); return; }
    int mode = FS_RDWR;                                   // devices ignore it
    if (!is_device(f->path))
        mode = ((f->mode & OTRUNC) && !f->wrote) ? FS_CREATE : FS_APPEND;
    int fd = fs_open(f->path, mode);
    if (fd < 0) { rerror_errstr(t->tag, "can't open"); return; }
    int n = fs_write(fd, t->data, t->count);
    fs_close(fd);
    if (n < 0) { rerror_errstr(t->tag, "write failed"); return; }
    f->wrote = true;
    memset(&r, 0, sizeof(r));
    r.type = Rwrite;
    r.tag = t->tag;
    r.count = n;
    reply(&r);
}

// Create a file on a mount ("/fram"): fid is the directory, and becomes
// the new file, open. An existing file is emptied, as Plan 9's create does.
static void s_create(Fcall *t)
{
    Sfid *f = findfid(t->fid);
    if (!f) { rerror(t->tag, "unknown fid"); return; }
    if (f->open) { rerror(t->tag, "already open"); return; }
    if (serving == SRV_RO) { rerror(t->tag, "read-only (serve -r)"); return; }
    if (!is_mount(f->path)) { rerror(t->tag, "files can only be made on a mount (/fram)"); return; }
    if (t->perm & DMDIR) { rerror(t->tag, "no directories on a mount yet"); return; }
    const char *nm = t->name;
    if (!nm[0] || !strcmp(nm, ".") || !strcmp(nm, "..") || strchr(nm, '/')) { rerror(t->tag, "not a valid name"); return; }
    char path[SRV_PATH];
    if (snprintf(path, sizeof(path), "%s/%s", f->path, nm) >= (int)sizeof(path)) { rerror(t->tag, "name too long"); return; }
    int fd = fs_open(path, FS_CREATE);
    if (fd < 0) { rerror_errstr(t->tag, "can't create"); return; }
    int n = fs_write(fd, "", 0);
    fs_close(fd);
    if (n < 0) { rerror_errstr(t->tag, "can't create"); return; }
    strcpy(f->path, path);
    f->isdir = false;
    f->open = true;
    f->mode = t->mode | OTRUNC;        // the first write replaces, later ones add
    f->wrote = false;
    f->vlen = 0;
    memset(&r, 0, sizeof(r));
    r.type = Rcreate;
    r.tag = t->tag;
    r.qid = qidof(path, false);
    r.iounit = SRV_IOUNIT;
    reply(&r);
}

static void s_clunk(Fcall *t)
{
    Sfid *f = findfid(t->fid);
    if (!f) { rerror(t->tag, "unknown fid"); return; }
    f->used = false;
    memset(&r, 0, sizeof(r));
    r.type = Rclunk;
    r.tag = t->tag;
    reply(&r);
}

void srv_request(uint8_t *msg, unsigned n)
{
    Fcall *t = &t_in;
    if (convM2S(msg, n, t) != n) return;   // not a valid message: no tag to answer
    if (serving == SRV_OFF) {
        char name[16], why[ERRMAX];
        sysname_get(name, sizeof(name));
        snprintf(why, sizeof(why), "%s doesn't serve (no serve line)", name);
        rerror(t->tag, why);
        return;
    }
    switch (t->type) {
    case Tversion: s_version(t); break;
    case Tauth:    rerror(t->tag, "no authentication required"); break;
    case Tattach:  s_attach(t); break;
    case Tflush: {
        memset(&r, 0, sizeof(r));
        r.type = Rflush;
        r.tag = t->tag;
        reply(&r);                       // every request is answered at once
        break;
    }
    case Twalk:    s_walk(t); break;
    case Topen:    s_open(t); break;
    case Tread:    s_read(t); break;
    case Twrite:   s_write(t); break;
    case Tclunk:   s_clunk(t); break;
    case Tcreate:  s_create(t); break;
    case Tremove:  rerror(t->tag, "removing a file over 9P: not yet"); break;
    case Tstat:    rerror(t->tag, "stat over 9P: not yet"); break;
    case Twstat:   rerror(t->tag, "wstat over 9P: not yet"); break;
    default:       rerror(t->tag, "not a request"); break;
    }
}
