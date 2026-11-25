/**
 * 9p/fcall.h — 9P2000 messages: the bytes on the wire <-> a struct
 *
 * 9P is Plan 9's file protocol (Bell Labs): every file operation is a
 * request (T-message) and a reply (R-message), each
 *
 *   size[4] type[1] tag[2] ...fields...
 *
 * little-endian, size counting the whole message, strings as len[2] +
 * bytes (no nul). The tag pairs a reply with its request; a fid is the
 * client's number for "this file over there"; a qid is the server's
 * unique id of a file. See plan2040_ideas/IDEAS.md, section 1, and
 * Plan 9's intro(5).
 *
 * The names and the shape are Plan 9's (fcall.h, convM2S, convS2M,
 * GBIT16/PBIT32), as is the trick that makes the strings of a received
 * message nul-terminated in place: each string is moved down one byte,
 * over its own length field. So convM2S changes the buffer it reads,
 * and the Fcall's strings and data point into it: handle the message
 * before the buffer is reused.
 *
 * Plain C++ subset (no heap, no templates): carries to C and the 1802.
 */
#pragma once

#include <stdint.h>

enum {
    Tversion = 100, Rversion,
    Tauth = 102,    Rauth,
    Tattach = 104,  Rattach,
    Terror = 106,   Rerror,      // Terror doesn't exist; Rerror answers anything
    Tflush = 108,   Rflush,
    Twalk = 110,    Rwalk,
    Topen = 112,    Ropen,
    Tcreate = 114,  Rcreate,
    Tread = 116,    Rread,
    Twrite = 118,   Rwrite,
    Tclunk = 120,   Rclunk,
    Tremove = 122,  Rremove,
    Tstat = 124,    Rstat,
    Twstat = 126,   Rwstat,
};

#define NOTAG    0xFFFF
#define NOFID    0xFFFFFFFFu
#define MAXWELEM 16      // names in one Twalk
#define IOHDRSZ  24      // a Twrite's or Rread's bytes besides the data
#define P9_MSIZE (1024 + IOHDRSZ) // our largest message: a 1 KB file in one write
#define P9_VERSION "9P2000"

// qid.type
#define QTDIR    0x80
#define QTAPPEND 0x40
#define QTFILE   0x00
// Topen/Tcreate mode
#define OREAD    0
#define OWRITE   1
#define ORDWR    2
#define OEXEC    3
#define OTRUNC   0x10
#define ORCLOSE  0x40
// Tcreate perm, stat mode
#define DMDIR    0x80000000u
#define DMAPPEND 0x40000000u

// Little-endian fields, byte by byte (any CPU, any alignment).
#define GBIT8(p)   ((uint8_t)(p)[0])
#define GBIT16(p)  ((uint16_t)((p)[0] | ((p)[1] << 8)))
#define GBIT32(p)  ((uint32_t)(p)[0] | ((uint32_t)(p)[1] << 8) | \
                    ((uint32_t)(p)[2] << 16) | ((uint32_t)(p)[3] << 24))
#define GBIT64(p)  ((uint64_t)GBIT32(p) | ((uint64_t)GBIT32((p) + 4) << 32))
#define PBIT8(p, v)  ((p)[0] = (uint8_t)(v))
#define PBIT16(p, v) ((p)[0] = (uint8_t)(v), (p)[1] = (uint8_t)((v) >> 8))
#define PBIT32(p, v) ((p)[0] = (uint8_t)(v), (p)[1] = (uint8_t)((v) >> 8), \
                      (p)[2] = (uint8_t)((v) >> 16), (p)[3] = (uint8_t)((v) >> 24))
#define PBIT64(p, v) (PBIT32(p, (uint32_t)(v)), PBIT32((p) + 4, (uint32_t)((uint64_t)(v) >> 32)))

#define QIDSZ 13   // type[1] vers[4] path[8]

typedef struct {
    uint64_t path;
    uint32_t vers;
    uint8_t  type;
} Qid;

// One message. Which fields count depends on type (see fcall.cpp).
typedef struct {
    uint8_t  type;
    uint16_t tag;
    uint32_t fid;

    uint32_t msize;           // Tversion, Rversion
    const char *version;      // Tversion, Rversion
    uint16_t oldtag;          // Tflush
    const char *ename;        // Rerror
    Qid      qid;             // Rattach, Ropen, Rcreate
    uint32_t iounit;          // Ropen, Rcreate
    Qid      aqid;            // Rauth
    uint32_t afid;            // Tauth, Tattach
    const char *uname;        // Tauth, Tattach
    const char *aname;        // Tauth, Tattach
    uint32_t perm;            // Tcreate
    const char *name;         // Tcreate
    uint8_t  mode;            // Topen, Tcreate
    uint32_t newfid;          // Twalk
    uint16_t nwname;          // Twalk
    const char *wname[MAXWELEM];
    uint16_t nwqid;           // Rwalk
    Qid      wqid[MAXWELEM];
    uint64_t offset;          // Tread, Twrite
    uint32_t count;           // Tread, Twrite, Rread, Rwrite
    const char *data;         // Twrite, Rread
    uint16_t nstat;           // Twstat, Rstat
    const uint8_t *stat;      // Twstat, Rstat
} Fcall;

// Bytes -> Fcall. ap holds one whole message of nap bytes (its size
// field must say nap). Returns nap, or 0 if it isn't a valid message
// (unknown type, a field past the end, bytes left over, nwname > 16).
// Changes ap (see above); f's strings and data point into ap.
unsigned convM2S(uint8_t *ap, unsigned nap, Fcall *f);

// Fcall -> bytes. Returns the message's size, or 0 if it doesn't fit in
// nap bytes or can't be sent (unknown type, nwname/nwqid > 16, a string
// over 65535 bytes). A null string is sent as "".
unsigned convS2M(const Fcall *f, uint8_t *ap, unsigned nap);

// The size convS2M would make: 0 for an invalid Fcall.
unsigned sizeS2M(const Fcall *f);

// One line describing a message, for logs and tests (Plan 9's fcallfmt):
// "Twalk tag 1 fid 0 newfid 1 nwname 2 'dev' 'rtc'".
int fcallfmt(char *buf, int len, const Fcall *f);
