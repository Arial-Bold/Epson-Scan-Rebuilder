/* Epson Scan user tone curves in a classic resource fork - see es_tonefile.h.
 * The Resource Manager isn't available to 64-bit code, so the fork is read and
 * written directly (Inside Macintosh: More Macintosh Toolbox, "Resource File Format"). */
#include "es_tonefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <sys/xattr.h>
#endif

/* the fork: macOS keeps it as the file's com.apple.ResourceFork; elsewhere (tests) the
 * file itself stands in for it */
static void fork_path(const char *file, char *out, size_t outlen)
{
#ifdef __APPLE__
    snprintf(out, outlen, "%s/..namedfork/rsrc", file);
#else
    snprintf(out, outlen, "%s", file);
#endif
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

typedef struct {
    uint32_t type;
    int16_t id;
    uint8_t attrs;
    uint8_t *name;     /* Pascal string or NULL */
    uint8_t *data;
    uint32_t len;
} Res;

static void free_res(Res *r, int n)
{
    for (int i = 0; i < n; i++) { free(r[i].name); free(r[i].data); }
    free(r);
}

static uint8_t *read_all(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *b = NULL;
    long n = 0, cap = 0;
    for (;;) {
        if (n == cap) {
            cap = cap ? cap * 2 : 65536;
            uint8_t *nb = realloc(b, (size_t)cap);
            if (!nb) { free(b); fclose(f); return NULL; }
            b = nb;
        }
        size_t got = fread(b + n, 1, (size_t)(cap - n), f);
        if (!got) break;
        n += (long)got;
    }
    fclose(f);
    *len = n;
    return b;
}

/* all resources of a fork; 0 resources for an empty fork, -1 if damaged */
static int parse_fork(const uint8_t *b, long len, Res **out)
{
    *out = NULL;
    if (len == 0) return 0;
    if (len < 16) return -1;
    uint32_t doff = be32(b), moff = be32(b + 4), dlen = be32(b + 8), mlen = be32(b + 12);
    if ((uint64_t)doff + dlen > (uint64_t)len || (uint64_t)moff + mlen > (uint64_t)len || mlen < 30) return -1;
    const uint8_t *map = b + moff;
    uint16_t tl = be16(map + 24), nl = be16(map + 26);
    if ((uint32_t)tl + 2 > mlen) return -1;
    const uint8_t *types = map + tl;
    int ntypes = (int16_t)be16(types) + 1, total = 0;
    if ((uint32_t)tl + 2 + 8u * (uint32_t)ntypes > mlen) return -1;
    for (int t = 0; t < ntypes; t++) total += be16(types + 2 + 8 * t + 4) + 1;
    Res *r = calloc((size_t)(total ? total : 1), sizeof *r);
    if (!r) return -1;
    int k = 0;
    for (int t = 0; t < ntypes; t++) {
        const uint8_t *te = types + 2 + 8 * t;
        uint32_t type = be32(te);
        int cnt = be16(te + 4) + 1;
        uint16_t ro = be16(te + 6);
        for (int i = 0; i < cnt; i++) {
            const uint8_t *ref = types + ro + 12 * i;
            if (ref + 12 > map + mlen) { free_res(r, k); return -1; }
            Res *x = &r[k];
            x->type = type;
            x->id = (int16_t)be16(ref);
            uint16_t no = be16(ref + 2);
            x->attrs = ref[4];
            uint32_t ro24 = (uint32_t)ref[5] << 16 | (uint32_t)ref[6] << 8 | ref[7];
            if (no != 0xFFFF && (uint32_t)nl + no < mlen) {
                const uint8_t *np = map + nl + no;
                x->name = malloc((size_t)np[0] + 1);
                if (x->name) memcpy(x->name, np, (size_t)np[0] + 1);
            }
            if ((uint64_t)ro24 + 4 > dlen) { free_res(r, k + 1); return -1; }
            uint32_t rl = be32(b + doff + ro24);
            if ((uint64_t)ro24 + 4 + rl > dlen) { free_res(r, k + 1); return -1; }
            x->len = rl;
            x->data = malloc(rl ? rl : 1);
            if (!x->data) { free_res(r, k + 1); return -1; }
            memcpy(x->data, b + doff + ro24 + 4, rl);
            k++;
        }
    }
    *out = r;
    return k;
}

/* write a complete fork: resources grouped by type in the order given */
static int write_fork(const char *path, const Res *r, int n)
{
    uint32_t types[256];
    int ntypes = 0;
    for (int i = 0; i < n; i++) {
        int found = 0;
        for (int t = 0; t < ntypes; t++) if (types[t] == r[i].type) found = 1;
        if (!found) { if (ntypes == 256) return -1; types[ntypes++] = r[i].type; }
    }
    uint32_t dlen = 0, nlen = 0;
    for (int i = 0; i < n; i++) { dlen += 4 + r[i].len; if (r[i].name) nlen += 1u + r[i].name[0]; }
    uint32_t tl = 28, reflist = tl + 2 + 8u * (uint32_t)ntypes;
    uint32_t nl = reflist + 12u * (uint32_t)n, mlen = nl + nlen;
    uint32_t doff = 256, moff = doff + dlen;
    if (nl > 0xFFFF || nlen > 0xFFFF || dlen > 0xFFFFFF) return -1;
    size_t total = (size_t)moff + mlen;
    uint8_t *b = calloc(total, 1);
    if (!b) return -1;
    put32(b, doff); put32(b + 4, moff); put32(b + 8, dlen); put32(b + 12, mlen);
    uint8_t *map = b + moff;
    memcpy(map, b, 16);
    put16(map + 24, (uint16_t)tl);
    put16(map + 26, (uint16_t)nl);
    uint8_t *tlist = map + tl;
    put16(tlist, (uint16_t)(ntypes - 1));
    uint32_t dpos = 0, npos = 0, refpos = reflist;
    for (int t = 0; t < ntypes; t++) {
        int cnt = 0;
        uint32_t first = refpos;
        for (int i = 0; i < n; i++) {
            if (r[i].type != types[t]) continue;
            uint8_t *ref = map + refpos;
            put16(ref, (uint16_t)r[i].id);
            if (r[i].name) {
                put16(ref + 2, (uint16_t)npos);
                memcpy(map + nl + npos, r[i].name, (size_t)r[i].name[0] + 1);
                npos += 1u + r[i].name[0];
            } else
                put16(ref + 2, 0xFFFF);
            ref[4] = r[i].attrs;
            ref[5] = (uint8_t)(dpos >> 16); ref[6] = (uint8_t)(dpos >> 8); ref[7] = (uint8_t)dpos;
            put32(b + doff + dpos, r[i].len);
            memcpy(b + doff + dpos + 4, r[i].data, r[i].len);
            dpos += 4 + r[i].len;
            refpos += 12;
            cnt++;
        }
        uint8_t *te = tlist + 2 + 8 * t;
        put32(te, types[t]);
        put16(te + 4, (uint16_t)(cnt - 1));
        put16(te + 6, (uint16_t)(first - tl));
    }
    int rc = -1;
#ifdef __APPLE__
    /* replaces the whole fork in one step */
    rc = setxattr(path, XATTR_RESOURCEFORK_NAME, b, total, 0, 0) == 0 ? 0 : -1;
#else
    FILE *f = fopen(path, "wb");
    if (f) {
        rc = fwrite(b, 1, total, f) == total ? 0 : -1;
        if (fclose(f)) rc = -1;
    }
#endif
    free(b);
    return rc;
}

#define TONU 0x544F4E55u   /* 'TONU' */

/* IMByteSwap_ToneCurveInfo: on disk the record is big-endian */
static void unpack(const uint8_t *d, EsToneCurve *e)
{
    memset(e, 0, sizeof *e);
    memcpy(e->name, d, sizeof e->name);
    e->name[sizeof e->name - 1] = 0;
    static const int ic[4] = { 0x24, 0xac, 0x130, 0x1b4 }, sel[4] = { 0xa8, 0x238, 0x23c, 0x240 };
    for (int c = 0; c < 4; c++) {
        int16_t cnt = (int16_t)be16(d + ic[c]);
        memcpy(e->curves[c], &cnt, 2);
        for (int k = 0; k < 32; k++) {
            int32_t v = (int32_t)be32(d + ic[c] + 4 + 4 * k);
            memcpy(e->curves[c] + 4 + 4 * k, &v, 4);
        }
        e->selected[c] = (int32_t)be32(d + sel[c]);
    }
}

static void pack(const EsToneCurve *e, uint8_t *d)
{
    memset(d, 0, TONU_SIZE);
    memcpy(d, e->name, sizeof e->name);
    d[sizeof e->name - 1] = 0;
    static const int ic[4] = { 0x24, 0xac, 0x130, 0x1b4 }, sel[4] = { 0xa8, 0x238, 0x23c, 0x240 };
    for (int c = 0; c < 4; c++) {
        int16_t cnt;
        memcpy(&cnt, e->curves[c], 2);
        put16(d + ic[c], (uint16_t)cnt);
        for (int k = 0; k < 32; k++) {
            int32_t v;
            memcpy(&v, e->curves[c] + 4 + 4 * k, 4);
            put32(d + ic[c] + 4 + 4 * k, (uint32_t)v);
        }
        put32(d + sel[c], (uint32_t)e->selected[c]);
    }
}

int tonu_load(const char *file, EsToneCurve **out)
{
    *out = NULL;
    char fork[4096];
    fork_path(file, fork, sizeof fork);
    long len = 0;
    uint8_t *b = read_all(fork, &len);
    if (!b) return 0;
    Res *r;
    int n = parse_fork(b, len, &r);
    free(b);
    if (n < 0) return -1;
    /* Get1IndResource order = the order in the map, which is ID order as Epson wrote them */
    int cnt = 0;
    for (int i = 0; i < n; i++) if (r[i].type == TONU && r[i].len >= TONU_SIZE) cnt++;
    EsToneCurve *e = calloc((size_t)(cnt ? cnt : 1), sizeof *e);
    if (!e) { free_res(r, n); return -1; }
    int k = 0;
    for (int i = 0; i < n; i++) if (r[i].type == TONU && r[i].len >= TONU_SIZE) unpack(r[i].data, &e[k++]);
    free_res(r, n);
    *out = e;
    return k;
}

int tonu_save(const char *file, const EsToneCurve *list, int n)
{
    char fork[4096];
    fork_path(file, fork, sizeof fork);
    long len = 0;
    uint8_t *b = read_all(fork, &len);
    Res *old = NULL;
    int nold = 0;
    if (b) {
        nold = parse_fork(b, len, &old);
        free(b);
        if (nold < 0) return -1;          /* never overwrite a fork we can't read */
    }
    Res *r = calloc((size_t)(nold + n + 1), sizeof *r);
    if (!r) { free_res(old, nold); return -1; }
    int k = 0;
    for (int i = 0; i < nold; i++) {      /* RemoveResource for every TONU, keep the rest */
        if (old[i].type == TONU) { free(old[i].name); free(old[i].data); continue; }
        r[k++] = old[i];
    }
    free(old);
    for (int i = 0; i < n; i++) {         /* AddResource, IDs 10001.. */
        Res *x = &r[k++];
        x->type = TONU;
        x->id = (int16_t)(10001 + i);
        x->attrs = 0;
        x->name = NULL;
        x->len = TONU_SIZE;
        x->data = malloc(TONU_SIZE);
        if (!x->data) { free_res(r, k - 1); return -1; }
        pack(&list[i], x->data);
    }
    int rc = write_fork(file, r, k);
    free_res(r, k);
    return rc;
}

int rsrc_get(const char *file, uint32_t type, int16_t id, uint8_t **data, uint32_t *len)
{
    *data = NULL; *len = 0;
    char fork[4096];
    fork_path(file, fork, sizeof fork);
    long flen = 0;
    uint8_t *b = read_all(fork, &flen);
    if (!b) return 0;
    Res *r;
    int n = parse_fork(b, flen, &r);
    free(b);
    if (n < 0) return -1;
    int found = 0;
    for (int i = 0; i < n; i++)
        if (r[i].type == type && r[i].id == id) {
            *data = r[i].data; *len = r[i].len;
            r[i].data = NULL;
            found = 1;
            break;
        }
    free_res(r, n);
    return found;
}

int rsrc_put(const char *file, uint32_t type, int16_t id, const char *name, const uint8_t *data, uint32_t len)
{
    char fork[4096];
    fork_path(file, fork, sizeof fork);
    long flen = 0;
    uint8_t *b = read_all(fork, &flen);
    Res *old = NULL;
    int nold = 0;
    if (b) {
        nold = parse_fork(b, flen, &old);
        free(b);
        if (nold < 0) return -1;
    }
    Res *r = calloc((size_t)nold + 1, sizeof *r);
    if (!r) { free_res(old, nold); return -1; }
    int k = 0;
    for (int i = 0; i < nold; i++) {
        if (old[i].type == type && old[i].id == id) { free(old[i].name); free(old[i].data); continue; }
        r[k++] = old[i];
    }
    free(old);
    if (!data) {                          /* RemoveResource only */
        int rc = write_fork(file, r, k);
        free_res(r, k);
        return rc;
    }
    Res *x = &r[k++];
    x->type = type; x->id = id; x->attrs = 0;
    size_t nl = name ? strlen(name) : 0;
    x->name = NULL;
    if (nl) {
        if (nl > 255) nl = 255;
        x->name = malloc(nl + 1);
        if (x->name) { x->name[0] = (uint8_t)nl; memcpy(x->name + 1, name, nl); }
    }
    x->len = len;
    x->data = malloc(len ? len : 1);
    if (!x->data) { free_res(r, k); return -1; }
    memcpy(x->data, data, len);
    int rc = write_fork(file, r, k);
    free_res(r, k);
    return rc;
}
