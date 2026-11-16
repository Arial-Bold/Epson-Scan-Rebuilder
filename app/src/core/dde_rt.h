/*
 * Runtime for the statically recompiled Epson DDE plug-in (Text Enhancement Technology binarization).
 *
 * The original 32-bit x86 code is translated instruction-by-instruction into
 * C (dde_gen.c). It runs against a flat 32-bit "guest" address space
 * that holds the original data segments, a heap and a stack, exactly like
 * the 32-bit process it was written for.
 */
#ifndef DDE_RT_H
#define DDE_RT_H

#include <stdint.h>
#include <math.h>
#include <string.h>

#define M dde_M
#define G dde_G
#define CPU DDE_CPU
#define X DDE_X

extern uint8_t *M;   /* base of the 4 GB guest address space */

typedef struct { uint64_t lo, hi; } X;   /* one SSE register */

typedef struct {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp;
    X xmm[8];
    double st[8];      /* x87 stack (only used to return floats) */
    int top;
} CPU;

extern CPU G;

/* memory */
static inline uint32_t R8(uint32_t a)  { return M[a]; }
static inline uint32_t R16(uint32_t a) { uint16_t v; memcpy(&v, M + a, 2); return v; }
static inline uint32_t R32(uint32_t a) { uint32_t v; memcpy(&v, M + a, 4); return v; }
static inline uint64_t R64(uint32_t a) { uint64_t v; memcpy(&v, M + a, 8); return v; }
static inline X R128(uint32_t a) { X v; memcpy(&v, M + a, 16); return v; }
static inline void W8(uint32_t a, uint32_t v)  { M[a] = (uint8_t)v; }
static inline void W16(uint32_t a, uint32_t v) { uint16_t t = (uint16_t)v; memcpy(M + a, &t, 2); }
static inline void W32(uint32_t a, uint32_t v) { memcpy(M + a, &v, 4); }
static inline void W64(uint32_t a, uint64_t v) { memcpy(M + a, &v, 8); }
static inline void W128(uint32_t a, X v) { memcpy(M + a, &v, 16); }

/* float <-> bits */
static inline float  BF(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint32_t FB(float f)  { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline double BD(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static inline uint64_t DB(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }

/* SSE lane helpers */
static inline float XF(X x) { return BF((uint32_t)x.lo); }
static inline double XD(X x) { return BD(x.lo); }
static inline X XSETF(X x, float f) { x.lo = (x.lo & 0xFFFFFFFF00000000ull) | FB(f); return x; }
static inline X XSETD(X x, double d) { x.lo = DB(d); return x; }
static inline X XLOADF(float f) { X x = { FB(f), 0 }; return x; }
static inline X XLOADD(double d) { X x = { DB(d), 0 }; return x; }

/* SSE semantics that differ from plain C */
static inline float  SSE_MINSS(float a, float b) { return a < b ? a : b; }
static inline float  SSE_MAXSS(float a, float b) { return a > b ? a : b; }
static inline int32_t SSE_CVTT_F(float f) {
    if (!(f > -2147483904.0f && f < 2147483648.0f)) return INT32_MIN;
    return (int32_t)f;
}
static inline int32_t SSE_CVTT_D(double d) {
    if (!(d > -2147483649.0 && d < 2147483648.0)) return INT32_MIN;
    return (int32_t)d;
}

/* x87 stack */
static inline void FPUSH(double v) { G.top = (G.top - 1) & 7; G.st[G.top] = v; }
static inline double FST(int i) { return G.st[(G.top + i) & 7]; }
static inline void FSETST(int i, double v) { G.st[(G.top + i) & 7] = v; }
static inline double FPOP(void) { double v = G.st[G.top]; G.top = (G.top + 1) & 7; return v; }

/* x87 control word (rounding mode for fistp) */
extern uint16_t dde_fcw;
#define G_FCW dde_fcw
static inline double FROUND(double v)
{
    switch ((G_FCW >> 10) & 3) {
    case 0: return nearbyint(v);
    case 1: return floor(v);
    case 2: return ceil(v);
    default: return trunc(v);
    }
}
static inline uint32_t FIST16(double v) { v = FROUND(v); return (v >= -32768.0 && v <= 32767.0) ? (uint32_t)(uint16_t)(int16_t)v : 0x8000u; }
static inline uint32_t FIST32(double v) { v = FROUND(v); return (v >= -2147483648.0 && v <= 2147483647.0) ? (uint32_t)(int32_t)v : 0x80000000u; }
static inline uint64_t FIST64(double v) { v = FROUND(v); return (v >= -9223372036854775808.0 && v < 9223372036854775808.0) ? (uint64_t)(int64_t)v : 0x8000000000000000ull; }

static inline uint32_t PARITY(uint32_t v) { return !__builtin_parity(v & 0xFF); }

/* provided by if_rt.c */
void dde_dispatch(uint32_t target);           /* indirect call */
void dde_import(uint32_t addr);               /* call into the jump table (libc etc.) */
void dde_fatal(const char *what, uint32_t addr);

#endif
