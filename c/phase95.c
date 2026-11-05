/* =========================================================================
   PHASE 95 — a vaporwave rhythm twin-stick shooter.
   Single-file C port of the PHASE HTML5/WebGL2 build for Windows 95/98/ME
   (and later), targeting Pentium II/III class machines.

   Everything is generated at runtime: vector graphics through a software
   rasterizer (additive neon lines, bloom, tone-mapping, scanlines) and a
   procedural, beat-locked score from a software synthesizer. No asset files.

   Windows build (no C runtime; imports KERNEL32, USER32, GDI32, WINMM only;
   x87 floating point, no MMX/SSE; needs an i686 CPU: Pentium Pro/II/III, Celeron, Athlon):
     i686-w64-mingw32-gcc -std=gnu99 -O2 -march=i686 -mtune=pentium3 -mfpmath=387
       -mno-sse -mno-mmx -ffreestanding -fno-tree-loop-distribute-patterns
       -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -mwindows
       -e _WinMainCRTStartup phase95.c -o PHASE95.EXE -lkernel32 -luser32 -lgdi32 -lwinmm -lgcc
   Test build: add -DPH_TEST_XP=1000000000 (and -o PHASE95T.EXE). It starts with
   1,000,000,000 XP, topped up at every launch, and keeps its own PHASE95T.SAV.
   64-bit build for modern Windows (x86-64, Windows XP x64 / Vista and later; same imports):
     x86_64-w64-mingw32-gcc -std=gnu99 -O2 -march=x86-64 -mtune=generic -ffreestanding
       -fno-tree-loop-distribute-patterns -fno-stack-protector -fno-asynchronous-unwind-tables
       -nostdlib -mwindows -e WinMainCRTStartup phase95.c -o PHASE95_X64.EXE
       -lkernel32 -luser32 -lgdi32 -lwinmm -lgcc
   Run: PHASE95.EXE [-w windowed] [-d debug keys] [-bench component benchmark] [-simd=0|1|2 cap].
   Alt+Enter / F11 toggles fullscreen. Settings and records are saved next to the EXE as PHASE95.SAV.
   Display (SETTINGS):
     RESOLUTION  BEST (default: the largest mode the card lists), DESKTOP, or 640x480 ... 3840x2160.
                 Fullscreen uses a borderless window when the choice matches the desktop, else
                 switches the display mode. Windowed, it sets the window size.
     FRAME RATE  30, 60 (default), 120, 240, 1000, or UNLIMITED.
     QUALITY     AUTO, or the scene at full output size, half height, half, or quarter
                 (the composite scales it up; menus and HUD are always drawn at full size).
                 On first start a short benchmark picks LOW, MEDIUM or HIGH.
   Output is RGB565 on 16-bit desktops and XRGB8888 above.
   Performance on newer machines (all chosen at run time; a Pentium under Windows 95 runs the
   same EXE on the plain x87/integer path):
     - CPUID + OS checks pick SSE2 or AVX2 pixel kernels (NT-family Windows 2000 and later only,
       AVX2 only when the OS saves the AVX state). Every kernel gives bit-identical pixels.
     - Up to 16 worker threads with a spin-then-sleep handoff; the background, world raster
       (finer bands, claimed dynamically), bloom, composite and HUD run in row bands.
     - Frames are blitted from their own thread out of two DIB sections on multi-core NT systems.
     - Lookup tables (tone curve, exp, row floors) and per-thread scratch rows trade RAM for time;
       non-temporal stores at 1920x1080 and above.
     - -bench writes PHASE95_BENCH.TXT: per-component milliseconds for each scene, kernel set and
       thread count. SETTINGS > SHOW FPS shows the live per-component split.
   Headless test build (Linux/macOS: renders frames to PPM and audio to WAV):
     cc -std=gnu99 -O2 phase95.c -o phase95 -lm -pthread
     ./phase95 --scene play --wave 5 --res 1920x1080 --fmt32 --quality 3 --rate 120 --threads 4 --shot 300 --out shots
     ./phase95 --compbench --res 1920x1080 --fmt32          (the component benchmark)
     ./phase95 ... --simd 0|1|2                              (cap the kernel set)
   ========================================================================= */
#if defined(_WIN32)
#define PH_WIN32 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#else
#define PH_HEADLESS 1
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#endif
#include <stdint.h>
#include <stddef.h>
#if defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#include <immintrin.h>   /* used only inside functions compiled for SSE2 / AVX2 */
#endif
#include <string.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t i32; typedef int64_t i64; typedef uint64_t u64;
#define PI 3.14159265358979f
#define TAU 6.28318530717958f
#define ARR(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define INL static inline

/* ---------------- math: x87 on the Windows builds (no C runtime), libm elsewhere; the 64-bit
   Windows build takes square roots and rounding from SSE and the transcendentals from x87 ---------------- */
#if defined(__i386__) || (defined(__x86_64__) && defined(PH_WIN32))
#if defined(__x86_64__)
INL float msqrt(float x) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x))); }
#else
INL float msqrt(float x) { float r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
#endif
INL float msin(float x) { float r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
INL float mcos(float x) { float r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
INL float mtan(float x) { float r; __asm__("fptan\n\tfstp %%st(0)" : "=t"(r) : "0"(x)); return r; }
INL float matan2(float y, float x) { float r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
INL float mlog2(float x) { float r; __asm__("fld1\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x)); return r; }
INL float mrint(float x) { float r; __asm__("frndint" : "=t"(r) : "0"(x)); return r; }
INL float mexp2(float t) {
  if (t < -126.f) return 0.f; if (t > 126.f) t = 126.f;
  float n = mrint(t), f = t - n, m, r;
  __asm__("f2xm1" : "=t"(m) : "0"(f)); m += 1.f;
  __asm__("fscale" : "=t"(r) : "0"(m), "u"(n));
  return r;
}
#if defined(__x86_64__)
INL int mlrint(float x) { return _mm_cvtss_si32(_mm_set_ss(x)); }   /* round to nearest even, as fistp */
#else
INL int mlrint(float x) { int i; __asm__("fistpl %0" : "=m"(i) : "t"(x) : "st"); return i; }
#endif
INL float mfmod(float a, float b) { float r; __asm__("1: fprem\n\tfnstsw %%ax\n\tsahf\n\tjp 1b" : "=t"(r) : "0"(a), "u"(b) : "ax", "cc"); return r; }
#else
INL float msqrt(float x) { return sqrtf(x); }
INL float msin(float x) { return sinf(x); }
INL float mcos(float x) { return cosf(x); }
INL float mtan(float x) { return tanf(x); }
INL float matan2(float y, float x) { return atan2f(y, x); }
INL float mlog2(float x) { return log2f(x); }
INL float mexp2(float t) { return exp2f(t); }
INL int mlrint(float x) { return (int)lrintf(x); }
INL float mfmod(float a, float b) { return fmodf(a, b); }
#endif
INL float mfabs(float x) { return x < 0 ? -x : x; }
INL int mfloori(float x) { int i = mlrint(x); return (float)i > x ? i - 1 : i; }
INL float mfloor(float x) { return (float)mfloori(x); }
INL float mexp(float x) { return mexp2(x * 1.44269504f); }
INL float mlog(float x) { return mlog2(x) * 0.69314718f; }
INL float mpow(float a, float b) { return a <= 0 ? 0 : mexp2(b * mlog2(a)); }
INL float mhypot(float x, float y) { return msqrt(x * x + y * y); }
INL float masin(float x) { if (x > 1) x = 1; if (x < -1) x = -1; return matan2(x, msqrt(1 - x * x)); }
INL float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
INL int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
INL float lerpf(float a, float b, float t) { return a + (b - a) * t; }
INL float minf(float a, float b) { return a < b ? a : b; }
INL float maxf(float a, float b) { return a > b ? a : b; }
INL float signf(float x) { return x > 0 ? 1.f : x < 0 ? -1.f : 0.f; }
INL float fractf(float x) { return x - mfloor(x); }
INL float damp(float a, float b, float lambda, float dt) { return lerpf(a, b, 1 - mexp(-lambda * dt)); }
INL float wrapAngle(float a) { a = mfmod(a + PI, TAU); if (a < 0) a += TAU; return a - PI; }
INL float lerpAngle(float a, float b, float t) { return a + wrapAngle(b - a) * t; }
INL float dist2(float ax, float ay, float bx, float by) { float dx = bx - ax, dy = by - ay; return dx * dx + dy * dy; }
INL float easeOutCubic(float t) { t = 1 - t; return 1 - t * t * t; }
INL float easeOutBack(float t) { const float c = 1.70158f; float u = t - 1; return 1 + (c + 1) * u * u * u + c * u * u; }
INL float smoothstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static float segDist2(float ax, float ay, float bx, float by, float px, float py) {
  float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t = l2 > 1e-9f ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  float cx = ax + dx * t - px, cy = ay + dy * t - py; return cx * cx + cy * cy;
}

/* ---------------- colour ---------------- */
typedef struct { float r, g, b; } C3;
INL C3 c3(float r, float g, float b) { C3 c = { r, g, b }; return c; }
INL C3 cmul(C3 c, float k) { return c3(c.r * k, c.g * k, c.b * k); }
INL C3 clerp(C3 a, C3 b, float t) { return c3(lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t)); }
static C3 hsl(float h, float s, float l) {
  float o[3]; int n[3] = { 0, 8, 4 };
  for (int i = 0; i < 3; i++) {
    float k = mfmod(n[i] + h * 12, 12), a = s * minf(l, 1 - l);
    o[i] = l - a * maxf(-1, minf(minf(k - 3, 9 - k), 1));
  }
  return c3(o[0], o[1], o[2]);
}

/* ---------------- random ---------------- */
static u32 rng_s = 0x9E3779B9u;
INL u32 rnd_u32(void) { u32 x = rng_s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_s = x; }
INL float frand(void) { return (rnd_u32() >> 8) * (1.0f / 16777216.0f); }
INL float rnd(float a) { return frand() * a; }
INL float rndr(float a, float b) { return a + frand() * (b - a); }
INL int rndi(int a, int b) { return a + (int)(frand() * (b - a + 1)); }
INL int chance(float p) { return frand() < p; }

/* ---------------- tiny libc (the Windows build links no C runtime) ---------------- */
#if defined(PH_WIN32)
/* string moves: dwords first, then the tail (DF is clear on entry under the Win32 ABI) */
#if defined(__x86_64__)   /* 64-bit build: byte string moves (fast on every 64-bit CPU's microcode) */
void *memset(void *d, int c, size_t n) { void *r = d; __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory"); return r; }
void *memcpy(void *d, const void *s, size_t n) { void *r = d; __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory"); return r; }
#else
void *memset(void *d, int c, size_t n) {
  int d0, d1; u32 v = (u8)c * 0x01010101u;
  __asm__ volatile("rep stosl\n\tmovl %4, %%ecx\n\trep stosb" : "=&c"(d0), "=&D"(d1) : "0"(n >> 2), "1"(d), "q"(n & 3), "a"(v) : "memory");
  return d;
}
void *memcpy(void *d, const void *s, size_t n) {
  int d0, d1, d2;
  __asm__ volatile("rep movsl\n\tmovl %5, %%ecx\n\trep movsb" : "=&c"(d0), "=&D"(d1), "=&S"(d2) : "0"(n >> 2), "1"(d), "q"(n & 3), "2"(s) : "memory");
  return d;
}
#endif
void *memmove(void *d, const void *s, size_t n) { u8 *p = d; const u8 *q = s; if (p < q) while (n--) *p++ = *q++; else { p += n; q += n; while (n--) *--p = *--q; } return d; }
int memcmp(const void *a, const void *b, size_t n) { const u8 *p = a, *q = b; for (; n; n--, p++, q++) if (*p != *q) return *p - *q; return 0; }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int _fltused = 1;
#endif
static void mzero(void *p, size_t n) { memset(p, 0, n); }

/* strings: a small builder, enough for scores and labels */
static int s_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int s_cmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }
static char *s_cpy(char *d, const char *s) { while ((*d = *s)) { d++; s++; } return d; }
static char *s_int(char *d, i64 v) {
  char t[24]; int n = 0; u64 u = v < 0 ? (u64)(-v) : (u64)v;
  if (v < 0) *d++ = '-';
  do { t[n++] = (char)('0' + (int)(u % 10)); u /= 10; } while (u);
  while (n) *d++ = t[--n];
  *d = 0; return d;
}
/** 1234567 -> "1,234,567" */
static char *s_fmt(char *d, i64 v) {
  char t[32]; int n = 0; u64 u = v < 0 ? (u64)(-v) : (u64)v;
  if (v < 0) *d++ = '-';
  do { if (n && n % 4 == 3) t[n++] = ','; t[n++] = (char)('0' + (int)(u % 10)); u /= 10; } while (u);
  while (n) *d++ = t[--n];
  *d = 0; return d;
}
static char *s_pad2(char *d, int v) { if (v < 10 && v >= 0) *d++ = '0'; return s_int(d, v); }
static const char *roman(int n) { static const char *R[] = { "", "I", "II", "III", "IV", "V", "VI" }; return n >= 0 && n < 7 ? R[n] : "+"; }
/* format: "%s" string, "%d" int, "%D" i64 with commas, "%2" zero-padded 2 digits, "%+" signed int */
static char *s_printf(char *d, const char *f, ...) {
  __builtin_va_list ap; __builtin_va_start(ap, f);
  char *o = d;
  while (*f) {
    if (*f != '%') { *o++ = *f++; continue; }
    f++;
    switch (*f++) {
      case 's': o = s_cpy(o, __builtin_va_arg(ap, const char *)); break;
      case 'd': o = s_int(o, __builtin_va_arg(ap, int)); break;
      case 'D': o = s_fmt(o, __builtin_va_arg(ap, i64)); break;
      case '2': o = s_pad2(o, __builtin_va_arg(ap, int)); break;
      case '+': { int v = __builtin_va_arg(ap, int); if (v > 0) *o++ = '+'; o = s_int(o, v); break; }
      case '%': *o++ = '%'; break;
    }
  }
  *o = 0; __builtin_va_end(ap); return d;
}

/* ---------------- column-major 4x4 matrices ---------------- */
static void m4_perspective(float *o, float fovy, float aspect, float n, float f) {
  float t = 1 / mtan(fovy / 2), nf = 1 / (n - f);
  mzero(o, 64); o[0] = t / aspect; o[5] = t; o[10] = (f + n) * nf; o[11] = -1; o[14] = 2 * f * n * nf;
}
static void m4_basis(float *o, const float *r, const float *u, const float *b, const float *e) {
  o[0] = r[0]; o[4] = r[1]; o[8] = r[2]; o[12] = -(r[0] * e[0] + r[1] * e[1] + r[2] * e[2]);
  o[1] = u[0]; o[5] = u[1]; o[9] = u[2]; o[13] = -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]);
  o[2] = b[0]; o[6] = b[1]; o[10] = b[2]; o[14] = -(b[0] * e[0] + b[1] * e[1] + b[2] * e[2]);
  o[3] = 0; o[7] = 0; o[11] = 0; o[15] = 1;
}
static void m4_mul(float *o, const float *a, const float *b) {
  float t[16];
  for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++)
    t[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
  memcpy(o, t, 64);
}
static void m4_invert(float *o, const float *m) {
  float a00 = m[0], a01 = m[1], a02 = m[2], a03 = m[3], a10 = m[4], a11 = m[5], a12 = m[6], a13 = m[7];
  float a20 = m[8], a21 = m[9], a22 = m[10], a23 = m[11], a30 = m[12], a31 = m[13], a32 = m[14], a33 = m[15];
  float b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10, b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11;
  float b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12, b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30;
  float b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31, b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
  float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
  if (det == 0) return; det = 1 / det;
  o[0] = (a11 * b11 - a12 * b10 + a13 * b09) * det; o[1] = (a02 * b10 - a01 * b11 - a03 * b09) * det;
  o[2] = (a31 * b05 - a32 * b04 + a33 * b03) * det; o[3] = (a22 * b04 - a21 * b05 - a23 * b03) * det;
  o[4] = (a12 * b08 - a10 * b11 - a13 * b07) * det; o[5] = (a00 * b11 - a02 * b08 + a03 * b07) * det;
  o[6] = (a32 * b02 - a30 * b05 - a33 * b01) * det; o[7] = (a20 * b05 - a22 * b02 + a23 * b01) * det;
  o[8] = (a10 * b10 - a11 * b08 + a13 * b06) * det; o[9] = (a01 * b08 - a00 * b10 - a03 * b06) * det;
  o[10] = (a30 * b04 - a31 * b02 + a33 * b00) * det; o[11] = (a21 * b02 - a20 * b04 - a23 * b00) * det;
  o[12] = (a11 * b07 - a10 * b09 - a12 * b06) * det; o[13] = (a00 * b09 - a01 * b07 + a02 * b06) * det;
  o[14] = (a31 * b01 - a30 * b03 - a32 * b00) * det; o[15] = (a20 * b03 - a21 * b01 + a22 * b00) * det;
}
INL void m4_apply(const float *m, float x, float y, float z, float *o) {
  o[0] = m[0] * x + m[4] * y + m[8] * z + m[12]; o[1] = m[1] * x + m[5] * y + m[9] * z + m[13];
  o[2] = m[2] * x + m[6] * y + m[10] * z + m[14]; o[3] = m[3] * x + m[7] * y + m[11] * z + m[15];
}

/* ---------------- world constants ---------------- */
#define ARENA_W 1800.f
#define ARENA_H 1200.f
#define LIFT 16.f
/* output (display) resolution: chosen at run time, up to 3840×2400; always a multiple of 4 */
#define OWMAX 3840
#define OHMAX 2400
static int OW = 640, OH = 480;

/* ---------------- platform services (implemented in the platform section) ---------------- */
static double plat_time(void);                 /* seconds, monotonic */
static int plat_save(const void *p, int n);
static int plat_load(void *p, int n);              /* bytes read, 0 if none */
static u32 plat_date(void);                    /* YYYYMMDD */
static void plat_video_apply(void);           /* re-apply RESOLUTION / FULLSCREEN */
static void plat_res_info(int res, int *w, int *h); /* the size a RESOLUTION entry stands for here (0 if unknown) */
static void *plat_alloc(size_t n);             /* zeroed */
static void plat_free(void *p);
static int nWork = 1;                          /* worker threads available for parallel jobs (1 on single-CPU machines) */
static void plat_par(int n, void (*fn)(int));  /* run fn(0..n-1), n <= nWork, and wait */
/* ---- worker pool core, shared by both platforms: workers wait on a task word, spinning briefly before
   they sleep, so back-to-back parallel stages hand over in well under a microsecond on multi-core CPUs
   instead of paying an OS wake-up each (that matters at thousands of frames per second). The platform
   supplies wk_sleep / wk_wake (an auto-reset event or a semaphore per thread, slot 0 = the main thread). ---- */
static void wk_sleep(int i); static void wk_wake(int i);
#define WKMAX 16
static volatile long wkTask, wkLeft, wkSlp[WKMAX]; static void (*volatile wkFn)(int); static int wkSpin = 4096;
INL void cpu_relax(void) {
#if defined(__i386__) || defined(__x86_64__)
  __asm__ __volatile__("rep; nop" ::: "memory");   /* PAUSE; a plain NOP on CPUs before the Pentium 4 */
#else
  __sync_synchronize();
#endif
}
/* return once *w == v (eq) or *w != v (!eq); spin first, then sleep on slot i */
static void wk_park(int i, volatile long *w, long v, int eq) {
  for (int k = wkSpin; k > 0; k--) { if ((*w == v) == eq) return; cpu_relax(); }
  for (;;) {   /* each post matches one flag cleared by a waker; a late post from a finished job is absorbed by re-checking */
    __sync_lock_test_and_set(&wkSlp[i], 1);
    if ((*w == v) == eq) { if (__sync_lock_test_and_set(&wkSlp[i], 0) == 0) wk_sleep(i); return; }   /* a waker took the flag: take its post */
    wk_sleep(i);
    if ((*w == v) == eq) return;
  }
}
static void worker_loop(int id) {
  long seen = 0;
  for (;;) {
    wk_park(id, &wkTask, seen, 0);
    long t = wkTask; seen = t;
    if (id < (int)(t & 31)) {
      wkFn(id);
      if (__sync_sub_and_fetch(&wkLeft, 1) == 0 && __sync_lock_test_and_set(&wkSlp[0], 0) == 1) wk_wake(0);
    }
  }
}
static void plat_par(int n, void (*fn)(int)) {
  if (n > nWork) n = nWork;
  if (n < 2) { fn(0); return; }
  wkFn = fn; wkLeft = n - 1;
  __sync_lock_test_and_set(&wkTask, (long)(((((unsigned long)wkTask >> 5) + 1) << 5) | (unsigned long)n));   /* new generation + width, published with a full barrier */
  for (int i = 1; i < n; i++) if (wkSlp[i] && __sync_lock_test_and_set(&wkSlp[i], 0) == 1) wk_wake(i);
  fn(0);
  wk_park(0, &wkLeft, 0, 1);
}
/* per-component timers: the -bench report and the SHOW FPS readout */
enum { C_SIM, C_BG, C_PREP, C_RASTER, C_BLOOM, C_COMP, C_HUD, C_PRESENT, C_FRAME, NCOMP };
static const char *const COMP_NAME[NCOMP] = { "SIM", "BG", "PREP", "RASTER", "BLOOM", "COMP", "HUD", "PRESENT", "FRAME" };
static double compT[NCOMP], compAudio; static int compOn;
static double plat_clock(void);               /* real seconds, for timers */
#define PROF(i) do { if (compOn) compT[i] -= plat_clock(); } while (0)
#define PROFE(i) do { if (compOn) compT[i] += plat_clock(); } while (0)
/* CPU features, and the instruction set the pixel kernels use */
static struct { char vendor[16], brand[52]; int mmx, sse, sse2, ssse3, sse41, avx, avx2, osSse, osAvx, nt, osMajor, osMinor, logical; } Cpu;
static int simdLevel, simdMax;
static int presentThreaded;   /* frames are blitted from their own thread (multi-core NT systems) */                /* 0 = x87/integer (Windows 95/98, old CPUs), 1 = SSE2, 2 = AVX2 */
#if defined(__x86_64__)
static const char *const SIMD_NAME[3] = { "X64", "SSE2", "AVX2" };   /* level 0 = plain x86-64 code (scalar SSE) */
#define PH_BUILD "64-BIT (X86-64)"
#else
static const char *const SIMD_NAME[3] = { "X87", "SSE2", "AVX2" };
#define PH_BUILD "32-BIT (I686, X87)"
#endif

/* ---------------- settings + save file ---------------- */
typedef struct {
  i32 master, music, sfx, offset, shake, bloom;
  u8 crt, vhs, flash, fps, fullscreen, rate, quality, res;
  /* rate: 0 = 60 fps, 1 = 30, 2 = 120, 3 = 240, 4 = 1000, 5 = unlimited.
     quality: 0 auto, 1 high … 4 lowest. res: index into RESOLUTIONS */
} Settings;
static const struct { u16 w, h; const char *name; } RESOLUTIONS[] = {
  { 0, 0, "BEST" }, { 0, 0, "DESKTOP" },
  { 640, 480, "640 X 480" }, { 800, 600, "800 X 600" }, { 1024, 768, "1024 X 768" }, { 1280, 720, "1280 X 720" }, { 1280, 1024, "1280 X 1024" },
  { 1366, 768, "1366 X 768" }, { 1600, 900, "1600 X 900" }, { 1600, 1200, "1600 X 1200" }, { 1920, 1080, "1920 X 1080" }, { 2560, 1440, "2560 X 1440" },
  { 3840, 2160, "3840 X 2160" } };
#define NRES 13
#define RES_BEST 0      /* the largest mode the display offers (up to 3840×2400), switched to exclusively */
#define RES_DESKTOP 1   /* the desktop as it is: a borderless window, no mode switch */
#define NRATE 6
static int rate_hz(int r) { static const int HZ[NRATE] = { 60, 30, 120, 240, 1000, 0 }; return HZ[r < NRATE ? r : 0]; }   /* 0 = unlimited */
/* defaults: exclusive fullscreen at the best resolution, 60 fps, audio offset -60 ms; quality is set by
   the one-time benchmark on first start */
static const Settings SETTINGS_DEFAULT = { 8, 7, 8, -60, 7, 7, 1, 1, 1, 0, 1, 0, 2, RES_BEST };
static Settings Set;

typedef struct { i64 score; i32 wave, start, kills, mult, time; u32 date; u8 won, rush, pad[2]; } Record;
#define SAVE_MAGIC 0x35394850u /* "PH95" */
#define SAVE_VER 2
typedef struct {
  u32 magic, ver;
  Settings set;
  Record rec[10]; i32 nrec;
  i64 bank;
  u8 meta[8];
  u32 levels;   /* bit per unlocked start sector (1, 5, 10, 15, 20, 25) + bit 31 for boss rush */
  u8 played, pad[3];
  /* version 2 */
  u8 benchDone, benchQ; u16 benchFps;   /* first-start benchmark: done, quality it chose, frames per second it measured */
  u32 reserved[7];
} SaveData;
typedef struct { u32 magic, ver; Settings set; Record rec[10]; i32 nrec; i64 bank; u8 meta[8]; u32 levels; u8 played, pad[3]; } SaveV1;
#if defined(PH_WIN32)
_Static_assert(sizeof(SaveV1) == 472, "version 1 saves are 472 bytes");
#endif
static SaveData Save;
static void save_write(void) { Save.magic = SAVE_MAGIC; Save.ver = SAVE_VER; Save.set = Set; plat_save(&Save, sizeof Save); }
static void save_read(void) {
  mzero(&Save, sizeof Save);
  int n = plat_load(&Save, sizeof Save);
  if (n == (int)sizeof(SaveV1) && Save.magic == SAVE_MAGIC && Save.ver == 1) {
    /* version 1: keep records, XP, bonuses, unlocks and the player's audio/visual choices; display
       options move to the new defaults (the resolution list changed) and the benchmark runs once */
    Settings o = Save.set; int r = o.rate;
    mzero(&Save.benchDone, sizeof Save - ((u8 *)&Save.benchDone - (u8 *)&Save));
    Save.set = SETTINGS_DEFAULT;
    Save.set.master = o.master; Save.set.music = o.music; Save.set.sfx = o.sfx; Save.set.shake = o.shake; Save.set.bloom = o.bloom;
    Save.set.crt = o.crt; Save.set.vhs = o.vhs; Save.set.flash = o.flash; Save.set.fps = o.fps; Save.set.rate = (u8)(r <= 2 ? r : 0);
    Save.set.offset = o.offset ? o.offset : SETTINGS_DEFAULT.offset;   /* the old default 0 becomes the new default */
    Save.ver = SAVE_VER;
  } else if (n != (int)sizeof Save || Save.magic != SAVE_MAGIC || Save.ver != SAVE_VER) { mzero(&Save, sizeof Save); Save.set = SETTINGS_DEFAULT; }
  Set = Save.set;
  Set.master = clampi(Set.master, 0, 10); Set.music = clampi(Set.music, 0, 10); Set.sfx = clampi(Set.sfx, 0, 10);
  Set.offset = clampi(Set.offset, -100, 250); Set.shake = clampi(Set.shake, 0, 10); Set.bloom = clampi(Set.bloom, 0, 10);
  if (Set.quality > 4) Set.quality = 0;
  if (Set.rate >= NRATE) Set.rate = 0;
  if (Set.res >= NRES) Set.res = RES_BEST;
}
/* =========================================================================
   AUDIO ENGINE — software synthesizer + mixer.
   The game thread builds voices (oscillators → shaper → filter → envelope →
   bus + sends) and queues them with absolute start times; the mixer renders
   them sample-accurately, so the score is tighter than the WebAudio original.
   Buses mirror the browser graph: music (+duck) → tape wow → muffle filter,
   effects, reverb, gated "80s" reverb, feedback delay, master compressor.
   ========================================================================= */
static int SR = 22050;
#define CTRL 16
#define MAXV 64
#define NOISE_N 32768
enum { O_SINE, O_SAW, O_SQUARE, O_TRI, O_NOISE };
enum { F_NONE, F_LP, F_HP, F_BP };
enum { B_MUSIC, B_DUCK, B_SFX };
enum { E_SET, E_LIN, E_EXP };
enum { SHP_NONE, SHP_SOFT, SHP_DIST };

typedef struct { float t[10], v[10]; u8 k[10]; u8 n; } Env;
INL void env_pt(Env *e, int k, float t, float v) { if (e->n < 10) { e->t[e->n] = t; e->v[e->n] = v; e->k[e->n] = (u8)k; e->n++; } }
INL void env_set(Env *e, float t, float v) { env_pt(e, E_SET, t, v); }
INL void env_lin(Env *e, float t, float v) { env_pt(e, E_LIN, t, v); }
INL void env_exp(Env *e, float t, float v) { env_pt(e, E_EXP, t, v < 1e-6f ? 1e-6f : v); }
/** WebAudio "perc": 0.0001 at t, exponential up to peak over a, exponential down over d. */
INL void env_perc(Env *e, float t, float a, float peak, float d) { env_set(e, t, 0.0001f); env_exp(e, t + a, maxf(peak, 0.0002f)); env_exp(e, t + a + d, 0.0001f); }
static float env_at(const Env *e, float tau, u8 *cur) {
  int n = e->n, i = *cur;
  if (!n) return 1;
  while (i < n && e->t[i] <= tau) i++;
  *cur = (u8)i;
  if (i == 0) return e->v[0];
  if (i >= n) return e->v[n - 1];
  float v0 = e->v[i - 1], v1 = e->v[i], t0 = e->t[i - 1], t1 = e->t[i], f = t1 > t0 ? (tau - t0) / (t1 - t0) : 1;
  if (e->k[i] == E_LIN) return v0 + (v1 - v0) * f;
  if (e->k[i] == E_EXP) { if (v0 <= 0) v0 = 1e-6f; return v0 * mexp2(f * mlog2(v1 / v0)); }
  return v0;
}

typedef struct { u8 type; float f, gain, det; u32 ph; float dr; } Osc;
typedef struct {
  double t0; float dur;
  u8 nosc, flt, ws, bus, fm, vib, live, prio;
  Osc o[10];
  Env amp, fe, cut, idx, vibd;
  float q, pan, gain, srev, sdel, sgate, fmr, vibr;
  u8 panned, ca, cf, cc, ci, cv;
  /* runtime */
  float z1, z2, b0, b1, b2, a1, a2, lc, lastAmp;
  u32 mph, vph, nix;
  i64 s0, s1;
  float pl, pr;
} Voice;

/* ---- tables ---- */
static float SINE[2048], NOISE[NOISE_N], SOFTC[1025], DISTC[1025];
static void audio_tables(void) {
  for (int i = 0; i < 2048; i++) SINE[i] = msin(i * TAU / 2048);
  for (int i = 0; i < NOISE_N; i++) NOISE[i] = frand() * 2 - 1;
  for (int i = 0; i <= 1024; i++) {
    float x = i / 512.f - 1;
    /* tanh via exp */
    float a = mexp(2 * x * 6), b = mexp(2 * x * 2.2f), c = mexp(2 * 2.2f);
    DISTC[i] = (a - 1) / (a + 1);
    SOFTC[i] = ((b - 1) / (b + 1)) / ((c - 1) / (c + 1));
  }
}
INL float shaper(const float *T, float x) {
  float p = (clampf(x, -1, 1) + 1) * 512; int i = (int)p; if (i >= 1024) i = 1023; float f = p - i;
  return T[i] + (T[i + 1] - T[i]) * f;
}

/* ---- bus parameters with first-order approach (WebAudio setTargetAtTime) ---- */
typedef struct { float v, target, tc; } Param;
INL void param_step(Param *p, float dt) { if (p->tc <= 0) p->v = p->target; else p->v += (p->target - p->v) * (1 - mexp(-dt / p->tc)); }

enum { P_MASTER, P_MUSIC, P_SFX, P_CUT, P_HUM, P_DUCK, P_CUTCANCEL, P_N };
typedef struct { double t; u8 id; float v, tc; } PCmd;

/* ---- biquad ---- */
typedef struct { float b0, b1, b2, a1, a2; } BQ;
static void bq_design(BQ *c, int type, float f, float q) {
  float nyq = SR * 0.5f; f = clampf(f, 10, nyq * 0.92f);
  float w = TAU * f / SR, cs = mcos(w), sn = msin(w), alpha, a0;
  if (type == F_BP) alpha = sn / (2 * maxf(q, 0.05f));
  else alpha = sn / (2 * mpow(10, q / 20));
  a0 = 1 + alpha;
  if (type == F_LP) { c->b0 = (1 - cs) / 2 / a0; c->b1 = (1 - cs) / a0; c->b2 = c->b0; }
  else if (type == F_HP) { c->b0 = (1 + cs) / 2 / a0; c->b1 = -(1 + cs) / a0; c->b2 = c->b0; }
  else { c->b0 = alpha / a0; c->b1 = 0; c->b2 = -alpha / a0; }
  c->a1 = -2 * cs / a0; c->a2 = (1 - alpha) / a0;
}

/* ---- mixer state ---- */


typedef struct { float *buf; int len, pos; float fb, damp, z; } Comb;
typedef struct { float *buf; int len, pos; } APass;
static struct {
  Voice v[MAXV];
  PCmd pend[96]; int npend;
  Param master, music, sfx, cut, hum;
  float duckT; int duckOn;          /* time since last duck trigger */
  i64 pos;                          /* samples rendered */
  /* effects */
  Comb comb[8]; APass ap[4];
  Comb gcomb[4];
  float *dl; int dlen, dpos; float dlz;
  float *wl, *wr; int wlen, wpos; float wph1, wph2;
  float mzl1, mzl2, mzr1, mzr2; BQ mbq; float mlc;
  float compG;
  /* hum drone */
  u32 hph[4]; float hz1, hz2; BQ hbq;
  /* levels for the visuals */
  float lpB, hpX, hpY, envB, envH;
  float levBass, levHigh;
  int sfxScaleSet;
} Mx;
static float lev_bass, lev_high;  /* read by the game thread */

/* ---- command queue (game thread → mixer) ---- */
#define VQ 256
#define PQ 128
static Voice vq[VQ]; static volatile int vq_h, vq_t;
static PCmd pq[PQ]; static volatile int pq_h, pq_t;
#if defined(PH_WIN32)
static CRITICAL_SECTION snd_cs;
#define SND_LOCK() EnterCriticalSection(&snd_cs)
#define SND_UNLOCK() LeaveCriticalSection(&snd_cs)
#else
#define SND_LOCK() ((void)0)
#define SND_UNLOCK() ((void)0)
#endif
static void mx_push_voice(const Voice *v) {
  SND_LOCK();
  int n = (vq_h + 1) % VQ;
  if (n != vq_t) { vq[vq_h] = *v; vq_h = n; }
  SND_UNLOCK();
}
static void mx_push_param(double t, int id, float v, float tc) {
  SND_LOCK();
  int n = (pq_h + 1) % PQ;
  if (n != pq_t) { pq[pq_h].t = t; pq[pq_h].id = (u8)id; pq[pq_h].v = v; pq[pq_h].tc = tc; pq_h = n; }
  SND_UNLOCK();
}

static float *mx_alloc(int n);
static void mx_init(int sr) {
  SR = sr;
  mzero(&Mx, sizeof Mx);
  Mx.master.v = Mx.master.target = 0.5f; Mx.music.v = Mx.music.target = 0.6f; Mx.sfx.v = Mx.sfx.target = 0.8f;
  Mx.cut.v = Mx.cut.target = 20000; Mx.hum.v = Mx.hum.target = 0;
  Mx.duckT = 10; Mx.compG = 1; Mx.mlc = -1;
  static const int CL[8] = { 1116, 1188, 1277, 1356, 1139, 1211, 1300, 1379 }, AL[4] = { 556, 441, 579, 464 }, GL[4] = { 311, 397, 463, 557 };
  float k = sr / 44100.f;
  for (int i = 0; i < 8; i++) { Mx.comb[i].len = (int)(CL[i] * k * 1.35f); Mx.comb[i].buf = mx_alloc(Mx.comb[i].len); Mx.comb[i].fb = 0.86f; Mx.comb[i].damp = 0.3f; }
  for (int i = 0; i < 4; i++) { Mx.ap[i].len = (int)(AL[i] * k); Mx.ap[i].buf = mx_alloc(Mx.ap[i].len); }
  for (int i = 0; i < 4; i++) { Mx.gcomb[i].len = (int)(GL[i] * k * 2.2f); Mx.gcomb[i].buf = mx_alloc(Mx.gcomb[i].len); Mx.gcomb[i].fb = 0.72f; Mx.gcomb[i].damp = 0.15f; }
  Mx.dlen = (int)(0.35f * sr); Mx.dl = mx_alloc(Mx.dlen);
  Mx.wlen = (int)(0.05f * sr) + 4; Mx.wl = mx_alloc(Mx.wlen); Mx.wr = mx_alloc(Mx.wlen);
  bq_design(&Mx.hbq, F_LP, 260, 4);
}

/* allocate a voice slot: a free one, else steal the quietest that has started */
static Voice *mx_slot(void) {
  int best = -1; float bl = 1e9f;
  for (int i = 0; i < MAXV; i++) if (!Mx.v[i].live) return &Mx.v[i];
  for (int i = 0; i < MAXV; i++) { Voice *v = &Mx.v[i]; if (v->s0 > Mx.pos) continue; float l = v->lastAmp * (v->prio ? 4 : 1); if (l < bl) { bl = l; best = i; } }
  return best >= 0 ? &Mx.v[best] : NULL;
}
static void mx_start(const Voice *src) {
  Voice *v = mx_slot(); if (!v) return;
  *v = *src;
  v->live = 1;
  v->s0 = (i64)(v->t0 * SR + 0.5); if (v->s0 < Mx.pos) v->s0 = Mx.pos;
  v->s1 = v->s0 + (i64)(v->dur * SR) + 1;
  v->z1 = v->z2 = 0; v->lc = -1; v->lastAmp = 1;
  v->ca = v->cf = v->cc = v->ci = v->cv = 0;
  v->nix = rnd_u32() & (NOISE_N - 1);
  for (int i = 0; i < v->nosc; i++) { v->o[i].ph = rnd_u32(); v->o[i].dr = mexp2(v->o[i].det / 1200); }
  v->mph = 0; v->vph = 0;
  if (v->panned) { float a = (clampf(v->pan, -1, 1) + 1) * 0.25f * PI; v->pl = mcos(a); v->pr = msin(a); } else v->pl = v->pr = 1;
}

/* polyBLEP */
INL float blep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1; }
  if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
  return 0;
}

/* render one control block of one voice into bus buffers */
static float bufM[2][CTRL], bufD[2][CTRL], bufS[2][CTRL], sRevM[CTRL], sRevS[CTRL], sDelM[CTRL], sDelS[CTRL], sGate[CTRL];
static void voice_block(Voice *v, i64 b0) {
  int k0 = 0;
  if (v->s0 >= b0 + CTRL) return;
  if (v->s0 > b0) k0 = (int)(v->s0 - b0);
  float tau = (float)(b0 + k0 - v->s0) / SR, tauE = (float)(b0 + CTRL - v->s0) / SR;
  float a0 = env_at(&v->amp, tau, &v->ca) * v->gain, a1 = env_at(&v->amp, tauE, &v->ca) * v->gain;
  v->lastAmp = mfabs(a1);
  float fe = env_at(&v->fe, tau, &v->cf), vibr = 1;
  if (v->vib) {
    float depth = env_at(&v->vibd, tau, &v->cv);
    v->vph += (u32)(v->vibr * CTRL / SR * 4294967296.0);
    vibr = mexp2(SINE[v->vph >> 21] * depth / 1200);
  }
  float tmp[CTRL]; const float k32 = 4294967296.0f / SR;
  for (int k = k0; k < CTRL; k++) tmp[k] = 0;
  for (int i = 0; i < v->nosc; i++) {
    Osc *o = &v->o[i];
    float f = o->f * fe * o->dr * vibr, g = o->gain;
    if (o->type == O_NOISE) { u32 ix = v->nix; for (int k = k0; k < CTRL; k++) { tmp[k] += NOISE[ix & (NOISE_N - 1)] * g; ix++; } v->nix = ix; continue; }
    if (f > SR * 0.48f) continue;
    if (i == 0 && v->fm) {
      float idx = env_at(&v->idx, tau, &v->ci), mf = f * v->fmr; u32 minc = (u32)(i64)(mf * k32);
      u32 ph = o->ph, mph = v->mph;
      for (int k = k0; k < CTRL; k++) {
        float m = SINE[mph >> 21]; mph += minc;
        float fi = f + m * idx; if (fi < 0) fi = -fi; if (fi > SR * 0.49f) fi = SR * 0.49f;
        ph += (u32)mlrint(fi * k32);
        float x;
        if (o->type == O_SINE) x = SINE[ph >> 21];
        else x = (ph & 0x80000000u) ? -1.f : 1.f;
        tmp[k] += x * g;
      }
      o->ph = ph; v->mph = mph; continue;
    }
    u32 inc = (u32)mlrint(f * k32), ph = o->ph;
    float dt = f / SR;
    switch (o->type) {
      case O_SINE: for (int k = k0; k < CTRL; k++) { tmp[k] += SINE[ph >> 21] * g; ph += inc; } break;
      case O_SAW: for (int k = k0; k < CTRL; k++) { float t = ph * 2.3283064e-10f; tmp[k] += (2 * t - 1 - blep(t, dt)) * g; ph += inc; } break;
      case O_SQUARE: for (int k = k0; k < CTRL; k++) { float t = ph * 2.3283064e-10f, t2 = (u32)(ph + 0x80000000u) * 2.3283064e-10f; tmp[k] += ((t < 0.5f ? 1.f : -1.f) + blep(t, dt) - blep(t2, dt)) * g; ph += inc; } break;
      case O_TRI: for (int k = k0; k < CTRL; k++) { float t = ph * 2.3283064e-10f; tmp[k] += (4 * mfabs(t - 0.5f) - 1) * g; ph += inc; } break;
    }
    o->ph = ph;
  }
  if (v->ws == SHP_SOFT) for (int k = k0; k < CTRL; k++) tmp[k] = shaper(SOFTC, tmp[k]);
  else if (v->ws == SHP_DIST) for (int k = k0; k < CTRL; k++) tmp[k] = shaper(DISTC, tmp[k]);
  if (v->flt) {
    float c = env_at(&v->cut, tau, &v->cc);
    if (mfabs(c - v->lc) > v->lc * 0.004f) { BQ q; bq_design(&q, v->flt, c, v->q); v->b0 = q.b0; v->b1 = q.b1; v->b2 = q.b2; v->a1 = q.a1; v->a2 = q.a2; v->lc = c; }
    float z1 = v->z1, z2 = v->z2;
    for (int k = k0; k < CTRL; k++) { float x = tmp[k], y = v->b0 * x + z1; z1 = v->b1 * x - v->a1 * y + z2; z2 = v->b2 * x - v->a2 * y; tmp[k] = y; }
    if (mfabs(z1) < 1e-15f) z1 = 0; if (mfabs(z2) < 1e-15f) z2 = 0;
    v->z1 = z1; v->z2 = z2;
  }
  float da = (a1 - a0) / (CTRL - k0), a = a0;
  float (*bus)[CTRL] = v->bus == B_MUSIC ? bufM : v->bus == B_DUCK ? bufD : bufS;
  int sfx = v->bus == B_SFX;
  float *rv = sfx ? sRevS : sRevM, *dv = sfx ? sDelS : sDelM;
  for (int k = k0; k < CTRL; k++) {
    float y = tmp[k] * a; a += da;
    bus[0][k] += y * v->pl; bus[1][k] += y * v->pr;
    if (v->srev) rv[k] += y * v->srev;
    if (v->sdel) dv[k] += y * v->sdel;
    if (v->sgate) sGate[k] += y * v->sgate;
  }
}

static float mx_comb(Comb *c, float x) {
  float y = c->buf[c->pos];
  c->z = y * (1 - c->damp) + c->z * c->damp;
  c->buf[c->pos] = x + c->z * c->fb + 1e-18f;
  if (++c->pos >= c->len) c->pos = 0;
  return y;
}
static float mx_ap(APass *a, float x) {
  float b = a->buf[a->pos], y = -x + b;
  a->buf[a->pos] = x + b * 0.5f;
  if (++a->pos >= a->len) a->pos = 0;
  return y;
}

/** Render n stereo frames into out (interleaved int16). */
static void mx_render(short *out, int n) {
  /* drain the queues */
  SND_LOCK();
  while (vq_t != vq_h) { mx_start(&vq[vq_t]); vq_t = (vq_t + 1) % VQ; }
  while (pq_t != pq_h) {
    PCmd c = pq[pq_t]; pq_t = (pq_t + 1) % PQ;
    if (c.id == P_CUTCANCEL) { int j = 0; for (int i = 0; i < Mx.npend; i++) if (Mx.pend[i].id != P_CUT) Mx.pend[j++] = Mx.pend[i]; Mx.npend = j; continue; }
    if (Mx.npend < ARR(Mx.pend)) Mx.pend[Mx.npend++] = c;
  }
  SND_UNLOCK();
  const float cdt = (float)CTRL / SR;
  for (int done = 0; done < n; done += CTRL) {
    i64 b0 = Mx.pos;
    double tb = (double)b0 / SR;
    /* due parameter changes */
    for (int i = 0; i < Mx.npend;) {
      PCmd *c = &Mx.pend[i];
      if (c->t > tb + cdt) { i++; continue; }
      Param *p = c->id == P_MASTER ? &Mx.master : c->id == P_MUSIC ? &Mx.music : c->id == P_SFX ? &Mx.sfx : c->id == P_CUT ? &Mx.cut : c->id == P_HUM ? &Mx.hum : NULL;
      if (p) { p->target = c->v; p->tc = c->tc; }
      else if (c->id == P_DUCK) { Mx.duckT = (float)(tb - c->t); Mx.duckOn = 1; }
      Mx.pend[i] = Mx.pend[--Mx.npend];
    }
    param_step(&Mx.master, cdt); param_step(&Mx.music, cdt); param_step(&Mx.sfx, cdt); param_step(&Mx.cut, cdt); param_step(&Mx.hum, cdt);
    /* duck: 1 → 0.3 over 12 ms → 1 by 280 ms */
    float duck = 1; if (Mx.duckOn) { float t = Mx.duckT; duck = t < 0 ? 1 : t < 0.012f ? 1 - 0.7f * t / 0.012f : t < 0.28f ? 0.3f + 0.7f * (t - 0.012f) / 0.268f : 1; Mx.duckT += cdt; if (t > 0.3f) Mx.duckOn = 0; }
    mzero(bufM, sizeof bufM); mzero(bufD, sizeof bufD); mzero(bufS, sizeof bufS);
    mzero(sRevM, sizeof sRevM); mzero(sRevS, sizeof sRevS); mzero(sDelM, sizeof sDelM); mzero(sDelS, sizeof sDelS); mzero(sGate, sizeof sGate);
    for (int i = 0; i < MAXV; i++) {
      Voice *v = &Mx.v[i]; if (!v->live) continue;
      if (v->s0 < b0 + CTRL) voice_block(v, b0);
      if (v->s1 <= b0 + CTRL) v->live = 0;
    }
    /* gravity-well drone: two detuned saws and a sine through a wobbling low-pass */
    float hum = Mx.hum.v;
    if (hum > 0.0005f) {
      static const float HF[3] = { 41.2f, 61.7f, 41.6f };
      float lfo = msin(Mx.hph[3] * 2.3283064e-10f * TAU) * 120;
      Mx.hph[3] += (u32)(0.31f * cdt * 4294967296.0f);
      BQ q; bq_design(&q, F_LP, 260 + lfo, 4);
      for (int k = 0; k < CTRL; k++) {
        float x = 0;
        for (int j = 0; j < 3; j++) { float t = Mx.hph[j] * 2.3283064e-10f; x += j == 1 ? SINE[Mx.hph[j] >> 21] : 2 * t - 1; Mx.hph[j] += (u32)(HF[j] / SR * 4294967296.0f); }
        float y = q.b0 * x + Mx.hz1; Mx.hz1 = q.b1 * x - q.a1 * y + Mx.hz2; Mx.hz2 = q.b2 * x - q.a2 * y;
        y *= hum; bufS[0][k] += y; bufS[1][k] += y; sRevS[k] += y;
      }
    }
    /* music filter (muffle) coefficients */
    if (mfabs(Mx.cut.v - Mx.mlc) > Mx.mlc * 0.003f) { bq_design(&Mx.mbq, F_LP, Mx.cut.v, 0.8f); Mx.mlc = Mx.cut.v; }
    float mv = Mx.music.v, sv = Mx.sfx.v, gm = Mx.master.v;
    float peak = 0, outL[CTRL], outR[CTRL];
    for (int k = 0; k < CTRL; k++) {
      /* music bus → tape wow → muffle */
      float ml = bufM[0][k] + bufD[0][k] * duck, mr = bufM[1][k] + bufD[1][k] * duck;
      Mx.wl[Mx.wpos] = ml; Mx.wr[Mx.wpos] = mr;
      float dsec = 0.012f + 0.0019f * SINE[mlrint(Mx.wph1 * 2048) & 2047] + 0.00022f * SINE[mlrint(Mx.wph2 * 2048) & 2047];
      Mx.wph1 += 0.43f / SR; if (Mx.wph1 >= 1) Mx.wph1 -= 1; Mx.wph2 += 5.7f / SR; if (Mx.wph2 >= 1) Mx.wph2 -= 1;
      float dp = Mx.wpos - dsec * SR; if (dp < 0) dp += Mx.wlen; int di = (int)dp; float df = dp - di; int dj = di + 1 >= Mx.wlen ? 0 : di + 1;
      ml = Mx.wl[di] + (Mx.wl[dj] - Mx.wl[di]) * df; mr = Mx.wr[di] + (Mx.wr[dj] - Mx.wr[di]) * df;
      if (++Mx.wpos >= Mx.wlen) Mx.wpos = 0;
      BQ *q = &Mx.mbq;
      float yl = q->b0 * ml + Mx.mzl1; Mx.mzl1 = q->b1 * ml - q->a1 * yl + Mx.mzl2; Mx.mzl2 = q->b2 * ml - q->a2 * yl;
      float yr = q->b0 * mr + Mx.mzr1; Mx.mzr1 = q->b1 * mr - q->a1 * yr + Mx.mzr2; Mx.mzr2 = q->b2 * mr - q->a2 * yr;
      /* levels for the visuals (pre-volume) */
      float mono = (yl + yr) * 0.5f;
      Mx.lpB += (mono - Mx.lpB) * (TAU * 150.f / SR);
      float hx = mono - Mx.hpX; Mx.hpX += hx * (TAU * 1700.f / SR); Mx.hpY += (hx - Mx.hpY) * (TAU * 8000.f / SR);
      float ab = mfabs(Mx.lpB), ah = mfabs(Mx.hpY);
      Mx.envB = ab > Mx.envB ? ab : Mx.envB * 0.9995f; Mx.envH = ah > Mx.envH ? ah : Mx.envH * 0.999f;
      /* delay: 350 ms, low-passed feedback */
      float din = sDelM[k] * mv + sDelS[k] * sv;
      float dy = Mx.dl[Mx.dpos]; Mx.dlz += (dy - Mx.dlz) * (TAU * 2600.f / SR > 1 ? 1 : TAU * 2600.f / SR);
      Mx.dl[Mx.dpos] = din + Mx.dlz * 0.36f + 1e-18f; if (++Mx.dpos >= Mx.dlen) Mx.dpos = 0;
      float dout = Mx.dlz * 0.5f;
      /* reverb */
      float rin = (sRevM[k] * mv + sRevS[k] * sv + dout) * 0.055f, rl = 0, rr = 0;
      for (int c = 0; c < 4; c++) rl += mx_comb(&Mx.comb[c], rin);
      for (int c = 4; c < 8; c++) rr += mx_comb(&Mx.comb[c], rin);
      rl = mx_ap(&Mx.ap[1], mx_ap(&Mx.ap[0], rl)); rr = mx_ap(&Mx.ap[3], mx_ap(&Mx.ap[2], rr));
      /* gated room for the snare */
      float gin = sGate[k] * mv * 0.11f, gl = 0, gr = 0;
      gl = mx_comb(&Mx.gcomb[0], gin) + mx_comb(&Mx.gcomb[1], gin); gr = mx_comb(&Mx.gcomb[2], gin) + mx_comb(&Mx.gcomb[3], gin);
      float L = yl * mv + bufS[0][k] * sv + rl * 0.45f + gl * 0.5f + dout;
      float R = yr * mv + bufS[1][k] * sv + rr * 0.45f + gr * 0.5f + dout;
      L *= gm; R *= gm;
      outL[k] = L; outR[k] = R;
      float p = mfabs(L) > mfabs(R) ? mfabs(L) : mfabs(R); if (p > peak) peak = p;
    }
    /* compressor: threshold -16 dB, knee 12, ratio 5, attack 2 ms, release 180 ms, with make-up */
    float lvl = peak > 1e-6f ? 20 * mlog2(peak) * 0.30103f : -120, over = lvl + 16, red = 0;
    if (over > 6) red = (over) * (1 - 1.f / 5); else if (over > -6) { float x = over + 6; red = (1 - 1.f / 5) * x * x / 24; }
    float tg = mexp2(-red / 20 * 3.3219281f);
    float co = tg < Mx.compG ? 1 - mexp(-cdt / 0.002f) : 1 - mexp(-cdt / 0.18f);
    Mx.compG += (tg - Mx.compG) * co;
    float g = Mx.compG * 1.35f;
    short *o = out + done * 2;
    for (int k = 0; k < CTRL; k++) {
      float L = outL[k] * g, R = outR[k] * g;
      L = L > 1 ? 1 : L < -1 ? -1 : L; R = R > 1 ? 1 : R < -1 ? -1 : R;
      o[2 * k] = (short)mlrint(L * 32000); o[2 * k + 1] = (short)mlrint(R * 32000);
    }
    Mx.pos += CTRL;
  }
  lev_bass = Mx.envB; lev_high = Mx.envH;
}
/* =========================================================================
   SOUND — the master clock, the procedural score and the spatial SFX.
   Time is a tempo map (BPM can change mid-song; the Eclipse warps it per
   beat) and a meter map (12/8 guardians). The sequencer walks 24 ticks per
   beat and gameplay reads the same map, so everything stays on the grid.
   ========================================================================= */
#define TPB 24
#define LOOK 0.2
enum { ST_TITLE, ST_GAME, ST_BOSS, ST_CALM, ST_OVER, ST_SILENT };
enum { MD_AEOLIAN, MD_DORIAN, MD_PHRYGIAN, MD_HARMONIC, MD_LYDIAN, MD_IONIAN };
static const int MODES[6][7] = { { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 5, 7, 9, 11 } };
static const int PROGS[7][4] = { { 0, 5, 2, 6 }, { 0, 6, 5, 6 }, { 0, 3, 5, 4 }, { 5, 6, 0, 0 }, { 0, 2, 6, 5 }, { 0, 5, 3, 4 }, { 3, 4, 0, 0 } };
static const int ARPS[5][8] = { { 0, 1, 2, 3, 4, 3, 2, 1 }, { 0, 2, 1, 3, 2, 4, 3, 5 }, { 0, 1, 2, 4, 0, 1, 3, 5 }, { 4, 2, 0, 2, 5, 3, 1, 3 }, { 0, 0, 2, 1, 4, 2, 5, 3 } };

typedef struct { int deg, raw[5], voiced[4], ninth, bass, first; } MChord;
typedef struct { int s, m; float d; } PNote;
typedef struct {
  u32 has; /* bit flags of which fields are set */
  int root, mode, prog[4], chordBars, arp, style, level, steps; float bpm;
} Score;
enum { SC_ROOT = 1, SC_MODE = 2, SC_PROG = 4, SC_CB = 8, SC_ARP = 16, SC_STYLE = 32, SC_LEVEL = 64, SC_BPM = 128, SC_STEPS = 256, SC_RESET = 512, SC_SNAP = 1024, SC_CRASH = 2048 };
typedef struct { int arc, orbit, missile, tempo, accent, spread, nova; } Layers;

static struct {
  int ok;
  struct { double t, b, bpm; } segs[16]; int nseg;
  struct { int tick, steps; } met[8]; int nmet;
  float lisX, lisW;
  int metro, warpOn; float warpTarget;
  Layers L;
  /* score state */
  int root, mode, prog[4], chordBars, progStart, style, level, arp; float bpm; int steps;
  Score pend; float detune;
  PNote phrase[8]; int nphrase;
  MChord cur; int hasCur;
  struct { int bar; MChord c; } clog[16]; int clogN;
  i64 nextTick;
  float sfxScale; float muffle; float humLvl;
  float load; double loadT;
  double gate[24];
  struct { int slot, n; } slots[32];
  struct { float bass, mid, high, avg, kick; } lv;
} S;

/* clock shared with the audio thread */
static volatile double aud_rt, aud_wall, aud_lat; static volatile float aud_blk;
static double snd_now(void) {
  double rt, wall; float blk;
  SND_LOCK(); rt = aud_rt; wall = aud_wall; blk = aud_blk; SND_UNLOCK();
#if defined(PH_HEADLESS)
  (void)wall; (void)blk; return rt;
#else
  double e = plat_time() - wall; if (e < 0) e = 0; if (e > blk * 2.5) e = blk * 2.5;
  return rt + e;
#endif
}
static int snd_isWarp(void) { return S.warpOn; }
static float snd_outLat(void) { float l = (float)aud_lat; return l < 0 ? 0 : l > 0.35f ? 0.35f : l; }

INL float mtof(float m) { return 440 * mexp2((m - 69) / 12); }
static double beatAt(double t) { int i = S.nseg - 1; while (i > 0 && S.segs[i].t > t) i--; return S.segs[i].b + (t - S.segs[i].t) * S.segs[i].bpm / 60; }
static double timeAt(double b) { int i = S.nseg - 1; while (i > 0 && S.segs[i].b > b) i--; return S.segs[i].t + (b - S.segs[i].b) * 60 / S.segs[i].bpm; }
static double bpmAtBeat(double b) { int i = S.nseg - 1; while (i > 0 && S.segs[i].b > b) i--; return S.segs[i].bpm; }
static float snd_bpm(void) { double t = snd_now(); int i = S.nseg - 1; while (i > 0 && S.segs[i].t > t) i--; return (float)S.segs[i].bpm; }
static float spb(void) { return 60.f / snd_bpm(); }
static int stepsAt(i64 tick) { int i = S.nmet - 1; while (i > 0 && S.met[i].tick > tick) i--; return S.met[i].steps; }
static double gameBeat(void) { return beatAt(snd_now() + 0.03); }
static int snd_steps(void) { return stepsAt((i64)mfloor((float)(gameBeat() * TPB))); }
static double visBeat(void) { return beatAt(snd_now() - snd_outLat() - Set.offset / 1000.0); }
static double visBeatAt(double age) { return beatAt(snd_now() - age - snd_outLat() - Set.offset / 1000.0); }
static double rawVisBeat(void) { return beatAt(snd_now() - snd_outLat()); }
static double tickTime(i64 tick) { return timeAt((double)tick / TPB); }
static double nextGrid(int sub, double minT, i64 *slot) { double b = (double)(i64)(beatAt(snd_now() + minT) * sub + 0.999999) / sub; if (slot) *slot = (i64)(b * sub + 0.5); return timeAt(b); }
static void setTempo(double b, double bpm) {
  double t = timeAt(b);
  while (S.nseg > 1 && S.segs[S.nseg - 1].b >= b) S.nseg--;
  if (S.nseg >= 16) { memmove(&S.segs[1], &S.segs[2], sizeof(S.segs[0]) * 14); S.nseg = 15; }
  S.segs[S.nseg].t = t; S.segs[S.nseg].b = b; S.segs[S.nseg].bpm = bpm; S.nseg++;
  double now = snd_now();
  while (S.nseg > 2 && S.segs[1].t < now - 4) { memmove(&S.segs[0], &S.segs[1], sizeof(S.segs[0]) * (S.nseg - 1)); S.nseg--; }
}

/* ---- voice building ---- */
static Voice VB;
static Voice *vnew(double t, float dur, int bus) { mzero(&VB, sizeof VB); VB.t0 = t; VB.dur = dur; VB.gain = 1; VB.bus = (u8)bus; return &VB; }
static void vosc(Voice *v, int type, float f, float det, float g) { if (v->nosc < 10) { Osc *o = &v->o[v->nosc++]; o->type = (u8)type; o->f = f; o->det = det; o->gain = g; } }
static void vflt(Voice *v, int type, float f, float q) { v->flt = (u8)type; v->q = q; env_set(&v->cut, 0, f); }
static void vpan(Voice *v, float pan) { if (pan != 0) { v->panned = 1; v->pan = clampf(pan, -1, 1); } }
static void vgo(Voice *v) { if (S.ok) mx_push_voice(v); }
static float tape(void) { return S.detune; }

/* ---------------- instruments (set A) ---------------- */
static void kick(double t, float v) {
  Voice *x = vnew(t, 0.55f, B_MUSIC); vosc(x, O_SINE, 150, 0, 1); env_set(&x->fe, 0, 1); env_exp(&x->fe, 0.11f, 42.f / 150); env_perc(&x->amp, 0, 0.003f, v * 0.95f, 0.42f); x->prio = 1; vgo(x);
  x = vnew(t, 0.05f, B_MUSIC); vosc(x, O_NOISE, 0, 0, 1); vflt(x, F_BP, 3500, 0.8f); env_perc(&x->amp, 0, 0.001f, v * 0.16f, 0.018f); vgo(x);
  if (S.ok) mx_push_param(t, P_DUCK, 0, 0);
}
static void snare(double t, float v) {
  Voice *x = vnew(t, 0.24f, B_MUSIC); vosc(x, O_NOISE, 0, 0, 1); vflt(x, F_BP, 1750, 0.6f); env_perc(&x->amp, 0, 0.001f, v * 0.42f, 0.17f); x->sgate = 0.85f; x->prio = 1; vgo(x);
  x = vnew(t, 0.12f, B_MUSIC); vosc(x, O_TRI, 215, 0, 1); env_set(&x->fe, 0, 1); env_exp(&x->fe, 0.07f, 165.f / 215); env_perc(&x->amp, 0, 0.001f, v * 0.3f, 0.09f); vgo(x);
}
static void clap(double t, float v) {
  Voice *x = vnew(t, 0.32f, B_MUSIC); vosc(x, O_NOISE, 0, 0, 1); vflt(x, F_BP, 1300, 0.9f);
  env_set(&x->amp, 0, 0.0001f);
  for (int k = 0; k < 3; k++) { env_set(&x->amp, k * 0.011f, v * 0.5f); env_exp(&x->amp, k * 0.011f + 0.0095f, v * 0.07f); }
  env_set(&x->amp, 0.034f, v * 0.45f); env_exp(&x->amp, 0.24f, 0.0001f); x->srev = 0.35f; vgo(x);
}
static void hat(double t, float v, int open, float pan) {
  Voice *x = vnew(t, open ? 0.32f : 0.08f, B_MUSIC); vosc(x, O_NOISE, 0, 0, 1); vflt(x, F_HP, open ? 7000 : 8600, 0.7f);
  env_perc(&x->amp, 0, 0.001f, v * 0.2f, open ? 0.22f : 0.032f); vpan(x, pan); vgo(x);
}
static void crash(double t, float v) {
  Voice *x = vnew(t, 2.22f, B_MUSIC); vosc(x, O_NOISE, 0, 0, 1); vflt(x, F_HP, 3200, 0.5f); env_perc(&x->amp, 0, 0.002f, v * 0.2f, 1.9f); x->srev = 0.5f; vgo(x);
}
/** FM pluck bass: a ratio-1 sine pair whose bite decays with the note, a square sub an octave down, soft-clipped. */
static void bass(double t, int midi, float dur, float v, float bright) {
  float f = mtof((float)midi), d = tape();
  Voice *x = vnew(t, dur + 0.02f, B_MUSIC); vosc(x, O_SINE, f, d, 1); x->fm = 1; x->fmr = 1;
  env_set(&x->idx, 0, f * (1.2f + 3.2f * bright)); env_exp(&x->idx, maxf(0.05f, dur * 0.55f), f * 0.18f);
  x->ws = SHP_SOFT; vflt(x, F_LP, 900 + 2600 * bright, 0.9f);
  env_set(&x->amp, 0, 0.0001f); env_exp(&x->amp, 0.005f, v * 0.34f); env_set(&x->amp, dur * 0.75f, v * 0.3f); env_exp(&x->amp, dur, 0.0001f);
  x->prio = 1; vgo(x);
  Env amp = x->amp;
  x = vnew(t, dur + 0.02f, B_MUSIC); vosc(x, O_SQUARE, f / 2, d, 0.3f); vflt(x, F_LP, 180, 0.8f); x->amp = amp; vgo(x);
}
static void pad(double t, const int *notes, int nn, float dur, float v) {
  Voice *x = vnew(t, dur + 0.4f, B_DUCK); float d0 = tape();
  for (int i = 0; i < nn && x->nosc < 10; i++) { vosc(x, O_SAW, mtof((float)notes[i]), -12 + d0, 1); vosc(x, O_SAW, mtof((float)notes[i]), 12 + d0, 1); }
  x->flt = F_LP; x->q = 0.7f; env_set(&x->cut, 0, 420); env_lin(&x->cut, dur * 0.45f, 1600); env_lin(&x->cut, dur, 620);
  env_set(&x->amp, 0, 0); env_lin(&x->amp, 0.5f, v); env_set(&x->amp, maxf(0.55f, dur - 0.5f), v); env_lin(&x->amp, dur + 0.35f, 0);
  x->srev = 0.6f; vgo(x);
}
/** FM electric piano: ratio-1 body with a high "tine" partial. */
static void rhodes(double t, const int *notes, int nn, float v, float dur) {
  for (int i = 0; i < nn; i++) {
    float f = mtof((float)notes[i]);
    Voice *x = vnew(t, dur + 0.1f, B_DUCK); vosc(x, O_SINE, f, tape(), 1); x->fm = 1; x->fmr = 1;
    env_set(&x->idx, 0, f * 1.6f); env_exp(&x->idx, dur * 0.5f, f * 0.08f);
    env_perc(&x->amp, 0, 0.003f, v * 0.12f, dur); x->srev = 0.45f; x->sdel = 0.18f; vgo(x);
    x = vnew(t, 0.1f, B_DUCK); vosc(x, O_SINE, minf(f * 14, 16000), 0, 1); env_perc(&x->amp, 0, 0.002f, v * v * 0.006f, 0.05f); x->srev = 0.45f; x->sdel = 0.18f; vgo(x);
  }
}
static void pluck(double t, int midi, float v, float dl, float pan, int tri) {
  float f = mtof((float)midi), d = tape();
  Voice *x = vnew(t, 0.26f, B_DUCK); vosc(x, tri ? O_TRI : O_SQUARE, f, d, 1); vosc(x, O_TRI, f * 2, d, 0.5f);
  x->flt = F_LP; x->q = 3; env_set(&x->cut, 0, 5400); env_exp(&x->cut, 0.16f, 420);
  env_perc(&x->amp, 0, 0.002f, v * 0.12f, 0.2f); vpan(x, pan); x->sdel = dl; vgo(x);
}
static void lead(double t, int midi, float dur, float v) {
  float f = mtof((float)midi), d = tape();
  Voice *x = vnew(t, dur + 0.15f, B_MUSIC); vosc(x, O_SAW, f, d, 1); vosc(x, O_SQUARE, f, d - 7, 0.5f);
  x->vib = 1; x->vibr = 5.6f; env_set(&x->vibd, 0, 0); env_lin(&x->vibd, 0.2f, 9);
  x->flt = F_LP; x->q = 2.5f; env_set(&x->cut, 0, 900); env_exp(&x->cut, 0.04f, 2800); env_exp(&x->cut, dur, 1300);
  env_set(&x->amp, 0, 0.0001f); env_exp(&x->amp, 0.012f, v * 0.09f); env_set(&x->amp, dur, v * 0.09f); env_exp(&x->amp, dur + 0.12f, 0.0001f);
  x->sdel = 0.4f; x->srev = 0.3f; vgo(x);
}
static void bell(double t, int midi, float v, float ratio, float idx, float dec, float pan) {
  float f = mtof((float)midi);
  Voice *x = vnew(t, dec + 0.05f, B_SFX); vosc(x, O_SINE, f, 0, 1); x->fm = 1; x->fmr = ratio;
  env_set(&x->idx, 0, f * idx); env_exp(&x->idx, dec * 0.6f, f * 0.04f);
  env_perc(&x->amp, 0, 0.002f, v, dec); vpan(x, pan); x->sdel = 0.3f; x->srev = 0.28f; vgo(x);
}
static void riser(double t, float dur, float v) {
  Voice *x = vnew(t, dur + 0.07f, B_MUSIC); vosc(x, O_NOISE, 0, 0, 1);
  x->flt = F_BP; x->q = 2.5f; env_set(&x->cut, 0, 250); env_exp(&x->cut, dur, 7500);
  env_set(&x->amp, 0, 0.0001f); env_exp(&x->amp, dur, v * 0.3f); env_lin(&x->amp, dur + 0.04f, 0); x->srev = 0.5f; vgo(x);
}
/** STATIC layer: inharmonic FM "metal" tick, high-passed. */
static void metal(double t, float v, float pan) {
  float f = 520 + frand() * 60;
  Voice *x = vnew(t, 0.1f, B_MUSIC); vosc(x, O_SQUARE, f, 0, 1); x->fm = 1; x->fmr = 3.73f;
  env_set(&x->idx, 0, f * 7); env_exp(&x->idx, 0.05f, f * 0.4f);
  vflt(x, F_HP, 5200, 1.4f); env_perc(&x->amp, 0, 0.0008f, v * 0.11f, 0.065f); vpan(x, pan); x->sdel = 0.25f; vgo(x);
}
/** BASS DROP layer: brass stab + sub drop. */
static void brass(double t, const int *notes, int nn, float v) {
  float d0 = tape();
  Voice *x = vnew(t, 0.6f, B_MUSIC);
  for (int i = 0; i < nn; i++) { vosc(x, O_SAW, mtof((float)notes[i]), -9 + d0, 1); vosc(x, O_SAW, mtof((float)notes[i]), 9 + d0, 1); }
  x->flt = F_LP; x->q = 2.2f; env_set(&x->cut, 0, 260); env_exp(&x->cut, 0.03f, 3400); env_exp(&x->cut, 0.38f, 650);
  env_perc(&x->amp, 0, 0.012f, v * 0.13f, 0.46f); x->srev = 0.4f; vgo(x);
  x = vnew(t, 0.5f, B_MUSIC); vosc(x, O_SINE, 110, 0, 1); env_set(&x->fe, 0, 1); env_exp(&x->fe, 0.42f, 34.f / 110); env_perc(&x->amp, 0, 0.004f, v * 0.55f, 0.45f); vgo(x);
}
/** STRIKE layer: formant "vox chop". */
static void vox(double t, int midi, float v) {
  static const float FQ[3][3] = { { 760, 9, 1 }, { 1180, 10, 0.6f }, { 2500, 12, 0.25f } };
  for (int i = 0; i < 3; i++) {
    Voice *x = vnew(t, 0.2f, B_DUCK); vosc(x, O_SAW, mtof((float)midi), tape(), 1); env_set(&x->fe, 0, 1); env_exp(&x->fe, 0.14f, 0.97f);
    vflt(x, F_BP, FQ[i][0], FQ[i][1]); env_perc(&x->amp, 0, 0.006f, v * 0.22f * FQ[i][2], 0.14f); x->sdel = 0.5f; vgo(x);
  }
}
static void metroClick(double t, int down) {
  Voice *x = vnew(t, 0.06f, B_SFX); vosc(x, O_SINE, down ? 2200 : 1500, 0, 1); env_perc(&x->amp, 0, 0.001f, 0.25f, 0.04f); vgo(x);
}

/* ---------------- composition ---------------- */
static int scaleNote(int i) { int m = ((i % 7) + 7) % 7, o = i >= 0 ? i / 7 : -((-i + 6) / 7); return S.root + MODES[S.mode][m] + 12 * o; }
static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static MChord chordAt(int bar) {
  MChord c; int idx = floordiv(bar - S.progStart, S.chordBars);
  c.deg = S.prog[((idx % 4) + 4) % 4];
  for (int k = 0; k < 5; k++) c.raw[k] = scaleNote(c.deg + 2 * k);
  for (int k = 0; k < 4; k++) { int x = c.raw[k]; while (x < 53) x += 12; while (x > 64) x -= 12; c.voiced[k] = x; }
  for (int i = 1; i < 4; i++) for (int j = i; j > 0 && c.voiced[j] < c.voiced[j - 1]; j--) { int tt = c.voiced[j]; c.voiced[j] = c.voiced[j - 1]; c.voiced[j - 1] = tt; }
  int b = c.raw[0] - 24; while (b > 45) b -= 12; while (b < 33) b += 12; c.bass = b;
  c.ninth = c.raw[4] - 12 * floordiv(c.raw[4] - 60, 12) + 12;
  c.first = ((bar - S.progStart) % S.chordBars + S.chordBars) % S.chordBars == 0;
  return c;
}
static MChord snd_chord(void) { return S.hasCur ? S.cur : chordAt(0); }
static MChord chordAtTime(double t) {
  int bar = (int)mfloor((float)(beatAt(t) / 4));
  for (int i = 0; i < S.clogN; i++) if (S.clog[i].bar == bar) return S.clog[i].c;
  return snd_chord();
}
static void setScore(const Score *o) {
  Score *p = &S.pend;
  if (o->has & SC_ROOT) p->root = o->root; if (o->has & SC_MODE) p->mode = o->mode;
  if (o->has & SC_PROG) memcpy(p->prog, o->prog, sizeof p->prog);
  if (o->has & SC_CB) p->chordBars = o->chordBars; if (o->has & SC_ARP) p->arp = o->arp;
  if (o->has & SC_STYLE) p->style = o->style; if (o->has & SC_LEVEL) p->level = o->level;
  if (o->has & SC_BPM) p->bpm = o->bpm; if (o->has & SC_STEPS) p->steps = o->steps;
  p->has |= o->has;
}
static void score_style(int style) { Score s; mzero(&s, sizeof s); s.has = SC_STYLE; s.style = style; setScore(&s); }
static void score_level(int lvl) { Score s; mzero(&s, sizeof s); s.has = SC_LEVEL; s.level = lvl; setScore(&s); }
enum { K_GAME, K_PRISM, K_CAROUSEL, K_ECLIPSE, K_OMEGA, K_PRIME };
static void snd_newKey(int kind) {
  Score s; mzero(&s, sizeof s);
  s.has = SC_ROOT | SC_MODE | SC_PROG | SC_ARP | SC_BPM | SC_STEPS | SC_CB | SC_RESET; s.chordBars = 2; s.bpm = 128; s.steps = 4;
  static const int roots[7] = { 57, 50, 52, 55, 53, 59, 54 }, modes[4] = { MD_AEOLIAN, MD_AEOLIAN, MD_DORIAN, MD_HARMONIC };
  const int *pg = NULL; static const int P_PRISM[4] = { 0, 3, 6, 4 }, P_CAR[4] = { 0, 5, 1, 4 }, P_OM[4] = { 0, 1, 5, 6 }, P_ECL[4] = { 0, 1, 5, 1 };
  switch (kind) {
    case K_GAME: s.root = roots[rndi(0, 6)]; s.mode = modes[rndi(0, 3)]; pg = PROGS[rndi(0, 6)]; s.arp = rndi(0, 4); break;
    case K_PRISM: s.root = 50; s.mode = MD_DORIAN; pg = P_PRISM; s.arp = 1; break;
    case K_CAROUSEL: s.root = 49; s.mode = MD_HARMONIC; pg = P_CAR; s.arp = 3; s.bpm = 120; s.steps = 3; break;
    case K_OMEGA: s.root = 47; s.mode = MD_PHRYGIAN; pg = P_OM; s.arp = 2; break;
    case K_PRIME: s.root = 45; s.mode = MD_HARMONIC; pg = P_CAR; s.arp = 4; s.bpm = 132; break;
    default: s.root = 46; s.mode = MD_PHRYGIAN; pg = P_ECL; s.arp = 2; break;
  }
  memcpy(s.prog, pg, sizeof s.prog);
  setScore(&s);
}
static void makePhrase(const MChord *c, int n) {
  static const int R12[3][7] = { { 0, 2, 3, 5, 6, 9, -1 }, { 0, 3, 5, 6, 8, 11, -1 }, { 0, 1, 3, 6, 9, 10, -1 } };
  static const int R16[5][8] = { { 0, 3, 6, 8, 10, 14, -1 }, { 0, 2, 4, 7, 10, 12, -1 }, { 0, 6, 8, 11, 14, -1 }, { 2, 4, 6, 10, 12, 13, 14, -1 }, { 0, 3, 4, 8, 11, -1 } };
  const int *r = n == 12 ? R12[rndi(0, 2)] : R16[rndi(0, 4)];
  int len = 0; while (r[len] >= 0) len++;
  float stepDur = spb() / (n / 4.f);
  static const int J[9] = { -2, -1, -1, 1, 1, 2, 0, 3, -3 }; static const int O3[3] = { 0, 2, 4 };
  int deg = c->deg + 7 + O3[rndi(0, 2)];
  S.nphrase = 0;
  for (int i = 0; i < len; i++) {
    deg = clampi(deg + J[rndi(0, 8)], c->deg + 4, c->deg + 13);
    int l = (i < len - 1 ? r[i + 1] : n) - r[i];
    S.phrase[S.nphrase].s = r[i]; S.phrase[S.nphrase].m = scaleNote(deg) + 12; S.phrase[S.nphrase].d = (l < 4 ? l : 4) * stepDur * 0.9f; S.nphrase++;
  }
}
static void barLine(int bar, i64 tick, double t, int on) {
  if (S.pend.has) {
    Score p = S.pend; S.pend.has = 0;
    if (p.has & SC_ROOT) S.root = p.root; if (p.has & SC_MODE) S.mode = p.mode; if (p.has & SC_PROG) memcpy(S.prog, p.prog, sizeof S.prog);
    if (p.has & SC_CB) S.chordBars = p.chordBars; if (p.has & SC_ARP) S.arp = p.arp; if (p.has & SC_STYLE) S.style = p.style;
    if (p.has & SC_LEVEL) S.level = p.level; if (p.has & SC_BPM) S.bpm = p.bpm;
    if (p.has & SC_RESET) S.progStart = bar;
    if ((p.has & SC_STEPS) && p.steps != S.steps) {
      S.steps = p.steps;
      if (S.nmet >= 6) { memmove(&S.met[0], &S.met[1], sizeof(S.met[0]) * (S.nmet - 1)); S.nmet--; }
      S.met[S.nmet].tick = (int)tick; S.met[S.nmet].steps = p.steps; S.nmet++;
    }
    if (p.has & SC_SNAP) setTempo((double)tick / TPB, S.bpm);
    if ((p.has & SC_CRASH) && on) crash(t, 0.9f);
  }
  S.cur = chordAt(bar); S.hasCur = 1;
  int slot = -1; for (int i = 0; i < S.clogN; i++) if (S.clog[i].bar == bar) slot = i;
  if (slot < 0) { if (S.clogN < 16) slot = S.clogN++; else { memmove(&S.clog[0], &S.clog[1], sizeof(S.clog[0]) * 15); slot = 15; } }
  S.clog[slot].bar = bar; S.clog[slot].c = S.cur;
  int n = S.steps * 4;
  if ((S.level >= 3 || S.style == ST_BOSS) && bar % 2 == 0) makePhrase(&S.cur, n);
  else if (bar % 2 == 0) S.nphrase = 0;
  if (on && S.L.nova && (S.style == ST_GAME || S.style == ST_BOSS) && bar % (S.L.nova >= 2 ? 2 : 4) == (S.L.nova >= 2 ? 1 : 3)) riser(t, spb() * 4 - 0.02f, 0.5f);
}
static void beatLine(i64 beat, double t) {
  (void)t;
  double cb = bpmAtBeat((double)beat), target = S.warpOn ? S.warpTarget : S.bpm;
  if (target - cb > 0.25 || cb - target > 0.25) { double d = target - cb; if (d > 7) d = 7; if (d < -7) d = -7; setTempo((double)beat, cb + d); }
  double nb = bpmAtBeat((double)beat);
  S.detune = clampf(1200 * mlog2((float)(nb / S.bpm)) * 0.6f, -450, 350);
}
static void schedStep(i64 tick, double t) {
  int n = S.steps * 4, q = S.steps, sb = (int)((tick % 96) / (TPB / q)), sub = sb % q, bt = sb / q;
  int bar = (int)(tick / 96); MChord c = S.hasCur ? S.cur : chordAt(bar);
  int st = S.style, L = S.level, ph = (((bar - S.progStart) % 8) + 8) % 8, intense = st == ST_GAME || st == ST_BOSS, tri = q == 3;
  float stepDur = spb() / q;
  if (S.metro && sub == 0) metroClick(t, bt == 0);
  /* ---- drums ---- */
  if (intense) {
    if (tri) { if ((sub == 0 && bt % 2 == 0) || sb == 5 || (L >= 3 && sb == 11)) kick(t, sb % 6 == 0 ? 1 : 0.7f); }
    else {
      if (sub == 0) kick(t, 1);
      if (st == ST_BOSS && (sb == 14 || (sb == 7 && bar % 2))) kick(t, 0.6f);
    }
    if (L >= 2 && sub == 0 && bt % 2 == 1) snare(t, 0.85f);
    if (!S.L.tempo) {
      if (tri) { if (sub > 0) hat(t, sub == 2 ? 0.5f : 0.28f, 0, 0); else if (bt % 2) hat(t, 0.3f, 1, 0); }
      else {
        if (sub == 2) hat(t, L >= 2 ? 0.5f : 0.32f, L >= 2, 0);
        if ((L >= 3 || st == ST_BOSS) && sub % 2 == 1) hat(t, 0.28f, 0, 0);
        else if (sub == 0 && L < 3) hat(t, 0.14f, 0, 0);
      }
    }
    if (L >= 2 && ph == 7 && bt >= 2) snare(t, 0.22f + (sb - n / 2) * 0.06f);
    if (ph == 7 && sb == 0 && L >= 3) riser(t, spb() * 4 - 0.02f, 0.6f);
    if (ph == 0 && sb == 0 && L >= 2) crash(t, 0.6f);
  } else if (st == ST_TITLE) {
    if (sb == 0 || sb == (tri ? 7 : 10)) kick(t, 0.6f);
    if (sub == 0 && bt == 2) snare(t, 0.55f);
    if (sub == 2 || (tri && sub == 1)) hat(t, 0.13f, 0, 0);
  } else if (st == ST_CALM) {
    if (sub == 2) hat(t, 0.09f, 0, 0);
  }
  /* ---- bass ---- */
  int bn = c.bass;
  if (intense) {
    if (tri) { if (sub != 1) bass(t, bn + (sub == 2 && bt == 3 ? 12 : 0), stepDur * 0.85f, sub == 0 ? 1 : 0.75f, 0.4f + (st == ST_BOSS ? 0.25f : 0)); }
    else if (st == ST_BOSS) {
      static const int pat[16] = { 0, 0, 12, 0, 0, 12, 0, 1, 0, 0, 12, 0, 0, 12, 7, 10 };
      if (sub != 0 || sb == 0) bass(t, bn + (pat[sb] == 1 ? (S.mode == MD_PHRYGIAN ? 1 : 0) : pat[sb]), stepDur * 0.9f, 0.9f, 0.55f + (sub == 2 ? 0.3f : 0));
    } else if (L <= 1) { if (sub == 2) bass(t, bn, stepDur * 1.8f, 0.9f, 0.35f); }
    else if (sub != 0) bass(t, bn + (sb == 14 && L >= 3 ? 12 : 0), stepDur * 0.85f, sub == 2 ? 1 : 0.7f, L >= 3 ? 0.5f : 0.32f);
  } else if (st == ST_TITLE) {
    if (sb == 0 || sb == (tri ? 5 : 6) || sb == (tri ? 8 : 10)) bass(t, bn, stepDur * (sb == 0 ? 5 : 3.5f), 0.6f, 0.12f);
  }
  /* ---- harmony ---- */
  if (sb == 0 && c.first && st != ST_SILENT) {
    float lvl = st == ST_CALM ? 0.055f : st == ST_OVER ? 0.065f : st == ST_TITLE ? 0.05f : 0.04f;
    int notes[5] = { c.voiced[0], c.voiced[1], c.voiced[2], c.voiced[3], c.ninth };
    pad(t, notes, S.L.spread ? 5 : 4, spb() * 4 * S.chordBars - 0.05f, lvl);
  }
  if (st == ST_TITLE || st == ST_CALM) {
    if (sb == 0 || (sb == (tri ? 7 : 10) && bar % 2 == 0)) { int nt[5] = { c.voiced[0] + 12, c.voiced[1] + 12, c.voiced[2] + 12, c.voiced[3] + 12, c.ninth + 12 }; rhodes(t, nt, 5, st == ST_CALM ? 0.7f : 1, 1.4f); }
  }
  if (intense && S.L.spread && sb == n - 2) { int nt[3] = { c.voiced[0] + 12, c.voiced[1] + 12, c.voiced[2] + 12 }; rhodes(t, nt, 3, 0.5f + 0.2f * S.L.spread, 0.5f); }
  /* ---- arp ---- */
  int arpOn = (intense && L >= 2) || st == ST_TITLE || st == ST_CALM;
  if (arpOn) {
    int every = (st == ST_TITLE || st == ST_CALM) ? 2 : 1;
    if (sb % every == 0) {
      int tones[8]; for (int k = 0; k < 4; k++) { tones[k] = c.voiced[k]; tones[k + 4] = c.voiced[k] + 12; }
      int i = (sb / every) % 8;
      float v = (sub == 0 ? 1 : 0.65f) * (st == ST_CALM ? 0.55f : st == ST_TITLE ? 0.6f : 0.8f);
      pluck(t, tones[ARPS[S.arp][i] % 8] + 12, v, every == 2 ? 0.55f : 0.3f, 0, 0);
    }
  }
  if (S.nphrase && intense && (L >= 3 || st == ST_BOSS)) for (int i = 0; i < S.nphrase; i++) if (S.phrase[i].s == sb) lead(t, S.phrase[i].m, S.phrase[i].d, st == ST_BOSS ? 1 : 0.85f);
  if (!intense) return;
  /* ---- layers composed by the player's build ---- */
  if (S.L.arc) {
    int on = tri ? (sub == 2 || (S.L.arc >= 2 && sub == 1)) : (sub == 3 || (S.L.arc >= 2 && sub == 1));
    if (on) { metal(t, 0.8f + 0.2f * S.L.arc, sb % 2 ? 0.55f : -0.55f); if (S.L.arc >= 3 && sub == 3) metal(t + stepDur / 2, 0.55f, -0.3f); }
  }
  if (S.L.orbit) {
    int tones[8]; for (int k = 0; k < 4; k++) { tones[k] = c.voiced[k]; tones[k + 4] = c.voiced[k] + 12; }
    i64 k = tick / (TPB / q); int drone = (int)(k % S.L.orbit);
    float a = (float)(beatAt(t) * TAU / 4) + drone * TAU / S.L.orbit;
    pluck(t, tones[k % 8] + 12, 0.5f, 0.2f, mcos(a) * 0.9f, 1);
    if (S.L.orbit >= 2 && k % 2 == 0) pluck(t, tones[(k >> 1) % 7] + 24, 0.3f, 0.35f, -mcos(a) * 0.9f, 1);
  }
  if (S.L.missile && sb == 0) { int nt[3] = { c.bass + 12, c.bass + 19, c.bass + 24 }; brass(t, nt, 3, 0.8f + 0.2f * S.L.missile); }
  if (S.L.accent && (sb == 0 || sb == (tri ? 7 : 10) || (S.L.accent >= 2 && sb == (tri ? 9 : 12)))) vox(t, c.voiced[sb == 0 ? 0 : 2] + 12, 1);
}
static void tickLayers(i64 tick, double t) {
  if (!S.L.tempo || !(S.style == ST_GAME || S.style == ST_BOSS)) return;
  int bt = (int)((tick % 96) / TPB), tb = (int)(tick % TPB), bar = (int)(tick / 96);
  if (S.L.tempo == 1) {
    int roll = bar % 2 == 1 && bt == 3 && tb >= 12;
    if (tick % (roll ? 2 : 4) == 0) hat(t, roll ? 0.18f + (tb - 12) * 0.02f : (tb == 0 ? 0.42f : tb % 8 == 0 ? 0.3f : 0.18f), 0, msin(tick * 0.4f) * 0.3f);
  } else if (tick % 3 == 0) {
    int s32 = (int)((tick % 96) / 3); float acc = s32 % 4 == 2 ? 0.45f : s32 % 2 ? 0.14f : 0.24f;
    hat(t, acc, s32 % 16 == 14, msin(tick * 0.3f) * 0.35f);
    if (s32 == 10 || s32 == 13 || s32 == 22 || s32 == 27 || s32 == 29) snare(t, 0.16f);
  }
}
static void snd_schedule(void) {
  double now = snd_now(), t = tickTime(S.nextTick);
  if (t < now - 0.25 || t > now + 1.5) S.nextTick = (i64)(beatAt(now + 0.01) * TPB + 0.999999);
  double rt = aud_rt;
  while ((t = tickTime(S.nextTick)) < now + LOOK) {
    i64 tick = S.nextTick++; int live = S.ok && t > rt - 0.005;
    if (tick % 96 == 0) barLine((int)(tick / 96), tick, t, live);
    if (tick % TPB == 0) beatLine(tick / TPB, t);
    if (live) {
      if (tick % (TPB / S.steps) == 0) schedStep(tick, t);
      tickLayers(tick, t);
    }
  }
}

/* ---------------- bus control ---------------- */
INL float sqv(int v) { return mpow(v / 10.f, 1.6f); }
static void snd_applyVolumes(void) {
  double t = snd_now();
  mx_push_param(t, P_MASTER, sqv(Set.master) * 0.9f, 0.02f);
  mx_push_param(t, P_MUSIC, sqv(Set.music) * 1.1f, 0.02f);
  mx_push_param(t, P_SFX, sqv(Set.sfx) * 1.25f * S.sfxScale, 0.02f);
}
static void snd_setSfxScale(float k) { if (k == S.sfxScale) return; S.sfxScale = k; snd_applyVolumes(); }
static float muffleHz(void) { return 20000 * mpow(500.f / 20000, S.muffle); }
static void snd_setMuffle(float amt, float tc) { S.muffle = amt; double t = snd_now(); mx_push_param(t, P_CUTCANCEL, 0, 0); mx_push_param(t, P_CUT, muffleHz(), tc); }
static void snd_restoreFilter(void) { snd_setMuffle(S.muffle, 0.3f); }
static void snd_setHum(float lvl) { lvl = mfloor(clampf(lvl, 0, 1) * 20 + 0.5f) / 20; if (lvl == S.humLvl) return; S.humLvl = lvl; mx_push_param(snd_now(), P_HUM, lvl * 0.22f, 0.15f); }
static void dip(float hz, float hold, float rel) { double t = snd_now(); mx_push_param(t, P_CUTCANCEL, 0, 0); mx_push_param(t, P_CUT, hz, 0.02f); mx_push_param(t + hold, P_CUT, muffleHz(), rel); }

/** Live spectrum of the score → visuals. */
static void snd_levels(void) {
  float tb = clampf((20 * mlog2(lev_bass + 1e-6f) * 0.30103f + 42) / 34, 0, 1);
  float th = clampf((20 * mlog2(lev_high + 1e-6f) * 0.30103f + 48) / 36, 0, 1);
  S.lv.bass = tb > S.lv.bass ? tb : S.lv.bass * 0.86f; S.lv.high = th > S.lv.high ? th : S.lv.high * 0.9f;
  S.lv.avg += (tb - S.lv.avg) * 0.03f;
  S.lv.kick = clampf((S.lv.bass - S.lv.avg) * 4, 0, 1) * 0.6f + clampf((S.lv.bass - 0.5f) * 2.2f, 0, 1) * 0.4f;
}

/* ---------------- game-facing SFX (x = world position for panning; NAN-free: use NOX) ---------------- */
#define NOX -99999.f
static float panOf(float x) { return x == NOX ? 0 : clampf((x - S.lisX) / (S.lisW * 0.55f), -1, 1) * 0.85f; }
static float distGain(float x) { return x == NOX ? 1 : 1 - clampf((mfabs(x - S.lisX) - S.lisW * 0.6f) / 1400, 0, 0.55f); }
static void sfxout(Voice *v, float x) { if (x != NOX) v->gain *= distGain(x); vpan(v, panOf(x)); }
enum { G_HIT, G_EX, G_GEOM, G_SP, G_EF, G_UIM, G_KLD, G_KLO, G_KLS, G_KLT, G_CH0 };
static int gate_(int k, float ms) { double n = plat_time() * 1000; if (S.gate[k] && n - S.gate[k] < ms) return 0; S.gate[k] = n; return 1; }
static int over(float cost, float limit) {
  double n = snd_now(); S.load *= mexp(-(float)(n - S.loadT) * 9); S.loadT = n;
  if (S.load + cost > limit) return 1;
  S.load += cost; return 0;
}
static double tnow(void) { return snd_now() + 0.005; }

static void sfx_shot(double t, int accent) {
  if (!S.ok) return; double n = snd_now(); if (t < n) t = n;
  Voice *x = vnew(t, 0.07f, B_SFX); vosc(x, O_NOISE, 0, 0, 1); vflt(x, F_BP, accent ? 2600 : 4200, 1.2f); env_perc(&x->amp, 0, 0.001f, accent ? 0.1f : 0.05f, accent ? 0.05f : 0.03f); vgo(x);
  if (accent) { x = vnew(t, 0.07f, B_SFX); vosc(x, O_SINE, 900, 0, 1); env_set(&x->fe, 0, 1); env_exp(&x->fe, 0.05f, 260.f / 900); env_perc(&x->amp, 0, 0.001f, 0.07f, 0.05f); vgo(x); }
}
/** Where a moment falls on the beat, as heard. 0 none, 1 down, 2 off (8th), 3 sixteenth, 4 triplet */
enum { BP_NONE, BP_DOWN, BP_OFF, BP_SIX, BP_TRI };
static int beatPlace(void) {
  int st = snd_steps(); double pos = visBeat() * st; double nearB = (double)(i64)(pos + (pos >= 0 ? 0.5 : -0.5));
  if (pos - nearB > 0.3 || nearB - pos > 0.3) return BP_NONE;
  int sub = (int)((((i64)nearB % st) + st) % st);
  return sub == 0 ? BP_DOWN : st == 3 ? BP_TRI : sub == 2 ? BP_OFF : BP_SIX;
}
static void killLayer(int place, double t, const MChord *ch, int big, float pan) {
  if (place == BP_DOWN) {
    Voice *x = vnew(t, 0.25f, B_SFX); vosc(x, O_SINE, 95, 0, 1); env_set(&x->fe, 0, 1); env_exp(&x->fe, 0.16f, 38.f / 95); env_perc(&x->amp, 0, 0.003f, big ? 0.32f : 0.22f, 0.2f); vpan(x, pan * 0.4f); vgo(x);
    for (int i = 1; i < 3; i++) bell(t, ch->voiced[i] + 12, big ? 0.09f : 0.065f, 1, 1.4f, 0.45f, pan * 0.6f);
  } else if (place == BP_OFF) {
    Voice *x = vnew(t, 0.07f, B_SFX); vosc(x, O_NOISE, 0, 0, 1); vflt(x, F_BP, 2300, 2.5f); env_perc(&x->amp, 0, 0.001f, 0.12f, 0.04f); vpan(x, pan); vgo(x);
    x = vnew(t, 0.05f, B_SFX); vosc(x, O_TRI, 820, 0, 1); env_set(&x->fe, 0, 1); env_exp(&x->fe, 0.03f, 560.f / 820); env_perc(&x->amp, 0, 0.001f, 0.06f, 0.03f); vpan(x, pan); vgo(x);
  } else if (place == BP_SIX) {
    bell(t, ch->voiced[0] + 36, 0.05f, 3.5f, 1.2f, 0.18f, -pan);
  } else if (place == BP_TRI) {
    Voice *x = vnew(t, 0.12f, B_SFX); vosc(x, O_NOISE, 0, 0, 1); vflt(x, F_BP, 5200, 1.1f); env_set(&x->amp, 0, 0.0001f); env_exp(&x->amp, 0.04f, 0.07f); env_exp(&x->amp, 0.09f, 0.0001f); vpan(x, pan); vgo(x);
    bell(t, ch->voiced[0] + 12, 0.08f, 4, 0.8f, 0.25f, pan * 0.5f);
  }
}
/** Kill note: quantized to the next step, climbing the chord as the chain grows. */
static void sfx_killNote(int chainN, int big, float x, int place) {
  if (!S.ok) return;
  i64 slot; double qt = nextGrid(snd_steps(), 0.012, &slot);
  int si = (int)(slot & 31);
  if (S.slots[si].slot != (int)slot) { S.slots[si].slot = (int)slot; S.slots[si].n = 0; }
  if (S.slots[si].n >= 2 || over(2, 44)) return; S.slots[si].n++;
  MChord ch = chordAtTime(qt); int ladder[9], nl = 0;
  for (int o = 0; o < 3; o++) for (int i = 0; i < 3; i++) ladder[nl++] = ch.voiced[i] + 12 + o * 12;
  int m = ladder[chainN < nl - 1 ? chainN : nl - 1]; float pan = panOf(x);
  bell(qt, m, big ? 0.3f : 0.2f, 2, big ? 3.2f : 2.2f, big ? 1.1f : 0.55f, pan);
  if (big) bell(qt, m - 12, 0.18f, 1, 1.5f, 1.2f, pan);
  if (place && gate_(G_KLD + place - 1, place == BP_DOWN ? 70 : 45) && !over(2, 30)) killLayer(place, qt, &ch, big, pan);
}
static void sfx_hit(float x) {
  if (!S.ok || !gate_(G_HIT, 28) || over(1, 24)) return; double t = tnow();
  Voice *v = vnew(t, 0.06f, B_SFX); vosc(v, O_NOISE, 0, 0, 1); vflt(v, F_BP, 3200, 1.5f); env_perc(&v->amp, 0, 0.001f, 0.065f, 0.03f); sfxout(v, x); vgo(v);
}
static void sfx_explode(float size, float x) {
  if (!S.ok || !gate_(G_EX, 22) || over(size > 1.5f ? 1 : 2, size > 1.5f ? 60 : 34)) return; double t = tnow(); size = clampf(size, 0.3f, 3);
  Voice *v = vnew(t, 0.92f, B_SFX); vosc(v, O_NOISE, 0, 0, 1); v->flt = F_LP; v->q = 0.9f; env_set(&v->cut, 0, 1800 + size * 900); env_exp(&v->cut, 0.25f + size * 0.3f, 120);
  env_perc(&v->amp, 0, 0.002f, 0.15f * size, 0.25f + size * 0.3f); if (size > 1.5f) v->srev = 0.6f; sfxout(v, x); vgo(v);
  v = vnew(t, 0.7f, B_SFX); vosc(v, O_SINE, 120, 0, 1); env_set(&v->fe, 0, 1); env_exp(&v->fe, 0.22f + size * 0.1f, 34.f / 120);
  env_perc(&v->amp, 0, 0.002f, 0.2f * minf(size, 2), 0.25f + size * 0.15f); sfxout(v, x); vgo(v);
}
static void sfx_playerHit(void) {
  if (!S.ok) return; double t = tnow();
  Voice *v = vnew(t, 0.6f, B_SFX); vosc(v, O_SQUARE, 240, 0, 1); env_set(&v->fe, 0, 1); env_exp(&v->fe, 0.5f, 0.2f); v->ws = SHP_DIST; env_perc(&v->amp, 0, 0.002f, 0.22f, 0.55f); vgo(v);
  v = vnew(t, 0.62f, B_SFX); vosc(v, O_NOISE, 0, 0, 1); vflt(v, F_LP, 1200, 1); env_perc(&v->amp, 0, 0.002f, 0.4f, 0.45f); vgo(v);
  dip(380, 0.35f, 0.35f);
}
/** tier: 0 perfect, 1 great, 2 good, -1 off the beat. Better timing, fuller chord. */
static void sfx_dash(int tier) {
  if (!S.ok) return; double t = tnow();
  Voice *v = vnew(t, 0.27f, B_SFX); vosc(v, O_NOISE, 0, 0, 1); v->flt = F_BP; v->q = 1.6f; env_set(&v->cut, 0, 600); env_exp(&v->cut, 0.17f, 3400);
  env_perc(&v->amp, 0, 0.01f, tier >= 0 ? 0.2f : 0.14f, 0.18f); vgo(v);
  if (tier < 0) return;
  MChord ch = snd_chord(); int n = tier == 0 ? 3 : tier == 1 ? 2 : 1;
  for (int i = 0; i < n; i++) bell(t + i * 0.035, ch.voiced[i] + 24, tier == 0 ? 0.1f : 0.08f, 3.01f, 1.6f, tier == 0 ? 1.1f : 0.6f, 0);
  if (tier != 2) { v = vnew(t, 0.32f, B_SFX); vosc(v, O_SINE, 80, 0, 1); env_set(&v->fe, 0, 1); env_exp(&v->fe, 0.3f, 30.f / 80); env_perc(&v->amp, 0, 0.002f, tier == 0 ? 0.4f : 0.22f, 0.3f); vgo(v); }
}
/** Guardian klaxon: a rising two-oscillator whoop over a sub pulse, scheduled on the beat. */
static void sfx_klaxon(double t, int hi) {
  if (!S.ok) return; double n = snd_now() + 0.005; if (t < n) t = n;
  float dur = minf(0.5f, spb() * 0.85f), f0 = hi ? 420 : 330, f1 = hi ? 940 : 760;
  Voice *v = vnew(t, dur + 0.05f, B_SFX); vosc(v, O_SQUARE, f0, 0, 1); vosc(v, O_SAW, f0 * 1.006f, 0, 1); env_set(&v->fe, 0, 1); env_exp(&v->fe, dur * 0.8f, f1 / f0);
  v->ws = SHP_SOFT; vflt(v, F_BP, 1300, 0.8f);
  env_set(&v->amp, 0, 0.0001f); env_exp(&v->amp, 0.03f, 0.12f); env_set(&v->amp, dur * 0.72f, 0.12f); env_exp(&v->amp, dur, 0.0001f); v->srev = 0.35f; vgo(v);
  v = vnew(t, dur + 0.05f, B_SFX); vosc(v, O_SINE, 72, 0, 1); env_perc(&v->amp, 0, 0.005f, 0.3f, 0.32f); vgo(v);
}
/** Bullet time: the score dips under water and a tape-stop sweep marks the edges. */
static void sfx_bulletTime(int on) {
  if (!S.ok) return;
  snd_setMuffle(on ? 0.5f : 0, on ? 0.06f : 0.15f);
  double t = tnow();
  Voice *v = vnew(t, 0.35f, B_SFX); vosc(v, O_SAW, on ? 420 : 110, 0, 1); env_set(&v->fe, 0, 1); env_exp(&v->fe, 0.28f, on ? 70.f / 420 : 420.f / 110);
  vflt(v, F_LP, 1400, 2); env_perc(&v->amp, 0, 0.01f, 0.07f, 0.3f); vgo(v);
}
static void sfx_bomb(int quiet) {
  if (!S.ok) return; double t = tnow();
  Voice *v = vnew(t, 1.6f, B_SFX); vosc(v, O_SINE, 95, 0, 1); env_set(&v->fe, 0, 1); env_exp(&v->fe, 1.4f, 22.f / 95); env_perc(&v->amp, 0, 0.005f, 0.85f, 1.5f); v->prio = 1; vgo(v);
  v = vnew(t, 2.02f, B_SFX); vosc(v, O_NOISE, 0, 0, 1); v->flt = F_LP; v->q = 0.7f; env_set(&v->cut, 0, 7000); env_exp(&v->cut, 1.7f, 70);
  env_perc(&v->amp, 0, 0.003f, 0.45f, 1.8f); v->srev = 0.8f; v->prio = 1; vgo(v);
  if (!quiet) dip(240, 0.6f, 0.5f);
}
static void sfx_geom(int k, float x) {
  if (!S.ok || !gate_(G_GEOM, 30) || over(1, 26)) return; double t = tnow();
  MChord ch = snd_chord(); int m = ch.voiced[k % 3] + 36 + 12 * ((k % 6) / 3);
  Voice *v = vnew(t, 0.12f, B_SFX); vosc(v, O_SINE, mtof((float)m), 0, 1); env_perc(&v->amp, 0, 0.002f, 0.04f, 0.09f); sfxout(v, x); vgo(v);
}
static void sfx_spawn(float x) {
  if (!S.ok || !gate_(G_SP, 45) || over(1, 22)) return; double t = tnow();
  Voice *v = vnew(t, 0.25f, B_SFX); vosc(v, O_TRI, 180, 0, 1); env_set(&v->fe, 0, 1); env_exp(&v->fe, 0.16f, 520.f / 180); env_perc(&v->amp, 0, 0.06f, 0.03f, 0.12f); sfxout(v, x); vgo(v);
}
static void sfx_enemyFire(float x) {
  if (!S.ok || !gate_(G_EF, 40) || over(1, 26)) return; double t = tnow();
  Voice *v = vnew(t, 0.1f, B_SFX); vosc(v, O_SAW, 820, 0, 1); env_set(&v->fe, 0, 1); env_exp(&v->fe, 0.08f, 240.f / 820); vflt(v, F_LP, 2400, 2); env_perc(&v->amp, 0, 0.002f, 0.042f, 0.08f); sfxout(v, x); vgo(v);
}
/** Rising whine before an attack — panned so you hear an off-screen threat coming. */
static void sfx_charge(float dur, float x) {
  int b = x == NOX ? 7 : clampi((int)(x / 300 + 0.5f), 0, 6);
  if (!S.ok || !gate_(G_CH0 + b, 60)) return; double t = tnow();
  Voice *v = vnew(t, dur + 0.05f, B_SFX); vosc(v, O_SAW, 110, 0, 1); env_set(&v->fe, 0, 1); env_exp(&v->fe, dur, 8); vflt(v, F_LP, 1200, 6);
  env_set(&v->amp, 0, 0.0001f); env_exp(&v->amp, dur, x == NOX ? 0.06f : 0.045f); env_lin(&v->amp, dur + 0.03f, 0); sfxout(v, x); vgo(v);
}
static void sfx_laser(float dur) {
  if (!S.ok) return; double t = tnow();
  Voice *v = vnew(t, dur + 0.02f, B_SFX); vosc(v, O_SAW, 73, 0, 1); vosc(v, O_SQUARE, 110.5f, 0, 1); vflt(v, F_LP, 1800, 3);
  env_set(&v->amp, 0, 0.0001f); env_exp(&v->amp, 0.02f, 0.15f); env_set(&v->amp, maxf(0.03f, dur - 0.05f), 0.15f); env_exp(&v->amp, dur, 0.0001f); v->prio = 1; vgo(v);
}
static void sfx_bossRoar(void) {
  if (!S.ok) return; double t = tnow();
  static const float FD[3][2] = { { 55, 0 }, { 55.6f, 0 }, { 82.4f, 0.05f } };
  for (int i = 0; i < 3; i++) {
    Voice *v = vnew(t + FD[i][1], 2.5f - FD[i][1], B_SFX); vosc(v, O_SAW, FD[i][0], 0, 1); env_set(&v->fe, 0, 1); env_exp(&v->fe, 2.2f - FD[i][1], 0.5f);
    v->flt = F_LP; v->q = 5; env_set(&v->cut, 0, 150); env_exp(&v->cut, 0.6f, 1400); env_exp(&v->cut, 2.2f, 90);
    env_set(&v->amp, 0, 0.0001f); env_exp(&v->amp, 0.3f, 0.2f); env_exp(&v->amp, 2.4f, 0.0001f); v->srev = 0.5f; v->prio = 1; vgo(v);
  }
  Voice *v = vnew(t, 2.22f, B_SFX); vosc(v, O_NOISE, 0, 0, 1); vflt(v, F_HP, 3200, 0.5f); env_perc(&v->amp, 0, 0.002f, 0.2f, 1.9f); v->srev = 0.5f; vgo(v);
}
enum { UI_MOVE, UI_OK, UI_BACK, UI_START };
static void sfx_ui(int kind) {
  if (!S.ok) return; double t = tnow(); MChord ch = snd_chord();
  if (kind == UI_MOVE) { if (!gate_(G_UIM, 40)) return; Voice *v = vnew(t, 0.08f, B_SFX); vosc(v, O_SINE, mtof((float)ch.voiced[0] + 36), 0, 1); env_perc(&v->amp, 0, 0.002f, 0.04f, 0.06f); vgo(v); }
  else if (kind == UI_OK) for (int i = 0; i < 3; i++) bell(t + i * 0.045, ch.voiced[i] + 24, 0.09f, 2, 1.8f, 0.5f, 0);
  else if (kind == UI_BACK) { bell(t, ch.voiced[0] + 12 + 12, 0.08f, 2, 1.5f, 0.35f, 0); bell(t + 0.05, ch.voiced[0] + 12 + 7, 0.08f, 2, 1.5f, 0.35f, 0); }
  else { Voice *v = vnew(t, 0.57f, B_SFX); vosc(v, O_NOISE, 0, 0, 1); v->flt = F_BP; v->q = 2.5f; env_set(&v->cut, 0, 250); env_exp(&v->cut, 0.5f, 7500); env_set(&v->amp, 0, 0.0001f); env_exp(&v->amp, 0.5f, 0.24f); env_lin(&v->amp, 0.54f, 0); v->srev = 0.5f; vgo(v);
    for (int i = 0; i < 4; i++) bell(t + i * 0.06, ch.voiced[i % 4] + 24, 0.1f, 2, 2, 0.8f, 0); }
}
static void sfx_fanfare(void) {
  if (!S.ok) return; double qt = nextGrid(snd_steps(), 0.02, NULL); MChord ch = snd_chord(); float st = spb() / snd_steps();
  int tones[9], n = 0; for (int o = 1; o <= 3; o++) for (int i = 0; i < 3; i++) tones[n++] = ch.voiced[i] + 12 * o;
  static const int seq[9] = { 0, 1, 2, 3, 4, 5, 6, 5, 7 };
  for (int i = 0; i < 9; i++) bell(qt + i * st, tones[seq[i] % 9], 0.11f, 2, 2.2f, 0.6f, 0);
}
static void sfx_milestone(void) { if (!S.ok) return; double qt = nextGrid(snd_steps(), 0.01, NULL); MChord ch = snd_chord(); float st = spb() / snd_steps(); for (int i = 0; i < 4; i++) bell(qt + i * st * 0.5f, ch.voiced[i % 3] + 24 + (i == 3 ? 12 : 0), 0.12f, 2, 2.4f, 0.7f, 0); }
static void sfx_heal(void) { if (!S.ok) return; double qt = nextGrid(snd_steps(), 0.01, NULL); MChord ch = snd_chord(); float st = spb() / snd_steps(); for (int i = 0; i < 3; i++) bell(qt + i * st * 0.5f, ch.voiced[i] + 24, 0.11f, 1, 1, 0.6f, 0); }
static void sfx_waveStart(void) { if (!S.ok) return; double qt = nextGrid(1, 0.02, NULL); riser(qt, spb() * 4 - 0.03f, 0.7f); }
static void sfx_death(void) {
  if (!S.ok) return; double t = tnow(); S.gate[G_EX] = 0; S.load = 0; sfx_explode(3, NOX);
  Voice *v = vnew(t, 2.6f, B_SFX); vosc(v, O_SAW, 220, 0, 1); env_set(&v->fe, 0, 1); env_exp(&v->fe, 2.4f, 30.f / 220); vflt(v, F_LP, 900, 4); env_perc(&v->amp, 0, 0.01f, 0.2f, 2.4f); v->prio = 1; vgo(v);
  dip(160, 30, 1);
}

static void snd_init(void) {
  mzero(&S, sizeof S);
  S.segs[0].t = 0; S.segs[0].b = 0; S.segs[0].bpm = 100; S.nseg = 1;
  S.met[0].tick = 0; S.met[0].steps = 4; S.nmet = 1;
  S.lisX = ARENA_W / 2; S.lisW = 1600; S.sfxScale = 1; S.humLvl = -1;
  S.root = 53; S.mode = MD_LYDIAN; S.prog[0] = 0; S.prog[1] = 3; S.prog[2] = 5; S.prog[3] = 4; S.chordBars = 2; S.style = ST_TITLE; S.level = 1; S.arp = 0; S.bpm = 100; S.steps = 4;
  S.lv.avg = 0.4f;
  double now = snd_now();
  S.segs[0].t = now + 0.12; S.segs[0].b = 0;
  S.ok = 1;
  snd_applyVolumes();
}
/* =========================================================================
   RENDERER — software, additive, HDR-ish.
   · The scene buffer packs 10 bits per channel (R:22-31, G:11-20, B:0-9,
     guard bits between); 256 = 1.0, so values up to 4.0 keep hot cores.
     Lines add into it with per-field saturation in one integer add.
   · Output: any size up to 3840×2400 (window, desktop-matched or switched
     fullscreen), RGB565 on 16-bit displays and XRGB8888 above. The scene is
     drawn at the output size or at 1/2 or 1/4 of it and scaled up by the
     composite (quality tiers).
   · Thin lines: Wu anti-aliased, energy-correct across resolutions.
     Thick lines, dots, halos: capsules with analytic coverage.
   · Bloom: threshold + 160×120 / 80×60 / 40×30 box-blur pyramid.
   · Composite: per-channel lookup tables (ACES, split-tone, gamma 0.88,
     flash, vignette, scanlines) straight to RGB565.
   · The HUD and menus are drawn afterwards on the 640×480 output.
   ========================================================================= */
#define NWMAX WKMAX      /* worker threads at most */
static int RW = 640, RH = 480; static float XS = 1, YS = 1, iXS = 1, iYS = 1;
static int rXs, rYs;           /* scene = output >> (rXs, rYs) */
static u32 *scene;
static void *outbuf;           /* OW*OH pixels, set by the platform layer */
static int fmt32;              /* output pixels: 0 = RGB565, 1 = XRGB8888 */
#define OB16 ((u16 *)outbuf)
#define OB32 ((u32 *)outbuf)
/* UI space: menus and HUD are laid out in units, 480 tall (or 640 wide, whichever binds);
   US = output pixels per unit. Menus are a 640×480 layout centred at (UOX, UOY). */
static float US = 1, UW = 640, UH = 480, UOX = 0, UOY = 0;
#define PK(r, g, b) (((u32)(r) << 22) | ((u32)(g) << 11) | (u32)(b))
#define GUARD 0x00200400u
INL u32 padd(u32 a, u32 b) {
  u32 s = a + b, o = s & GUARD; int c = s < a;
  if (o | c) { s = (s & ~GUARD) | ((o >> 10) * 1023u); if (c) s |= 0xFFC00000u; }
  return s;
}
INL u32 psub(u32 a, int r, int g, int b) {
  int ar = (int)(a >> 22) - r, ag = (int)((a >> 11) & 1023) - g, ab = (int)(a & 1023) - b;
  return PK(ar < 0 ? 0 : ar, ag < 0 ? 0 : ag, ab < 0 ? 0 : ab);
}
/* per-byte saturating add of two XRGB8888 pixels: the low 7 bits add without crossing lanes, the top
   bit and its carry come from the majority rule, and overflowing lanes are filled with 0xFF */
INL u32 sat_add8(u32 a, u32 b) {
  u32 lo = (a & 0x7F7F7Fu) + (b & 0x7F7F7Fu), sum = lo ^ ((a ^ b) & 0x808080u), ov = ((a & b) | (lo & (a | b))) & 0x808080u;
  return sum | ((ov << 1) - (ov >> 7));
}
typedef struct BRow BRow;
struct CapRow_; struct OvRow_;
typedef struct {
  void (*comp32)(u32 *d, const u32 *sp, const BRow *b, const u32 *L, int n4, int xs);  /* n4 groups of 4 output px */
  void (*addspan32)(u32 *p, int n, u32 c);                  /* per-byte saturating add of one colour */
  void (*addrow32)(u32 *p, const u32 *c, int n);            /* … of a row of colours */
  void (*dim32)(u32 *d, const u32 *a, int n, int cls);      /* scanline copy ×0.906 / ×0.8125 */
  void (*scale32)(u32 *p, size_t n, int kk, u32 tv);        /* p·kk/256 + tint (menu backdrop) */
  void (*glass32)(u32 *p, int n, u32 v);                    /* p/4 + tint (menu panels) */
  void (*padd_span)(u32 *p, int n, u32 c);                  /* scene: 10:10:10 saturating add of one colour */
  void (*caprow)(u32 *row, int a, int b, const struct CapRow_ *c);   /* capsule edge pixels of one row */
  void (*emit32)(u32 *d, const u32 *a, int n, int cls, int nt);       /* scratch row → output row: copy or scanline dim */
  void (*ovrow32)(u32 *row, int a, int b, const struct OvRow_ *c);     /* overlay capsule pixels of one row (32-bit output) */
  void (*fence)(void);                                                 /* after non-temporal stores */
  int gather;                                                          /* composite uses AVX2 gathers (picked by timing) */
} Kern;
static Kern K;
/* A raster band: scene rows [y0, y1) and whether colour is subtracted (the dark batch). Bands let the
   world be drawn by several threads at once, each into its own rows; every pixel gets the same value
   whichever band or path draws it. */
typedef struct { int y0, y1, sub; } RBand;
static const RBand RB_ALL = { 0, 1 << 30, 0 };
INL void plot_b(const RBand *rb, int x, int y, int cr, int cg, int cb, int w) {
  if ((unsigned)x >= (unsigned)RW || (unsigned)y >= (unsigned)RH || y < rb->y0 || y >= rb->y1 || w <= 0) return;
  int r = (cr * w) >> 8, g = (cg * w) >> 8, b = (cb * w) >> 8;
  if (r > 1023) r = 1023; if (g > 1023) g = 1023; if (b > 1023) b = 1023;
  u32 *p = scene + y * RW + x;
  *p = rb->sub ? psub(*p, r, g, b) : padd(*p, PK(r, g, b));
}
INL void plot_pk(const RBand *rb, int x, int y, u32 c) {
  if ((unsigned)x >= (unsigned)RW || (unsigned)y >= (unsigned)RH || y < rb->y0 || y >= rb->y1) return;
  u32 *p = scene + y * RW + x; *p = padd(*p, c);
}
INL void plot(int x, int y, int cr, int cg, int cb, int w) { plot_b(&RB_ALL, x, y, cr, cg, cb, w); }
/* Liang–Barsky clip of the parametric segment to a rectangle */
static int clip_lb(float x0, float y0, float dx, float dy, float xmin, float ymin, float xmax, float ymax, float *t0, float *t1) {
  float p[4] = { -dx, dx, -dy, dy }, q[4] = { x0 - xmin, xmax - x0, y0 - ymin, ymax - y0 };
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0) { if (q[i] < 0) return 0; continue; }
    float r = q[i] / p[i];
    if (p[i] < 0) { if (r > *t1) return 0; if (r > *t0) *t0 = r; }
    else { if (r < *t0) return 0; if (r < *t1) *t1 = r; }
  }
  return 1;
}
/** Anti-aliased thin line, output-pixel coordinates; g0/g1 = brightness × width (output px) at each end. */
static void rs_wu_b(const RBand *rb, float x0, float y0, float x1, float y1, int cr, int cg, int cb, float g0, float g1) {
  float X0 = x0 * iXS - 0.5f, Y0 = y0 * iYS - 0.5f, X1 = x1 * iXS - 0.5f, Y1 = y1 * iYS - 0.5f, dx = X1 - X0, dy = Y1 - Y0, t0 = 0, t1 = 1;
  if ((Y0 < rb->y0 - 2 && Y1 < rb->y0 - 2) || (Y0 > rb->y1 + 1 && Y1 > rb->y1 + 1)) return;
  if (!(X0 >= -1.5f && X1 >= -1.5f && Y0 >= -1.5f && Y1 >= -1.5f && X0 <= RW + 0.5f && X1 <= RW + 0.5f && Y0 <= RH + 0.5f && Y1 <= RH + 0.5f) &&
      !clip_lb(X0, Y0, dx, dy, -1.5f, -1.5f, RW + 0.5f, RH + 0.5f, &t0, &t1)) return;
  float ax = X0 + dx * t0, ay = Y0 + dy * t0, bx = X0 + dx * t1, by = Y0 + dy * t1, ga = g0 + (g1 - g0) * t0, gb = g0 + (g1 - g0) * t1;
  int steep = mfabs(dy) > mfabs(dx);
  float a0, b0, a1, b1;
  if (steep) { a0 = ay; b0 = ax; a1 = by; b1 = bx; } else { a0 = ax; b0 = ay; a1 = bx; b1 = by; }
  if (a0 > a1) { float t = a0; a0 = a1; a1 = t; t = b0; b0 = b1; b1 = t; t = ga; ga = gb; gb = t; }
  float len = a1 - a0, slope = len > 1e-4f ? (b1 - b0) / len : 0;
  float k = steep ? msqrt(YS * YS + slope * slope * XS * XS) / (XS * YS) : msqrt(XS * XS + slope * slope * YS * YS) / (XS * YS);
  int i0 = mlrint(a0), i1 = mlrint(a1);
  if (i1 <= i0) { /* shorter than a pixel: one splat carrying its energy */
    float g = (ga + gb) * 0.5f * k * (len > 0.05f ? len : 0.05f) * 256, bm = b0 + slope * (i0 - a0);
    int j = mfloori(bm); float f = bm - j; int w0 = mlrint((1 - f) * g), w1 = mlrint(f * g);
    if (steep) { plot_b(rb, j, i0, cr, cg, cb, w0); plot_b(rb, j + 1, i0, cr, cg, cb, w1); } else { plot_b(rb, i0, j, cr, cg, cb, w0); plot_b(rb, i0, j + 1, cr, cg, cb, w1); }
    return;
  }
  i32 B = mlrint((b0 + slope * (i0 - a0)) * 65536.f), dB = mlrint(slope * 65536.f);
  float gstep = len > 1e-4f ? (gb - ga) / len : 0;
  i32 G = mlrint((ga + gstep * (i0 - a0)) * k * 65536.f), dG = mlrint(gstep * k * 65536.f);
  if (steep) {   /* rows are the main axis: keep only this band's */
    int lo = rb->y0 > 0 ? rb->y0 : 0, hi = (rb->y1 < RH ? rb->y1 : RH) - 1;
    if (i0 < lo) { if (lo > i1) return; B += dB * (lo - i0); G += dG * (lo - i0); i0 = lo; }
    if (i1 > hi) { if (hi < i0) return; i1 = hi; }
  } else if (dB && (rb->y0 > 0 || rb->y1 < RH)) {   /* columns are the main axis: keep the columns whose two rows j, j+1 touch this band */
    i32 lo = (i32)(rb->y0 - 1) * 65536, hi = (i32)(rb->y1 < RH + 2 ? rb->y1 : RH + 2) * 65536 - 1;   /* need lo <= B + dB*k <= hi */
    int k0, k1, n0 = i1 - i0;
    if (dB > 0) { k0 = B >= lo ? 0 : (int)((lo - B + dB - 1) / dB); k1 = B > hi ? -1 : (int)((hi - B) / dB); }
    else { i32 e = -dB; k0 = B <= hi ? 0 : (int)((B - hi + e - 1) / e); k1 = B < lo ? -1 : (int)((B - lo) / e); }
    if (k1 > n0) k1 = n0;
    if (k0 > k1) return;
    B += dB * k0; G += dG * k0; i1 = i0 + k1; i0 += k0;
  }
  int n = i1 - i0, mainN = steep ? RH : RW, minN = steep ? RW : RH;
  i32 Bend = B + dB * n, Gend = G + dG * n;
  int jA = B >> 16, jB = Bend >> 16, jlo = jA < jB ? jA : jB, jhi = jA < jB ? jB : jA;
  int gmax = (G > Gend ? G : Gend) >> 8, cmax = cr > cg ? cr : cg; if (cb > cmax) cmax = cb;
  int nosat = !rb->sub && G >= 0 && Gend >= 0 && cmax * gmax < 262144;
  if (nosat && i0 >= 0 && i1 < mainN && jlo >= 0 && jhi + 1 < minN && (steep || (jlo >= rb->y0 && jhi + 1 < rb->y1))) {
    /* fast path: whole run inside the screen and the band, no per-plot checks or saturation */
    int ms = steep ? RW : 1, ns = steep ? 1 : RW;
    u32 *base = scene + i0 * ms;
    if (dG == 0) {   /* constant width (the grid): split the weight by packed subtraction */
      int g = G >> 8; u32 full = PK((cr * g) >> 8, (cg * g) >> 8, (cb * g) >> 8);
      for (int i = 0; i <= n; i++, B += dB, base += ms) {
        int w1 = (((B >> 8) & 255) * g) >> 8;
        u32 c1 = PK((cr * w1) >> 8, (cg * w1) >> 8, (cb * w1) >> 8), *p = base + (B >> 16) * ns;
        p[0] = padd(p[0], full - c1); p[ns] = padd(p[ns], c1);
      }
    } else {
      for (int i = 0; i <= n; i++, B += dB, G += dG, base += ms) {
        int g = G >> 8, w1 = (((B >> 8) & 255) * g) >> 8, w0 = g - w1;
        u32 *p = base + (B >> 16) * ns;
        p[0] = padd(p[0], PK((cr * w0) >> 8, (cg * w0) >> 8, (cb * w0) >> 8)); p[ns] = padd(p[ns], PK((cr * w1) >> 8, (cg * w1) >> 8, (cb * w1) >> 8));
      }
    }
    return;
  }
  /* general path: per-plot clipping (screen edges, band edges, saturation, the dark batch) */
  u32 full = 0; int split = nosat && dG == 0;
  if (split) { int g = G >> 8; full = PK((cr * g) >> 8, (cg * g) >> 8, (cb * g) >> 8); }
  for (int i = i0; i <= i1; i++, B += dB, G += dG) {
    int j = B >> 16, f = (B >> 8) & 255, g = G >> 8; if (g <= 0) continue;
    int w1 = (f * g) >> 8, w0 = g - w1;
    if (split) {
      u32 c1 = PK((cr * w1) >> 8, (cg * w1) >> 8, (cb * w1) >> 8), c0 = full - c1;
      if (steep) { plot_pk(rb, j, i, c0); plot_pk(rb, j + 1, i, c1); } else { plot_pk(rb, i, j, c0); plot_pk(rb, i, j + 1, c1); }
    } else if (steep) { plot_b(rb, j, i, cr, cg, cb, w0); plot_b(rb, j + 1, i, cr, cg, cb, w1); }
    else { plot_b(rb, i, j, cr, cg, cb, w0); plot_b(rb, i, j + 1, cr, cg, cb, w1); }
  }
}
static void rs_wu(float x0, float y0, float x1, float y1, int cr, int cg, int cb, float g0, float g1) { rs_wu_b(&RB_ALL, x0, y0, x1, y1, cr, cg, cb, g0, g1); }
/** Thick line / disc with anti-aliased edges (soft = linear falloff, used for halos). Output-pixel units. */
/* The x-interval where a horizontal line Y meets a capsule (segment A→A+d, radius r). A capsule is
   convex, so the answer is one interval: the union of what the two end discs and the slab give. */
typedef struct { float x0, y0, dx, dy, ux, uy, L, iux, iuy; } CapG;
static void cap_geom(CapG *g, float x0, float y0, float x1, float y1) {
  g->x0 = x0; g->y0 = y0; g->dx = x1 - x0; g->dy = y1 - y0; g->L = msqrt(g->dx * g->dx + g->dy * g->dy);
  if (g->L > 1e-4f) { g->ux = g->dx / g->L; g->uy = g->dy / g->L; } else { g->ux = 1; g->uy = 0; g->L = 0; }
  g->iux = mfabs(g->ux) > 1e-5f ? 1 / g->ux : 0; g->iuy = mfabs(g->uy) > 1e-5f ? 1 / g->uy : 0;
}
static int cap_span(const CapG *g, float Y, float r, float *xl, float *xr) {
  float lo = 1e30f, hi = -1e30f, h, s;
  h = Y - g->y0; if (h * h <= r * r) { s = msqrt(r * r - h * h); lo = g->x0 - s; hi = g->x0 + s; }
  h = Y - (g->y0 + g->dy); if (h * h <= r * r) { s = msqrt(r * r - h * h); float c = g->x0 + g->dx; if (c - s < lo) lo = c - s; if (c + s > hi) hi = c + s; }
  if (g->L > 0) {
    float yy = Y - g->y0, a0 = -1e30f, a1 = 1e30f, b0, b1;
    if (g->iuy != 0) { b0 = (g->ux * yy - r) * g->iuy; b1 = (g->ux * yy + r) * g->iuy; if (b0 > b1) { float t = b0; b0 = b1; b1 = t; } a0 = b0; a1 = b1; }
    else if (mfabs(yy) > r) a0 = 1e30f;
    if (g->iux != 0) { b0 = -g->uy * yy * g->iux; b1 = (g->L - g->uy * yy) * g->iux; if (b0 > b1) { float t = b0; b0 = b1; b1 = t; } if (b0 > a0) a0 = b0; if (b1 < a1) a1 = b1; }
    else { float p = g->uy * yy; if (p < 0 || p > g->L) a0 = 1e30f; }
    if (a0 <= a1) { if (g->x0 + a0 < lo) lo = g->x0 + a0; if (g->x0 + a1 > hi) hi = g->x0 + a1; }
  }
  *xl = lo; *xr = hi; return lo <= hi;
}
INL float cap_dist2(const CapG *g, float px, float py, float invL2) {
  float t = (px * g->dx + py * g->dy) * invL2; t = t < 0 ? 0 : t > 1 ? 1 : t;
  float ex = px - g->dx * t, ey = py - g->dy * t; return ex * ex + ey * ey;
}
/* One row of capsule edge pixels [a, b]: soft halos (linear falloff) or anti-aliased hard edges. */
typedef struct CapRow_ { float XS, x0, py, dx, dy, invL2, R, R2, invR, invpix, rin2, rout2; int cr, cg, cb, soft; } CapRow;
static void caprow_c(u32 *row, int a, int b, const CapRow *c) {
  for (int i = a; i <= b; i++) {
    float px = (i + 0.5f) * c->XS - c->x0, t = (px * c->dx + c->py * c->dy) * c->invL2; t = t < 0 ? 0 : t > 1 ? 1 : t;
    float ex = px - c->dx * t, ey = c->py - c->dy * t, d2 = ex * ex + ey * ey; int w;
    if (c->soft) { if (d2 >= c->R2) continue; w = mlrint((1 - msqrt(d2) * c->invR) * 256); }
    else { if (d2 >= c->rout2) continue; if (d2 <= c->rin2) w = 256; else { float v = (c->R - msqrt(d2)) * c->invpix + 0.5f; w = mlrint(clampf(v, 0, 1) * 256); } }
    if (w <= 0) continue;
    int r = (c->cr * w) >> 8, g = (c->cg * w) >> 8, bb = (c->cb * w) >> 8; if (r > 1023) r = 1023; if (g > 1023) g = 1023; if (bb > 1023) bb = 1023;
    row[i] = padd(row[i], PK(r, g, bb));
  }
}
/* One row of an overlay capsule (UI text strokes, HUD lines) on 32-bit output: coverage per pixel,
   colour × coverage added with per-byte saturation. Used over the bounding box of small capsules. */
typedef struct OvRow_ { float x0, py, dx, dy, invL2, R, rin2, rout2; int r, g, b; } OvRow;
static void ovrow32_c(u32 *row, int a, int b, const OvRow *c) {
  for (int x = a; x <= b; x++) {
    float px = x + 0.5f - c->x0, t = (px * c->dx + c->py * c->dy) * c->invL2; t = t < 0 ? 0 : t > 1 ? 1 : t;
    float ex = px - c->dx * t, ey = c->py - c->dy * t, d2 = ex * ex + ey * ey;
    if (d2 >= c->rout2) continue;
    float cv = d2 <= c->rin2 ? 1 : clampf(c->R - msqrt(d2) + 0.5f, 0, 1); int w = mlrint(cv * 256); if (w <= 0) continue;
    int r = (c->r * w) >> 16, g = (c->g * w) >> 16, bb = (c->b * w) >> 16;
    row[x] = sat_add8(row[x], ((u32)(r > 255 ? 255 : r) << 16) | ((u32)(g > 255 ? 255 : g) << 8) | (u32)(bb > 255 ? 255 : bb));
  }
}
static void rs_capsule_b(const RBand *rb, float x0, float y0, float x1, float y1, float R, int cr, int cg, int cb, int soft) {
  if (R <= 0.05f) return;
  float pix = XS > YS ? XS : YS, ext = R + pix;
  float ymin = (y0 < y1 ? y0 : y1) - ext, ymax = (y0 > y1 ? y0 : y1) + ext;
  if (ymax < 0 || ymin > RH * YS) return;
  int j0 = clampi(mfloori(ymin * iYS), 0, RH - 1), j1 = clampi(mfloori(ymax * iYS), 0, RH - 1);
  if (j0 < rb->y0) j0 = rb->y0; if (j1 > rb->y1 - 1) j1 = rb->y1 - 1; if (j0 > j1) return;
  CapG g; cap_geom(&g, x0, y0, x1, y1);
  float L2 = g.dx * g.dx + g.dy * g.dy, invL2 = L2 > 1e-6f ? 1 / L2 : 0, invpix = 1 / pix, invR = 1 / R;
  float rin = R - pix * 0.5f, rin2 = rin > 0 ? rin * rin : 0, rout = R + pix * 0.5f, rout2 = rout * rout, R2 = R * R;
  int fr = cr > 1023 ? 1023 : cr, fg = cg > 1023 ? 1023 : cg, fb = cb > 1023 ? 1023 : cb; u32 full = PK(fr, fg, fb);
  if (!rb->sub && simdLevel) {   /* small capsules (bullets, dots, short strokes): scan the bounding box with the vector kernel */
    int bx0 = clampi(mfloori(((x0 < x1 ? x0 : x1) - ext) * iXS), 0, RW - 1), bx1 = clampi(mfloori(((x0 > x1 ? x0 : x1) + ext) * iXS), 0, RW - 1);
    if (bx1 - bx0 <= 40) {
      CapRow cw; cw.XS = XS; cw.x0 = x0; cw.dx = g.dx; cw.dy = g.dy; cw.invL2 = invL2; cw.R = R; cw.R2 = R2; cw.invR = invR; cw.invpix = invpix; cw.rin2 = rin2; cw.rout2 = rout2; cw.cr = cr; cw.cg = cg; cw.cb = cb; cw.soft = soft;
      for (int j = j0; j <= j1; j++) { cw.py = (j + 0.5f) * YS - y0; K.caprow(scene + j * RW, bx0, bx1, &cw); }
      return;
    }
  }
  for (int j = j0; j <= j1; j++) {
    float Y = (j + 0.5f) * YS, ol, orr, il = 1, ir = 0;
    if (!cap_span(&g, Y, soft ? R : rout, &ol, &orr)) continue;
    int i0 = clampi(mfloori(ol * iXS - 0.5f), 0, RW - 1), i1 = clampi(mfloori(orr * iXS - 0.5f) + 1, 0, RW - 1);
    int k0 = 1, k1 = 0;   /* pixels whose centres lie inside the inner span get full coverage */
    if (!soft && rin > 0 && cap_span(&g, Y, rin, &il, &ir)) { k0 = mfloori(il * iXS - 0.5f) + 1; k1 = mfloori(ir * iXS - 0.5f); if (k0 < i0) k0 = i0; if (k1 > i1) k1 = i1; }
    u32 *row = scene + j * RW; float py = Y - y0;
    if (!rb->sub) {   /* additive (all but the dark batch): edge rows through the kernel table, interior as a span */
      CapRow cw; cw.XS = XS; cw.x0 = x0; cw.py = py; cw.dx = g.dx; cw.dy = g.dy; cw.invL2 = invL2; cw.R = R; cw.R2 = R2; cw.invR = invR; cw.invpix = invpix; cw.rin2 = rin2; cw.rout2 = rout2; cw.cr = cr; cw.cg = cg; cw.cb = cb; cw.soft = soft;
      if (k0 <= k1) { if (k0 > i0) K.caprow(row, i0, k0 - 1, &cw); K.padd_span(row + k0, k1 - k0 + 1, full); if (k1 < i1) K.caprow(row, k1 + 1, i1, &cw); }
      else K.caprow(row, i0, i1, &cw);
      continue;
    }
    for (int i = i0; i <= i1; i++) {
      if (i == k0 && k0 <= k1) {
        if (rb->sub) { for (; i <= k1; i++) row[i] = psub(row[i], fr, fg, fb); }
        else { K.padd_span(row + i, k1 - i + 1, full); i = k1 + 1; }
        i = k1; continue;
      }
      float d2 = cap_dist2(&g, (i + 0.5f) * XS - x0, py, invL2); int w;
      if (soft) { if (d2 >= R2) continue; w = mlrint((1 - msqrt(d2) * invR) * 256); }
      else { if (d2 >= rout2) continue; if (d2 <= rin2) w = 256; else { float c = (R - msqrt(d2)) * invpix + 0.5f; w = mlrint(clampf(c, 0, 1) * 256); } }
      if (w <= 0) continue;
      int r = (cr * w) >> 8, gg = (cg * w) >> 8, b = (cb * w) >> 8; if (r > 1023) r = 1023; if (gg > 1023) gg = 1023; if (b > 1023) b = 1023;
      row[i] = rb->sub ? psub(row[i], r, gg, b) : padd(row[i], PK(r, gg, b));
    }
  }
}
static void rs_capsule(float x0, float y0, float x1, float y1, float R, int cr, int cg, int cb, int soft) { rs_capsule_b(&RB_ALL, x0, y0, x1, y1, R, cr, cg, cb, soft); }
/** Fill a horizontal span of the scene with a value (background). */
INL void fill32(u32 *p, u32 v, int n) {
#if defined(__i386__)
  int d0, d1; __asm__ volatile("cld\n\trep stosl" : "=&D"(d0), "=&c"(d1) : "0"(p), "1"(n), "a"(v) : "memory");
#else
  for (int i = 0; i < n; i++) p[i] = v;
#endif
}
INL void span_set(u32 *row, int a, int b, u32 v) { if (a < 0) a = 0; if (b > RW) b = RW; if (b > a) fill32(row + a, v, b - a); }
INL u32 pack_c(C3 c) { int r = mlrint(c.r * 256), g = mlrint(c.g * 256), b = mlrint(c.b * 256); return PK(clampi(r, 0, 1023), clampi(g, 0, 1023), clampi(b, 0, 1023)); }

/* ---------------- line batches (3D segments, world units) ---------------- */
typedef struct { float x0, y0, z0, x1, y1, z1, w, halo; C3 c; } Seg;
typedef struct { Seg *s; int n, cap; float z; } Batch;
static Batch Bdark, Bworld, Bfloor, Bhud;
static void bseg(Batch *b, float x0, float y0, float z0, float x1, float y1, float z1, float w, C3 c, float halo) {
  if (b->n >= b->cap) return;
  Seg *s = &b->s[b->n++]; s->x0 = x0; s->y0 = y0; s->z0 = z0; s->x1 = x1; s->y1 = y1; s->z1 = z1; s->w = w; s->halo = halo; s->c = c;
}
INL void bline(Batch *b, float x0, float y0, float x1, float y1, float w, C3 c) { bseg(b, x0, y0, b->z, x1, y1, b->z, w, c, 0); }
INL void blineh(Batch *b, float x0, float y0, float x1, float y1, float w, C3 c, float h) { bseg(b, x0, y0, b->z, x1, y1, b->z, w, c, h); }
INL void bdot(Batch *b, float x, float y, float w, C3 c) { bseg(b, x, y, b->z, x, y, b->z, w, c, 0); }
INL void bdoth(Batch *b, float x, float y, float w, C3 c, float h) { bseg(b, x, y, b->z, x, y, b->z, w, c, h); }
static void barc(Batch *b, float x, float y, float rad, float a0, float a1, int seg, float w, C3 c) {
  float px = x + mcos(a0) * rad, py = y + msin(a0) * rad;
  for (int i = 1; i <= seg; i++) { float a = a0 + (a1 - a0) * i / seg, nx = x + mcos(a) * rad, ny = y + msin(a) * rad; bline(b, px, py, nx, ny, w, c); px = nx; py = ny; }
}
INL void bcircle(Batch *b, float x, float y, float rad, int seg, float w, C3 c, float a0) { barc(b, x, y, rad, a0, a0 + TAU, seg, w, c); }
/** pts: flat x,y pairs in local space */
static void bpoly(Batch *b, const float *pts, int n, float x, float y, float ang, float sc, float w, C3 c, int closed) {
  float cs = mcos(ang) * sc, sn = msin(ang) * sc;
  float px = x + pts[0] * cs - pts[1] * sn, py = y + pts[0] * sn + pts[1] * cs, fx = px, fy = py;
  for (int i = 1; i < n; i++) { float lx = pts[i * 2], ly = pts[i * 2 + 1], nx = x + lx * cs - ly * sn, ny = y + lx * sn + ly * cs; bline(b, px, py, nx, ny, w, c); px = nx; py = ny; }
  if (closed) bline(b, px, py, fx, fy, w, c);
}
INL void bngon(Batch *b, float x, float y, float rad, int n, float ang, float w, C3 c) { bcircle(b, x, y, rad, n, w, c, ang); }

/* ---------------- projection ---------------- */
static float CamPxk = 1;   /* world units per output pixel at depth 1 */
#define NEARW 1.0f
INL int cvt(int v) { return v > 4095 ? 4095 : v < 0 ? 0 : v; }
/** Draw one projected segment. tint scales colour. */
static void draw_seg3(const RBand *rb, const float *vp, float x0, float y0, float z0, float x1, float y1, float z1, float w, float halo, C3 c, float tint) {
  float A[4], B[4];
  m4_apply(vp, x0, y0, z0, A); m4_apply(vp, x1, y1, z1, B);
  if (A[3] < NEARW && B[3] < NEARW) return;
  if (A[3] < NEARW) { float t = (NEARW - A[3]) / (B[3] - A[3]); for (int i = 0; i < 4; i++) A[i] += (B[i] - A[i]) * t; }
  else if (B[3] < NEARW) { float t = (NEARW - B[3]) / (A[3] - B[3]); for (int i = 0; i < 4; i++) B[i] += (A[i] - B[i]) * t; }
  float ax = (A[0] / A[3] + 1) * 0.5f * OW, ay = (1 - A[1] / A[3]) * 0.5f * OH, bx = (B[0] / B[3] + 1) * 0.5f * OW, by = (1 - B[1] / B[3]) * 0.5f * OH;
  float ppu = 2 / ((A[3] + B[3]) * CamPxk), hw = w * ppu;
  { float ext = (hw + halo * 2.2f * ppu + 2) * iYS, ya = ay * iYS, yb = by * iYS; if ((ya < rb->y0 - ext && yb < rb->y0 - ext) || (ya > rb->y1 + ext && yb > rb->y1 + ext)) return; }
  int cr = cvt(mlrint(c.r * tint * 256)), cg = cvt(mlrint(c.g * tint * 256)), cb = cvt(mlrint(c.b * tint * 256));
  if (!(cr | cg | cb)) return;
  if (halo > 0) {
    float Rh = halo * 2.2f * ppu;
    if (Rh >= 1.0f) rs_capsule_b(rb, ax, ay, bx, by, Rh + hw, (int)(cr * 0.3f), (int)(cg * 0.3f), (int)(cb * 0.3f), 1);
    else hw += halo * ppu * 0.5f;
  }
  if (hw <= 0.85f * (XS > YS ? XS : YS)) {   /* thinner than ~1.7 scene pixels: Wu line carrying the energy */
    if (hw < 0.05f) hw = 0.05f;
    float dx = bx - ax, dy = by - ay;
    if (dx * dx + dy * dy < 0.25f) rs_capsule_b(rb, ax, ay, bx, by, hw > 0.5f ? hw : 0.5f, hw < 0.5f ? (int)(cr * hw * 2) : cr, hw < 0.5f ? (int)(cg * hw * 2) : cg, hw < 0.5f ? (int)(cb * hw * 2) : cb, 0);
    else rs_wu_b(rb, ax, ay, bx, by, cr, cg, cb, 2 * hw, 2 * hw);
  } else rs_capsule_b(rb, ax, ay, bx, by, hw, cr, cg, cb, 0);
}
static void draw_batch_b(const RBand *band, const Batch *b, const float *vp, float tint, int sub) {
  RBand rb = *band; rb.sub = sub;
  for (int i = 0; i < b->n; i++) { const Seg *s = &b->s[i]; draw_seg3(&rb, vp, s->x0, s->y0, s->z0, s->x1, s->y1, s->z1, s->w, s->halo, s->c, tint); }
}
INL int project(const float *vp, float x, float y, float z, float *sx, float *sy, float *w) {
  float c[4]; m4_apply(vp, x, y, z, c); *w = c[3];
  if (c[3] < NEARW) return 0;
  *sx = (c[0] / c[3] + 1) * 0.5f * OW; *sy = (1 - c[1] / c[3]) * 0.5f * OH; return 1;
}

static void glow_add(float x, float y, float R, C3 c);
/* ---------------- palette ---------------- */
typedef struct { const char *name; C3 north, south, hot, border, skyTop, skyLow, haze, sunA, sunB, floorLine, floor, mount; } Pal;
#define PALN 12

/* ---------------- noise (shared with the particle flow field) ---------------- */
INL float h21(float x, float y) { x = fractf(x * 123.34f); y = fractf(y * 456.21f); float d = x * (x + 45.32f) + y * (y + 45.32f); x += d; y += d; return fractf(x * y); }
static float vn(float x, float y) {
  float ix = mfloor(x), iy = mfloor(y), fx = x - ix, fy = y - iy, ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
  float a = h21(ix, iy), b = h21(ix + 1, iy), c = h21(ix, iy + 1), d = h21(ix + 1, iy + 1);
  return lerpf(lerpf(a, b, ux), lerpf(c, d, ux), uy);
}

/* ---------------- sky: stars + ridge are built once ---------------- */
#define NSTAR 1500
static struct { float x, y, z, e, r; } stars[NSTAR]; static int nstars;
static void sky_init(void) {
  nstars = 0;
  for (int gy = 3; gy < 48 && nstars < NSTAR; gy++) for (int gx = -252; gx < 252 && nstars < NSTAR; gx++) {
    float r = h21((float)gx, (float)gy); if (r > 0.06f) continue;
    float ox = h21(gx + 3.1f, gy + 3.1f) - 0.5f, oy = h21(gx + 7.7f, gy + 7.7f) - 0.5f;
    float az = (gx + 0.5f + ox * 0.6f) / 80, e = (gy + 0.5f + oy * 0.6f) / 80;
    stars[nstars].x = msin(az) * mcos(e); stars[nstars].y = -mcos(az) * mcos(e); stars[nstars].z = msin(e); stars[nstars].e = e; stars[nstars].r = r; nstars++;
  }
}
static float ridgeM(float az) {
  float side = smoothstep(0.08f, 0.6f, mfabs(az));
  return 0.008f + 0.075f * mpow(vn(az * 3.1f, 1.7f), 1.6f) * (0.2f + 0.8f * side) + 0.02f * vn(az * 13, 4.2f) * side;
}
INL C3 skyCol(const Pal *p, float e) { C3 c = clerp(p->skyLow, p->skyTop, smoothstep(0, 0.42f, e)); float h = mexp(-mfabs(e) * 24) * 0.45f; return c3(c.r + p->haze.r * h, c.g + p->haze.g * h, c.b + p->haze.b * h); }

/* camera access (defined with the camera) */
static float CamVP[16], CamInv[16], CamEye[3];
static void unproject(float nx, float ny, float *ro, float *rd) {
  float a[4], b[4]; m4_apply(CamInv, nx, ny, -1, a); m4_apply(CamInv, nx, ny, 1, b);
  ro[0] = a[0] / a[3]; ro[1] = a[1] / a[3]; ro[2] = a[2] / a[3];
  float dx = b[0] / b[3] - ro[0], dy = b[1] / b[3] - ro[1], dz = b[2] / b[3] - ro[2], l = msqrt(dx * dx + dy * dy + dz * dz);
  rd[0] = dx / l; rd[1] = dy / l; rd[2] = dz / l;
}
/** Screen x (output px) of a direction from the eye, or -1e9. */
static int dir_screen(float dx, float dy, float dz, float *sx, float *sy) {
  float w; return project(CamVP, CamEye[0] + dx * 8000, CamEye[1] + dy * 8000, CamEye[2] + dz * 8000, sx, sy, &w);
}

/** Background: sky gradient, striped sun, stars and wireframe ridge above the horizon; a dark glossy floor below it. */
static float rowE[OHMAX];
/* exp(-x) for x in [0, 16) at 1/256 steps */
static float expTab[4096];
static void exp_init(void) { for (int i = 0; i < 4096; i++) expTab[i] = mexp(-i / 256.f); }
/* sky and floor rows, split into bands across the worker threads */
static u8 rowFloor[OHMAX];
static struct { const Pal *p; C3 red; float dk, pulse; int n; } BGc;
static void bg_rows(int ja, int jb) {
  const Pal *p = BGc.p; C3 red = BGc.red; float dk = BGc.dk, pulse = BGc.pulse, ro[3], rd[3];
  for (int j = ja; j < jb; j++) {
    float ny = 1 - (j + 0.5f) * 2 / RH;
    unproject(0, ny, ro, rd);
    float e = masin(rd[2]); rowE[j] = e;
    u32 *row = scene + j * RW;
    if (rd[2] > -0.0005f) {
      C3 c = skyCol(p, e); if (dk > 0) c = clerp(c, red, dk);
      span_set(row, 0, RW, pack_c(c)); rowFloor[j] = 0; continue;
    }
    rowFloor[j] = 1;
    float t = -ro[2] / rd[2];
    float k = 1 + pulse * 0.6f, fres = mpow(1 - mfabs(rd[2]), 7);
    C3 rf = skyCol(p, -e); C3 c = c3(p->floor.r * k + rf.r * 0.5f * fres * 0.22f, p->floor.g * k + rf.g * 0.5f * fres * 0.22f, p->floor.b * k + rf.b * 0.5f * fres * 0.22f);
    float fog = smoothstep(2500, 14000, t); C3 fc = c3(p->haze.r * 0.6f + p->skyLow.r * 0.3f, p->haze.g * 0.6f + p->skyLow.g * 0.3f, p->haze.b * 0.6f + p->skyLow.b * 0.3f);
    C3 cin = clerp(c, fc, fog), cout = clerp(cmul(c, 0.35f), fc, fog);
    if (dk > 0) { cin = clerp(cin, red, dk); cout = clerp(cout, red, dk); }
    /* where along this row the arena lies */
    float roL[3], rdL[3], roR[3], rdR[3]; unproject(-1, ny, roL, rdL); unproject(1, ny, roR, rdR);
    if (rdL[2] >= -1e-4f || rdR[2] >= -1e-4f) { span_set(row, 0, RW, pack_c(cin)); continue; }
    float tL = -roL[2] / rdL[2], tR = -roR[2] / rdR[2];
    float Lx = roL[0] + rdL[0] * tL, Ly = roL[1] + rdL[1] * tL, Rx = roR[0] + rdR[0] * tR, Ry = roR[1] + rdR[1] * tR;
    float s0 = 0, s1 = 1;
    span_set(row, 0, RW, pack_c(cout));
    if (clip_lb(Lx, Ly, Rx - Lx, Ry - Ly, 0, 0, ARENA_W, ARENA_H, &s0, &s1)) {
      float ax, ay, bx, by, w;
      if (project(CamVP, Lx + (Rx - Lx) * s0, Ly + (Ry - Ly) * s0, 0, &ax, &ay, &w) && project(CamVP, Lx + (Rx - Lx) * s1, Ly + (Ry - Ly) * s1, 0, &bx, &by, &w))
        span_set(row, mlrint(minf(ax, bx) / XS), mlrint(maxf(ax, bx) / XS), pack_c(cin));
    }
  }
}
static void bg_job(int i) { bg_rows(RH * i / BGc.n, RH * (i + 1) / BGc.n); }
static void draw_background(const Pal *p, float time, float pulse, float danger) {
  int anySky = 0, horizon = -1;
  C3 red; { float nz = vn(0.5f * 3 + time * 0.3f, 0.5f * 2 + time * 0.3f); red = c3(0.5f * (0.4f + 0.6f * nz), 0.02f * (0.4f + 0.6f * nz), 0.06f * (0.4f + 0.6f * nz)); }
  BGc.p = p; BGc.red = red; BGc.dk = danger * 0.5f; BGc.pulse = pulse;
  { int n = nWork; if (n > NWMAX) n = NWMAX; if (n > RH / 60) n = RH / 60; if (n < 1) n = 1; BGc.n = n; if (n > 1) plat_par(n, bg_job); else bg_rows(0, RH); }
  for (int j = 0; j < RH; j++) { if (rowFloor[j]) { if (horizon < 0) horizon = j; } else anySky = 1; }
  if (!anySky) return;
  /* a soft column of sunlight on the glossy floor */
  float sx, sy;
  if (horizon > 0 && dir_screen(0, -1, 0, &sx, &sy)) {
    C3 sc = clerp(p->sunA, p->sunB, 0.65f);
    float rpp = horizon + 1 < RH ? mfabs(rowE[horizon] - rowE[horizon + 1]) / YS : 0.002f; if (rpp < 1e-5f) rpp = 0.002f;
    for (int j = horizon; j < RH && j < horizon + 80; j++) {
      float em = -rowE[j], fres = mpow(1 - msin(em), 7), k = mexp(-em * 5) * 0.55f * fres * 0.22f;
      if (k < 0.002f) break;
      float wpx = (3.f / 16) / rpp / XS; int i0 = mlrint(sx / XS - wpx), i1 = mlrint(sx / XS + wpx);
      u32 *row = scene + j * RW;
      for (int i = (i0 < 0 ? 0 : i0); i <= i1 && i < RW; i++) { float az = mfabs((i + 0.5f) * XS - sx) * rpp; float f = expTab[clampi(mlrint(az * 16 * 256), 0, 4095)] * k; row[i] = padd(row[i], pack_c(cmul(sc, f))); }
    }
  }
  /* stars */
  for (int i = 0; i < nstars; i++) {
    float x, y, w; if (!project(CamVP, CamEye[0] + stars[i].x * 8000, CamEye[1] + stars[i].y * 8000, CamEye[2] + stars[i].z * 8000, &x, &y, &w)) continue;
    if (x < 0 || y < 0 || x >= OW || y >= OH) continue;
    float r = stars[i].r, tw = 0.55f + 0.45f * msin(time * (1 + r * 30) + r * 60), k = 0.8f * tw * smoothstep(0.04f, 0.3f, stars[i].e) * 0.6f;
    int px = (int)(x / XS), py = (int)(y / YS);
    plot(px, py, (int)(0.85f * k * 256), (int)(0.85f * k * 256), (int)(k * 256), 256);
  }
  /* striped retro sun, due north */
  if (dir_screen(0, -mcos(0.1f), msin(0.1f), &sx, &sy)) {
    const float R = 0.16f;
    C3 g = clerp(p->sunB, p->sunA, 0.4f);
    float rpp = 0.87f / OH; /* radians per output pixel, refined below */
    { float ax, ay; if (dir_screen(0, -mcos(0.1f + 0.05f), msin(0.1f + 0.05f), &ax, &ay)) rpp = 0.05f / maxf(1, mfabs(ay - sy)); }
    float Rpx = R / rpp;
    glow_add(sx, sy, Rpx * 2.6f, cmul(g, 0.35f));
    int j0 = clampi((int)((sy - Rpx) / YS) - 1, 0, RH - 1), j1 = clampi((int)((sy + Rpx) / YS) + 1, 0, RH - 1);
    for (int j = j0; j <= j1; j++) {
      float e = rowE[j], de = e - 0.1f; if (de * de >= R * R) continue;
      float hw = msqrt(R * R - de * de) / rpp;
      float k = clampf((0.1f + R - e) / (2 * R), 0, 1), gap = smoothstep(0.45f, 1, k) * 0.6f, band = fractf(e * 52 + time * 0.3f);
      if (k > 0.45f && band < gap) continue;
      u32 v = pack_c(cmul(clerp(p->sunA, p->sunB, k), 1.35f));
      span_set(scene + j * RW, mlrint((sx - hw) / XS), mlrint((sx + hw) / XS), v);
    }
  }
  /* wireframe ridge line on the horizon */
  {
    float roA[3], rdA[3], roB[3], rdB[3];
    unproject(-1, 0, roA, rdA); unproject(1, 0, roB, rdB);
    float azL = matan2(rdA[0], -rdA[1]) - 0.05f, azR = matan2(rdB[0], -rdB[1]) + 0.05f;
    if (azR < azL) azR += TAU;
    const int N = 120; float px[121], py[121], bx[121], by[121]; int ok[121];
    C3 mc = cmul(p->mount, 0.3f), lc = p->floorLine;
    for (int i = 0; i <= N; i++) {
      float az = azL + (azR - azL) * i / N, m = ridgeM(az), w;
      ok[i] = dir_screen(msin(az) * mcos(m), -mcos(az) * mcos(m), msin(m), &px[i], &py[i]) && dir_screen(msin(az), -mcos(az), msin(-0.02f), &bx[i], &by[i]);
      (void)w;
    }
    /* fill the mountains column by column */
    for (int i = 0; i < N; i++) {
      if (!ok[i] || !ok[i + 1]) continue;
      int c0 = clampi(mlrint(px[i] / XS), 0, RW), c1 = clampi(mlrint(px[i + 1] / XS), 0, RW);
      u32 v = pack_c(mc);
      for (int c = c0; c < c1; c++) {
        float f = (c * XS - px[i]) / maxf(1e-3f, px[i + 1] - px[i]), top = lerpf(py[i], py[i + 1], f), bot = lerpf(by[i], by[i + 1], f);
        int r0 = clampi(mlrint(top / YS), 0, RH), r1 = clampi(mlrint(bot / YS), 0, RH);
        for (int r = r0; r < r1; r++) scene[r * RW + c] = v;
      }
    }
    /* contour lines, vertical ribs and the bright ridge edge */
    int cr = (int)(lc.r * 256), cg = (int)(lc.g * 256), cb = (int)(lc.b * 256);
    for (int i = 0; i < N; i++) {
      if (!ok[i] || !ok[i + 1]) continue;
      rs_wu(px[i], py[i], px[i + 1], py[i + 1], (int)(cr * 0.9f), (int)(cg * 0.9f), (int)(cb * 0.9f), 1.1f, 1.1f);
      float az0 = azL + (azR - azL) * i / N, az1 = azL + (azR - azL) * (i + 1) / N, m0 = ridgeM(az0), m1 = ridgeM(az1);
      for (int k = 1; k < 10; k++) {
        float e0 = m0 - k / 90.f, e1 = m1 - k / 90.f; if (e0 < -0.02f || e1 < -0.02f) continue;
        float a0 = 0.3f * (1 - (k / 90.f) / maxf(m0, 1e-3f)), x0, y0, x1, y1; if (a0 <= 0.01f) continue;
        if (dir_screen(msin(az0) * mcos(e0), -mcos(az0) * mcos(e0), msin(e0), &x0, &y0) && dir_screen(msin(az1) * mcos(e1), -mcos(az1) * mcos(e1), msin(e1), &x1, &y1))
          rs_wu(x0, y0, x1, y1, (int)(cr * a0), (int)(cg * a0), (int)(cb * a0), 0.8f, 0.8f);
      }
      if (mfloori(az0 * 30) != mfloori(az1 * 30)) rs_wu(bx[i], by[i], px[i], py[i], (int)(cr * 0.3f), (int)(cg * 0.3f), (int)(cb * 0.3f), 0, 0.8f);
    }
  }
}

/* ---------------- grid (drawn from the node texture, GPU-style) ---------------- */
static float *gridData; static int gridCols, gridRows; static u16 *gridIdx; static int gridN;
static float *gpx, *gpy, *gpw;
static void draw_grid_project(const float *vp) {
  int N = gridCols * gridRows;
  for (int i = 0; i < N; i++) { const float *d = gridData + i * 4; if (!project(vp, d[0], d[1], d[2], &gpx[i], &gpy[i], &gpw[i])) gpw[i] = -1; }
}
static void draw_grid_lines(const RBand *rb, float pulse, float bright, C3 north, C3 south, C3 hot) {
  float ylo = (rb->y0 - 2) * YS, yhi = (rb->y1 + 2) * YS;
  for (int s = 0; s < gridN; s++) {
    int a = gridIdx[s * 2], b = gridIdx[s * 2 + 1] & 0x7FFF, maj = gridIdx[s * 2 + 1] >> 15;
    if (gpw[a] < 0 || gpw[b] < 0) continue;
    if ((gpy[a] < ylo && gpy[b] < ylo) || (gpy[a] > yhi && gpy[b] > yhi)) continue;
    const float *A = gridData + a * 4, *B = gridData + b * 4;
    float e = (A[3] + B[3]) * 0.5f;
    float k = (maj ? 0.15f : 0.055f) * bright * (1 + pulse * (maj ? 0.9f : 0.55f)) + e * (maj ? 0.42f : 0.2f);
    C3 base = clerp(north, south, clampf((A[1] + B[1]) * 0.5f / ARENA_H, 0, 1)), c = cmul(clerp(base, hot, minf(1, e * 1.4f)), k);
    float hw = (maj ? 1.3f : 0.7f) * 2 / ((gpw[a] + gpw[b]) * CamPxk);
    rs_wu_b(rb, gpx[a], gpy[a], gpx[b], gpy[b], mlrint(c.r * 256), mlrint(c.g * 256), mlrint(c.b * 256), 2 * hw, 2 * hw);
  }
}

/* ---------------- bloom + composite ---------------- */
/* The bloom works on texels of bT×bT output pixels (bT = 4, 8 or 16, so the bloom grid stays about
   120-150 texels tall at any output size; doubled on the low tiers), plus a coarse level at 1/4 of that. */
#define BWMAX 512
#define BHMAX 160
static int bT = 4, bLg = 2, bw = 160, bh = 120, cw = 40, chh = 30, bLow;
static int blA[3][BWMAX * BHMAX], blC[3][(BWMAX / 4) * (BHMAX / 4)];
struct BRow { int r; u32 gb; };                    /* one output row of bloom at 2-pixel steps: R, and G<<11|B */
static BRow bloomRows[NWMAX][OWMAX / 2 + 4];       /* one per composite band */
static u16 LUT[6][3][2048];        /* 6 brightness levels (vignette / scanline), RGB565 fields */
static u32 LUT32[6][3][2048];      /* the same as XRGB8888 fields */
static const float LUTM[6] = { 1.0f, 0.9f, 0.8f, 0.7f, 0.62f, 0.55f };
static float curveTab[3][4097];    /* display curve per channel for linear 0..8 */
static struct { float gain, flash; C3 flashCol, tint; int valid, fmt; } lutKey;
static void curve_init(void) {
  const float sh[3] = { 0.5f, 0.12f, 0.95f }, hi[3] = { 1.0f, 0.5f, 0.65f };
  for (int ch = 0; ch < 3; ch++) for (int i = 0; i <= 4096; i++) {
    float x = i / 512.f * 1.08f, a = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
    a = clampf(a, 0, 1);
    a += sh[ch] * (1 - smoothstep(0, 0.35f, a)) * 0.08f * smoothstep(0, 0.04f, a) + hi[ch] * smoothstep(0.55f, 1, a) * 0.06f;
    a = clampf(a, 0, 1);
    curveTab[ch][i] = mpow(a, 0.88f);
  }
}
static void lut_build(float gain, float flash, C3 fcol, C3 tint) {
  if (lutKey.valid && lutKey.fmt == fmt32 && lutKey.gain == gain && mfabs(lutKey.flash - flash) < 0.01f && !memcmp(&lutKey.flashCol, &fcol, sizeof fcol) && !memcmp(&lutKey.tint, &tint, sizeof tint)) return;
  lutKey.valid = 1; lutKey.fmt = fmt32; lutKey.gain = gain; lutKey.flash = flash; lutKey.flashCol = fcol; lutKey.tint = tint;
  const float add[3] = { fcol.r * flash, fcol.g * flash, fcol.b * flash }, tn[3] = { tint.r, tint.g, tint.b };
  const int bits[3] = { 31, 63, 31 }, sh[3] = { 11, 5, 0 }, sh32[3] = { 16, 8, 0 };
  for (int ch = 0; ch < 3; ch++) {
    int mx = fmt32 ? 255 : bits[ch];
    for (int i = 0; i < 2048; i++) {
      float v = i / 256.f * gain + add[ch]; int ix = mlrint(v * 512); if (ix > 4096) ix = 4096; if (ix < 0) ix = 0;
      float c = curveTab[ch][ix];
      for (int m = 0; m < 6; m++) {
        float o = c * LUTM[m] + tn[ch]; int q = mlrint(o * mx); if (q > mx) q = mx; if (q < 0) q = 0;
        if (fmt32) LUT32[m][ch][i] = (u32)q << sh32[ch]; else LUT[m][ch][i] = (u16)(q << sh[ch]);
      }
    }
  }
}
/* Vignette and CRT scanlines: per output row, runs of LUT level. [y][0] plain, [y][1] with the row's
   scanline factor (the web build's sin(y·0.66π) pattern, ~3 px period at any resolution). */
typedef struct { u8 n; u16 end[14]; u8 lvl[14]; } VRun;
static VRun *vrun;                 /* OH × 2 */
static u8 *scanCls;                /* per output row: 0 full, 1 ×0.9, 2 ×0.8 (for line-doubled rows) */
static float scanF(int y) { return 1 - 0.2f * (0.5f + 0.5f * msin(y * PI * 0.66f)); }
static u8 vlev[OWMAX / 2];
static void vignette_init(void) {
  float a = (float)OW / OH, th[5]; for (int m = 0; m < 5; m++) th[m] = (LUTM[m] + LUTM[m + 1]) * 0.5f;
  for (int y = 0; y < OH; y++) {
    float s = scanF(y); scanCls[y] = (u8)(s > 0.95f ? 0 : s > 0.85f ? 1 : 2);
    for (int dim = 0; dim < 2; dim++) {
      float cy = (y + 0.5f) / OH - 0.5f, k = dim ? s : 1;
      for (int x = 0; x < OW / 2; x++) {   /* symmetric: the left half, mirrored */
        float cx = (x + 0.5f) / OW - 0.5f, l = msqrt(cx * a * 0.8f * cx * a * 0.8f + cy * cy) * 1.25f, f = (1 - 0.5f * smoothstep(0.35f, 1.25f, l)) * k;
        int m = 0; while (m < 5 && f < th[m]) m++; vlev[x] = (u8)m;
      }
      VRun *v = &vrun[y * 2 + dim]; int n = 0, last = -1;
      for (int x = 0; x < OW; x++) {
        int m = vlev[x < OW / 2 ? x : OW - 1 - x];
        if (m != last && n < 14) { if (n) v->end[n - 1] = (u16)(x & ~3); v->lvl[n] = (u8)m; n++; last = m; }
      }
      v->end[n - 1] = (u16)OW; v->n = (u8)n;
    }
  }
}
/* chromatic aberration direction × radial strength per bloom texel (output px ×64) */
static int abX[BWMAX * BHMAX], abY[BWMAX * BHMAX];
/* Blur: two passes of an edge-clamped radius-2 box (≈ gaussian σ 2) on all three channels at once,
   packed 21 bits each in a u64 (R<<42 | G<<21 | B). Inputs are ≤ 2047, so the unnormalised
   sum (×625) still fits a field. The vertical pass walks rows in memory order with running
   column sums, keeping the three most recent original rows in a ring. */
#define F21 0x1FFFFFull
static int bthr[1024]; static float bthrStr = -1;
INL u64 pk64(int r, int g, int b) { return ((u64)r << 42) | ((u64)g << 21) | (u64)b; }
static u64 bl64[BWMAX * BHMAX], c64[(BWMAX / 4) * (BHMAX / 4)], bl_cs[BWMAX], bl_ring[3][BWMAX];
static u64 bl_padT[NWMAX][BWMAX + 8];
/* horizontal pass over rows [y0, y1) (band scratch: its own padded row) */
static void blur64_hr(u64 *a, int w, int y0, int y1, u64 *pad) {
  u64 *p = pad + 2;
  for (int y = y0; y < y1; y++) {
    u64 *row = a + y * w;
    p[-2] = p[-1] = row[0]; memcpy(p, row, w * 8); p[w] = p[w + 1] = p[w + 2] = row[w - 1];
    u64 sum = p[-2] + p[-1] + p[0] + p[1] + p[2];
    for (int x = 0; x < w; x++) { row[x] = sum; sum += p[x + 3] - p[x - 2]; }
  }
}
/* vertical pass over columns [x0, x1): running column sums, the three latest original rows in a ring */
static void blur64_vc(u64 *a, int w, int h, int x0, int x1) {
  u64 *cs = bl_cs; int n = x1 - x0;
  for (int x = x0; x < x1; x++) cs[x] = a[x] * 3 + a[w + x] + a[2 * w + x];
  for (int y = 0; y < h; y++) {
    u64 *row = a + y * w; memcpy(bl_ring[y % 3] + x0, row + x0, n * 8);
    const u64 *sb = bl_ring[(y >= 2 ? y - 2 : 0) % 3], *ad = a + (y + 3 < h ? y + 3 : h - 1) * w;
    if (y + 1 < h) for (int x = x0; x < x1; x++) { u64 v = cs[x]; row[x] = v; cs[x] = v + ad[x] - sb[x]; }
    else memcpy(row + x0, cs + x0, n * 8);
  }
}
static void blur64_h(u64 *a, int w, int h) { blur64_hr(a, w, 0, h, bl_padT[0]); }
static void blur64_v(u64 *a, int w, int h) { blur64_vc(a, w, h, 0, w); }
static void blur64(u64 *a, int w, int h) { blur64_h(a, w, h); blur64_v(a, w, h); blur64_h(a, w, h); blur64_v(a, w, h); }
#define UNPK(v, sh) ((int)((((u32)((v) >> (sh)) & 0x1FFFFF) * 105u) >> 16))   /* field / 625 */
/* soft glows (sun, large halos) are added straight into the bloom buffer */
static struct { float x, y, R; C3 c; } glows[8]; static int nglow;
static void glow_add(float x, float y, float R, C3 c) { if (nglow < 8) { glows[nglow].x = x; glows[nglow].y = y; glows[nglow].R = R; glows[nglow].c = c; nglow++; } }
static int soff[8], nsamp = 8;     /* downsample: 4 samples per texel (2×2 lattice) on small texels, else 8 (checkerboard over 4×4) */
static u8 fold_x0[BWMAX], fold_x1[BWMAX], fold_fu[BWMAX];
/** Bloom grid, aberration profile and sampling for the current output size and tier. */
static void render_config(void) {
  int T = 4; while ((OH / T > 150 || OW / T > BWMAX) && T < 64) T <<= 1;
  if (bLow) T <<= 1;
  bT = T; bLg = 0; while ((1 << bLg) < T) bLg++;
  bw = OW / T; bh = OH / T; if (bh > BHMAX) bh = BHMAX; if (bw > BWMAX) bw = BWMAX;
  cw = bw / 4; chh = bh / 4; if (cw < 3) cw = 3; if (chh < 3) chh = 3;
  int fx = T >> rXs, fy = T >> rYs, k = 0;
  nsamp = bLow || fx * fy >= 64 ? 8 : 4;
  if (nsamp == 8) { for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) if (!((i + j) & 1)) soff[k++] = (j * fy / 4) * RW + i * fx / 4; }
  else { soff[0] = 0; soff[1] = fx >> 1; soff[2] = (fy >> 1) * RW; soff[3] = soff[2] + (fx >> 1); }
  for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) {
    float vx = (bx * T + T * 0.5f) / OW - 0.5f, vy = (by * T + T * 0.5f) / OH - 0.5f, s = 0.15f + msqrt(vx * vx + vy * vy) * 1.8f;
    abX[by * bw + bx] = mlrint(vx * s * OW * 64); abY[by * bw + bx] = mlrint(vy * s * OH * 64);
  }
  bthrStr = -1;
}
/* bloom phases: rows (downsample + horizontal blur), columns (vertical blur), rows, columns (+ unpack), rows (fold);
   each runs on all worker threads at HD sizes */
static struct { int n, ph; } BL;
static void bloom_down(int ya, int yb) {
  int fx = bT >> rXs, fy = bT >> rYs;
  const int o0 = soff[0], o1 = soff[1], o2 = soff[2], o3 = soff[3], o4 = soff[4], o5 = soff[5], o6 = soff[6], o7 = soff[7];
  if (nsamp == 4) for (int y = ya; y < yb; y++) {
    const u32 *p = scene + (y * fy) * RW;
    u64 *o = bl64 + y * bw;
    for (int x = 0; x < bw; x++, p += fx) {
      u32 a = p[o0], b = p[o1], c = p[o2], d = p[o3];
      int r = ((a >> 22) + (b >> 22) + (c >> 22) + (d >> 22)) >> 2, g = (((a >> 11) & 1023) + ((b >> 11) & 1023) + ((c >> 11) & 1023) + ((d >> 11) & 1023)) >> 2, bb = ((a & 1023) + (b & 1023) + (c & 1023) + (d & 1023)) >> 2;
      int br = r > g ? r : g; if (bb > br) br = bb; int f = bthr[br];
      if (!f) { o[x] = 0; continue; }
      r = (r * f) >> 8; g = (g * f) >> 8; bb = (bb * f) >> 8;
      o[x] = pk64(r > 2047 ? 2047 : r, g > 2047 ? 2047 : g, bb > 2047 ? 2047 : bb);
    }
  }
  else for (int y = ya; y < yb; y++) {
    const u32 *p = scene + (y * fy) * RW;
    u64 *o = bl64 + y * bw;
    for (int x = 0; x < bw; x++, p += fx) {
      u32 a = p[o0], b = p[o1], c = p[o2], d = p[o3], e = p[o4], f2 = p[o5], g2 = p[o6], h = p[o7];
      int r = ((a >> 22) + (b >> 22) + (c >> 22) + (d >> 22) + (e >> 22) + (f2 >> 22) + (g2 >> 22) + (h >> 22)) >> 3;
      int g = (((a >> 11) & 1023) + ((b >> 11) & 1023) + ((c >> 11) & 1023) + ((d >> 11) & 1023) + ((e >> 11) & 1023) + ((f2 >> 11) & 1023) + ((g2 >> 11) & 1023) + ((h >> 11) & 1023)) >> 3;
      int bb = ((a & 1023) + (b & 1023) + (c & 1023) + (d & 1023) + (e & 1023) + (f2 & 1023) + (g2 & 1023) + (h & 1023)) >> 3;
      int br = r > g ? r : g; if (bb > br) br = bb; int f = bthr[br];
      if (!f) { o[x] = 0; continue; }
      r = (r * f) >> 8; g = (g * f) >> 8; bb = (bb * f) >> 8;
      o[x] = pk64(r > 2047 ? 2047 : r, g > 2047 ? 2047 : g, bb > 2047 ? 2047 : bb);
    }
  }
}
static void bloom_fold(int ya, int yb) {
  int *CR = blC[0], *CG = blC[1], *CB = blC[2], *AR = blA[0], *AG = blA[1], *AB = blA[2];
  int vr[BWMAX / 4], vg[BWMAX / 4], vb[BWMAX / 4];
  for (int y = ya; y < yb; y++) {
    int v = ((y * 2 + 1) << 5) - 128, y0 = v >> 8, fv = v & 255; if (y0 < 0) { y0 = 0; fv = 0; } if (y0 > chh - 1) y0 = chh - 1; int y1 = y0 < chh - 1 ? y0 + 1 : chh - 1;
    int q0 = y0 * cw, q1 = y1 * cw;
    for (int x = 0; x < cw; x++) {
      vr[x] = CR[q0 + x] + (((CR[q1 + x] - CR[q0 + x]) * fv) >> 8);
      vg[x] = CG[q0 + x] + (((CG[q1 + x] - CG[q0 + x]) * fv) >> 8);
      vb[x] = CB[q0 + x] + (((CB[q1 + x] - CB[q0 + x]) * fv) >> 8);
    }
    int *ar = AR + y * bw, *ag = AG + y * bw, *ab = AB + y * bw;
    for (int x = 0; x < bw; x++) {
      int a0 = fold_x0[x], a1 = fold_x1[x], fu = fold_fu[x], t, o;
      t = vr[a0] + (((vr[a1] - vr[a0]) * fu) >> 8); o = (ar[x] * 210 + t * 400) >> 8; ar[x] = o > 1023 ? 1023 : o;
      t = vg[a0] + (((vg[a1] - vg[a0]) * fu) >> 8); o = (ag[x] * 210 + t * 400) >> 8; ag[x] = o > 1023 ? 1023 : o;
      t = vb[a0] + (((vb[a1] - vb[a0]) * fu) >> 8); o = (ab[x] * 210 + t * 400) >> 8; ab[x] = o > 1023 ? 1023 : o;
    }
  }
}
static void bloom_unpack(int x0, int x1) {
  int *AR = blA[0], *AG = blA[1], *AB = blA[2];
  for (int y = 0; y < bh; y++) for (int x = x0; x < x1; x++) { int i = y * bw + x; u64 v = bl64[i]; AR[i] = UNPK(v, 42); AG[i] = UNPK(v, 21); AB[i] = UNPK(v, 0); }
}
static void bl_job(int i) {
  int ya = bh * i / BL.n, yb = bh * (i + 1) / BL.n, xa = bw * i / BL.n, xb = bw * (i + 1) / BL.n;
  switch (BL.ph) {
    case 0: bloom_down(ya, yb); blur64_hr(bl64, bw, ya, yb, bl_padT[i]); break;
    case 1: blur64_vc(bl64, bw, bh, xa, xb); break;
    case 2: blur64_hr(bl64, bw, ya, yb, bl_padT[i]); break;
    case 3: blur64_vc(bl64, bw, bh, xa, xb); bloom_unpack(xa, xb); break;
    default: bloom_fold(ya, yb); break;
  }
}
static void bloom_build(float strength) {
  if (strength != bthrStr) {
    bthrStr = strength; const float th = 0.7f * 256, knee = th * 0.6f;
    for (int br = 0; br < 1024; br++) { float soft = clampf(br - th + knee, 0, 2 * knee); soft = soft * soft / (4 * knee + 1e-4f); bthr[br] = mlrint(maxf(soft, br - th) / maxf((float)br, 1) * strength * (bLow ? 1.2f : 1.f) * 256); }
    for (int x = 0; x < bw; x++) { int u = ((x * 2 + 1) << 5) - 128, x0 = u >> 8, fu = u & 255; if (x0 < 0) { x0 = 0; fu = 0; } if (x0 > cw - 1) x0 = cw - 1; fold_x0[x] = (u8)x0; fold_x1[x] = (u8)(x0 < cw - 1 ? x0 + 1 : cw - 1); fold_fu[x] = (u8)fu; }
  }
  int np = nWork; if (np > NWMAX) np = NWMAX; if (bw * bh < 6000 || np > bh / 8) np = bw * bh < 6000 ? 1 : bh / 8; if (np < 1) np = 1;
  BL.n = np;
  if (np > 1) { for (BL.ph = 0; BL.ph < 4; BL.ph++) plat_par(np, bl_job); }
  else { bloom_down(0, bh); blur64_hr(bl64, bw, 0, bh, bl_padT[0]); blur64_vc(bl64, bw, bh, 0, bw); blur64_hr(bl64, bw, 0, bh, bl_padT[0]); blur64_vc(bl64, bw, bh, 0, bw); bloom_unpack(0, bw); }
  int n = bw * bh, cn = cw * chh;
  (void)n;
  int *AR = blA[0], *AG = blA[1], *AB = blA[2];
  for (int y = 0; y < chh; y++) for (int x = 0; x < cw; x++) {
    int yy = y * 4 + 1, xx = x * 4 + 1; if (yy + 2 >= bh) yy = bh - 3; if (xx + 2 >= bw) xx = bw - 3;
    int i = yy * bw + xx, j = i + 2, k = i + bw * 2, l = k + 2;
    c64[y * cw + x] = pk64((AR[i] + AR[j] + AR[k] + AR[l]) >> 2, (AG[i] + AG[j] + AG[k] + AG[l]) >> 2, (AB[i] + AB[j] + AB[k] + AB[l]) >> 2);
  }
  blur64(c64, cw, chh);
  int *CR = blC[0], *CG = blC[1], *CB = blC[2];
  for (int i = 0; i < cn; i++) { u64 v = c64[i]; CR[i] = UNPK(v, 42); CG[i] = UNPK(v, 21); CB[i] = UNPK(v, 0); }
  /* fold the coarse level back in (bilinear, fixed point), clamped to the field range */
  if (np > 1) { BL.ph = 4; plat_par(np, bl_job); } else bloom_fold(0, bh);
  float gs = 1.f / bT;
  for (int k = 0; k < nglow; k++) {
    float gx = glows[k].x * gs, gy = glows[k].y * gs, R = glows[k].R * gs, iR = 1 / R; int cr = mlrint(glows[k].c.r * 256), cg = mlrint(glows[k].c.g * 256), cb = mlrint(glows[k].c.b * 256);
    int y0 = clampi(mfloori(gy - R), 0, bh - 1), y1 = clampi(mfloori(gy + R) + 1, 0, bh - 1), x0 = clampi(mfloori(gx - R), 0, bw - 1), x1 = clampi(mfloori(gx + R) + 1, 0, bw - 1);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
      float dx = x + 0.5f - gx, dy = y + 0.5f - gy, d2 = dx * dx + dy * dy; if (d2 >= R * R) continue;
      int w = mlrint((1 - msqrt(d2) * iR) * 256), i = y * bw + x; w = (w * w) >> 8;
      int r = AR[i] + ((cr * w) >> 8), g = AG[i] + ((cg * w) >> 8), b = AB[i] + ((cb * w) >> 8);
      AR[i] = r > 1023 ? 1023 : r; AG[i] = g > 1023 ? 1023 : g; AB[i] = b > 1023 ? 1023 : b;
    }
  }
  nglow = 0;
}
/* one output row of bloom at 2-pixel steps, bilinear from the bloom grid (values are already within 0..1023) */
static void bloom_row(int y, BRow *br) {
  int v = ((y * 2 + 1) << (7 - bLg)) - 128, y0 = v >> 8, fv = v & 255; if (y0 < 0) { y0 = 0; fv = 0; } if (y0 > bh - 1) y0 = bh - 1; int y1 = y0 < bh - 1 ? y0 + 1 : bh - 1;
  const int *R0 = blA[0] + y0 * bw, *R1 = blA[0] + y1 * bw, *G0 = blA[1] + y0 * bw, *G1 = blA[1] + y1 * bw, *B0 = blA[2] + y0 * bw, *B1 = blA[2] + y1 * bw;
  /* ×4 fixed point vertically; across a texel the E entries step by exact multiples of the difference
     (accumulated ×E), so each entry is the floored linear value and never dips below zero */
  int pr = (R0[0] << 2) + (((R1[0] - R0[0]) * fv) >> 6), pg = (G0[0] << 2) + (((G1[0] - G0[0]) * fv) >> 6), pb = (B0[0] << 2) + (((B1[0] - B0[0]) * fv) >> 6);
  int E = bT >> 1, lgE = bLg - 1, sh = lgE + 2, i = 0;
  for (int x = 0; x < bw; x++) {
    int xn = x < bw - 1 ? x + 1 : x;
    int nr = (R0[xn] << 2) + (((R1[xn] - R0[xn]) * fv) >> 6), ng = (G0[xn] << 2) + (((G1[xn] - G0[xn]) * fv) >> 6), nb = (B0[xn] << 2) + (((B1[xn] - B0[xn]) * fv) >> 6);
    if (E == 2) {
      br[i].r = pr >> 2; br[i].gb = ((u32)(pg >> 2) << 11) | (u32)(pb >> 2);
      br[i + 1].r = (pr + nr) >> 3; br[i + 1].gb = ((u32)((pg + ng) >> 3) << 11) | (u32)((pb + nb) >> 3);
      i += 2;
    } else {
      int dr = nr - pr, dg = ng - pg, db = nb - pb, ar = pr << lgE, ag = pg << lgE, ab = pb << lgE;
      for (int k = 0; k < E; k++, i++, ar += dr, ag += dg, ab += db) { br[i].r = ar >> sh; br[i].gb = ((u32)(ag >> sh) << 11) | (u32)(ab >> sh); }
    }
    pr = nr; pg = ng; pb = nb;
  }
  for (int n = OW / 2; i < n; i++) br[i] = br[i - 1];
}
/* shockwave displacement per bloom texel (output px) */
static short wdx[BWMAX * BHMAX], wdy[BWMAX * BHMAX]; static u8 wrow[BHMAX];
typedef struct { float x, y, r, s; } Wave;
static void waves_build(const Wave *w, int n) {
  mzero(wrow, sizeof wrow);
  if (!n) return;
  mzero(wdx, bw * bh * sizeof(short)); mzero(wdy, bw * bh * sizeof(short));
  int lim = 40 * OH / 480;
  for (int k = 0; k < n; k++) {
    float cx = w[k].x * OW, cy = (1 - w[k].y) * OH, r = w[k].r, wid = 0.035f + r * 0.12f, band = 2.6f * wid * OH, R = r * OH, iwid = 1 / wid;
    for (int by = 0; by < bh; by++) {
      float py = by * bT + bT * 0.5f - cy; if (mfabs(py) > R + band) continue;
      for (int bx = 0; bx < bw; bx++) {
        float px = bx * bT + bT * 0.5f - cx, d2 = px * px + py * py;
        if (d2 > (R + band) * (R + band) || (R > band && d2 < (R - band) * (R - band))) continue;
        float d = msqrt(d2); if (d < 1e-3f) continue;
        float x = (d * (1.f / OH) - r) * iwid, ring = mexp(-x * x) * w[k].s * x, m = ring * 0.022f * OH / d;
        int i = by * bw + bx, ox = wdx[i] + mlrint(px * m), oy = wdy[i] + mlrint(py * m);
        wdx[i] = (short)clampi(ox, -lim, lim); wdy[i] = (short)clampi(oy, -lim, lim);
        if (ox || oy) wrow[by] = 1;
      }
    }
  }
}
/* Dimmed copies of a row (line-multiplied tiers): ×0.906 (cls 1) or ×0.8125 (cls 2), all channels at once */
static void line_copy(int dy, int sy, int cls) {
  if (!cls) { if (dy != sy) memcpy((u8 *)outbuf + (size_t)dy * OW * (fmt32 ? 4 : 2), (u8 *)outbuf + (size_t)sy * OW * (fmt32 ? 4 : 2), OW * (fmt32 ? 4 : 2)); return; }
  if (fmt32) {
    const u32 *a = OB32 + (size_t)sy * OW; u32 *d = OB32 + (size_t)dy * OW;
    K.dim32(d, a, OW, cls);
  } else {
    const u32 *a = (const u32 *)(OB16 + (size_t)sy * OW); u32 *d = (u32 *)(OB16 + (size_t)dy * OW);
    if (cls == 1) for (int x = 0; x < OW / 2; x++) { u32 v = a[x]; d[x] = v - ((v >> 3) & 0x18E318E3u) + ((v >> 5) & 0x00200020u); }
    else for (int x = 0; x < OW / 2; x++) { u32 v = a[x]; d[x] = v - ((v >> 2) & 0x39E739E7u) + ((v >> 4) & 0x08610861u); }
  }
}
INL u32 cpx16(const u16 *L, u32 s, const BRow *b) { u32 t = (s & 0x3FFFFFu) + b->gb; return L[(s >> 22) + b->r] | L[2048 + (t >> 11)] | L[4096 + (t & 2047)]; }
INL u32 cpx32(const u32 *L, u32 s, const BRow *b) { u32 t = (s & 0x3FFFFFu) + b->gb; return L[(s >> 22) + b->r] | L[2048 + (t >> 11)] | L[4096 + (t & 2047)]; }
/* ---------------- pixel kernels: scalar, SSE2 and AVX2, chosen at run time ----------------
   Every version produces bit-identical pixels; the SIMD ones are compiled for their instruction set
   per function, so the rest of the program stays plain i686 and still runs on a Pentium Pro. */
static void comp32_c(u32 *d, const u32 *sp, const BRow *b, const u32 *L, int n4, int xs) {
  u32 *de = d + n4 * 4;
  if (xs == 0) { do { d[0] = cpx32(L, sp[0], b); d[1] = cpx32(L, sp[1], b); d[2] = cpx32(L, sp[2], b + 1); d[3] = cpx32(L, sp[3], b + 1); sp += 4; b += 2; d += 4; } while (d < de); }
  else if (xs == 1) { do { d[0] = d[1] = cpx32(L, sp[0], b); d[2] = d[3] = cpx32(L, sp[1], b + 1); sp += 2; b += 2; d += 4; } while (d < de); }
  else { do { d[0] = d[1] = d[2] = d[3] = cpx32(L, *sp++, b); b += 2; d += 4; } while (d < de); }
}
static void addspan32_c(u32 *p, int n, u32 c) { for (int i = 0; i < n; i++) p[i] = sat_add8(p[i], c); }
static void addrow32_c(u32 *p, const u32 *c, int n) { for (int i = 0; i < n; i++) p[i] = sat_add8(p[i], c[i]); }
static void dim32_c(u32 *d, const u32 *a, int n, int cls) {
  if (cls == 1) for (int x = 0; x < n; x++) { u32 v = a[x]; d[x] = v - ((v >> 3) & 0x1F1F1Fu) + ((v >> 5) & 0x070707u); }
  else for (int x = 0; x < n; x++) { u32 v = a[x]; d[x] = v - ((v >> 2) & 0x3F3F3Fu) + ((v >> 4) & 0x0F0F0Fu); }
}
static void scale32_c(u32 *p, size_t n, int kk, u32 tv) { for (size_t i = 0; i < n; i++) { u32 q = p[i], rb = (((q & 0xFF00FFu) * kk) >> 8) & 0xFF00FFu, g = (((q & 0xFF00u) * kk) >> 8) & 0xFF00u; p[i] = rb + g + tv; } }
static void glass32_c(u32 *p, int n, u32 v) { for (int i = 0; i < n; i++) p[i] = ((p[i] >> 2) & 0x3F3F3Fu) + v; }
static void padd_span_c(u32 *p, int n, u32 c) { for (int i = 0; i < n; i++) p[i] = padd(p[i], c); }
static void emit32_c(u32 *d, const u32 *a, int n, int cls, int nt) { (void)nt; if (!cls) memcpy(d, a, (size_t)n * 4); else dim32_c(d, a, n, cls); }
static void fence_c(void) {}
#if defined(__i386__) || defined(__x86_64__)
#define SSE2_FN __attribute__((target("sse2"), force_align_arg_pointer))
#define AVX2_FN __attribute__((target("avx2"), force_align_arg_pointer))
SSE2_FN static void addspan32_sse2(u32 *p, int n, u32 c) {
  __m128i C = _mm_set1_epi32((int)c); int i = 0;
  for (; i + 4 <= n; i += 4) _mm_storeu_si128((__m128i *)(p + i), _mm_adds_epu8(_mm_loadu_si128((const __m128i *)(p + i)), C));
  for (; i < n; i++) p[i] = sat_add8(p[i], c);
}
SSE2_FN static void addrow32_sse2(u32 *p, const u32 *c, int n) {
  int i = 0;
  for (; i + 4 <= n; i += 4) _mm_storeu_si128((__m128i *)(p + i), _mm_adds_epu8(_mm_loadu_si128((const __m128i *)(p + i)), _mm_loadu_si128((const __m128i *)(c + i))));
  for (; i < n; i++) p[i] = sat_add8(p[i], c[i]);
}
SSE2_FN static void dim32_sse2(u32 *d, const u32 *a, int n, int cls) {
  int s1 = cls == 1 ? 3 : 2, s2 = cls == 1 ? 5 : 4, i = 0;
  __m128i m1 = _mm_set1_epi32(cls == 1 ? 0x1F1F1F : 0x3F3F3F), m2 = _mm_set1_epi32(cls == 1 ? 0x070707 : 0x0F0F0F);
  for (; i + 4 <= n; i += 4) {
    __m128i v = _mm_loadu_si128((const __m128i *)(a + i));
    _mm_storeu_si128((__m128i *)(d + i), _mm_add_epi32(_mm_sub_epi32(v, _mm_and_si128(_mm_srl_epi32(v, _mm_cvtsi32_si128(s1)), m1)), _mm_and_si128(_mm_srl_epi32(v, _mm_cvtsi32_si128(s2)), m2)));
  }
  if (i < n) dim32_c(d + i, a + i, n - i, cls);
}
SSE2_FN static void scale32_sse2(u32 *p, size_t n, int kk, u32 tv) {
  __m128i K16 = _mm_set1_epi16((short)kk), m = _mm_set1_epi32(0x00FF00FF), T = _mm_set1_epi32((int)tv); size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    __m128i q = _mm_loadu_si128((const __m128i *)(p + i));
    __m128i rb = _mm_srli_epi16(_mm_mullo_epi16(_mm_and_si128(q, m), K16), 8), g = _mm_srli_epi16(_mm_mullo_epi16(_mm_and_si128(_mm_srli_epi32(q, 8), m), K16), 8);
    _mm_storeu_si128((__m128i *)(p + i), _mm_add_epi32(_mm_or_si128(rb, _mm_slli_epi32(_mm_and_si128(g, _mm_set1_epi32(0xFF)), 8)), T));
  }
  if (i < n) scale32_c(p + i, n - i, kk, tv);
}
SSE2_FN static void glass32_sse2(u32 *p, int n, u32 v) {
  __m128i V = _mm_set1_epi32((int)v), m = _mm_set1_epi32(0x3F3F3F); int i = 0;
  for (; i + 4 <= n; i += 4) _mm_storeu_si128((__m128i *)(p + i), _mm_add_epi32(_mm_and_si128(_mm_srli_epi32(_mm_loadu_si128((const __m128i *)(p + i)), 2), m), V));
  for (; i < n; i++) p[i] = ((p[i] >> 2) & 0x3F3F3Fu) + v;
}
/* 10:10:10 saturating add, branch-free: guard-bit overflow fills that field, a carry out of bit 31 fills red */
SSE2_FN static void padd_span_sse2(u32 *p, int n, u32 c) {
  __m128i C = _mm_set1_epi32((int)c), G = _mm_set1_epi32((int)GUARD), S = _mm_set1_epi32((int)0x80000000u), RF = _mm_set1_epi32((int)0xFFC00000u); int i = 0;
  for (; i + 4 <= n; i += 4) {
    __m128i a = _mm_loadu_si128((const __m128i *)(p + i)), s = _mm_add_epi32(a, C), o = _mm_and_si128(s, G);
    __m128i cy = _mm_cmpgt_epi32(_mm_xor_si128(a, S), _mm_xor_si128(s, S)), o10 = _mm_srli_epi32(o, 10);
    s = _mm_or_si128(_mm_or_si128(_mm_andnot_si128(G, s), _mm_sub_epi32(_mm_slli_epi32(o10, 10), o10)), _mm_and_si128(cy, RF));
    _mm_storeu_si128((__m128i *)(p + i), s);
  }
  for (; i < n; i++) p[i] = padd(p[i], c);
}
/* scratch → output; with nt, 16-byte non-temporal stores (output rows start 16-byte aligned: OW is a multiple of 4) */
SSE2_FN static void emit32_sse2(u32 *d, const u32 *a, int n, int cls, int nt) {
  int i = 0;
  if (!nt) { if (!cls) memcpy(d, a, (size_t)n * 4); else dim32_sse2(d, a, n, cls); return; }
  if (((size_t)d & 15) != 0) { emit32_c(d, a, n, cls, 0); return; }
  if (!cls) { for (; i + 4 <= n; i += 4) _mm_stream_si128((__m128i *)(d + i), _mm_loadu_si128((const __m128i *)(a + i))); }
  else {
    int s1 = cls == 1 ? 3 : 2, s2 = cls == 1 ? 5 : 4; __m128i c1 = _mm_cvtsi32_si128(s1), c2 = _mm_cvtsi32_si128(s2);
    __m128i m1 = _mm_set1_epi32(cls == 1 ? 0x1F1F1F : 0x3F3F3F), m2 = _mm_set1_epi32(cls == 1 ? 0x070707 : 0x0F0F0F);
    for (; i + 4 <= n; i += 4) { __m128i v = _mm_loadu_si128((const __m128i *)(a + i)); _mm_stream_si128((__m128i *)(d + i), _mm_add_epi32(_mm_sub_epi32(v, _mm_and_si128(_mm_srl_epi32(v, c1), m1)), _mm_and_si128(_mm_srl_epi32(v, c2), m2))); }
  }
  if (i < n) emit32_c(d + i, a + i, n - i, cls, 0);
}
SSE2_FN static void fence_sse2(void) { _mm_sfence(); }
AVX2_FN static void addspan32_avx2(u32 *p, int n, u32 c) {
  __m256i C = _mm256_set1_epi32((int)c); int i = 0;
  for (; i + 8 <= n; i += 8) _mm256_storeu_si256((__m256i *)(p + i), _mm256_adds_epu8(_mm256_loadu_si256((const __m256i *)(p + i)), C));
  for (; i < n; i++) p[i] = sat_add8(p[i], c);
}
AVX2_FN static void addrow32_avx2(u32 *p, const u32 *c, int n) {
  int i = 0;
  for (; i + 8 <= n; i += 8) _mm256_storeu_si256((__m256i *)(p + i), _mm256_adds_epu8(_mm256_loadu_si256((const __m256i *)(p + i)), _mm256_loadu_si256((const __m256i *)(c + i))));
  for (; i < n; i++) p[i] = sat_add8(p[i], c[i]);
}
AVX2_FN static void dim32_avx2(u32 *d, const u32 *a, int n, int cls) {
  int s1 = cls == 1 ? 3 : 2, s2 = cls == 1 ? 5 : 4, i = 0;
  __m256i m1 = _mm256_set1_epi32(cls == 1 ? 0x1F1F1F : 0x3F3F3F), m2 = _mm256_set1_epi32(cls == 1 ? 0x070707 : 0x0F0F0F);
  __m128i c1 = _mm_cvtsi32_si128(s1), c2 = _mm_cvtsi32_si128(s2);
  for (; i + 8 <= n; i += 8) {
    __m256i v = _mm256_loadu_si256((const __m256i *)(a + i));
    _mm256_storeu_si256((__m256i *)(d + i), _mm256_add_epi32(_mm256_sub_epi32(v, _mm256_and_si256(_mm256_srl_epi32(v, c1), m1)), _mm256_and_si256(_mm256_srl_epi32(v, c2), m2)));
  }
  if (i < n) dim32_c(d + i, a + i, n - i, cls);
}
AVX2_FN static void scale32_avx2(u32 *p, size_t n, int kk, u32 tv) {
  __m256i K16 = _mm256_set1_epi16((short)kk), m = _mm256_set1_epi32(0x00FF00FF), T = _mm256_set1_epi32((int)tv), mf = _mm256_set1_epi32(0xFF); size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    __m256i q = _mm256_loadu_si256((const __m256i *)(p + i));
    __m256i rb = _mm256_srli_epi16(_mm256_mullo_epi16(_mm256_and_si256(q, m), K16), 8), g = _mm256_srli_epi16(_mm256_mullo_epi16(_mm256_and_si256(_mm256_srli_epi32(q, 8), m), K16), 8);
    _mm256_storeu_si256((__m256i *)(p + i), _mm256_add_epi32(_mm256_or_si256(rb, _mm256_slli_epi32(_mm256_and_si256(g, mf), 8)), T));
  }
  if (i < n) scale32_c(p + i, n - i, kk, tv);
}
AVX2_FN static void glass32_avx2(u32 *p, int n, u32 v) {
  __m256i V = _mm256_set1_epi32((int)v), m = _mm256_set1_epi32(0x3F3F3F); int i = 0;
  for (; i + 8 <= n; i += 8) _mm256_storeu_si256((__m256i *)(p + i), _mm256_add_epi32(_mm256_and_si256(_mm256_srli_epi32(_mm256_loadu_si256((const __m256i *)(p + i)), 2), m), V));
  for (; i < n; i++) p[i] = ((p[i] >> 2) & 0x3F3F3Fu) + v;
}
AVX2_FN static void padd_span_avx2(u32 *p, int n, u32 c) {
  __m256i C = _mm256_set1_epi32((int)c), G = _mm256_set1_epi32((int)GUARD), S = _mm256_set1_epi32((int)0x80000000u), RF = _mm256_set1_epi32((int)0xFFC00000u); int i = 0;
  for (; i + 8 <= n; i += 8) {
    __m256i a = _mm256_loadu_si256((const __m256i *)(p + i)), s = _mm256_add_epi32(a, C), o = _mm256_and_si256(s, G);
    __m256i cy = _mm256_cmpgt_epi32(_mm256_xor_si256(a, S), _mm256_xor_si256(s, S)), o10 = _mm256_srli_epi32(o, 10);
    s = _mm256_or_si256(_mm256_or_si256(_mm256_andnot_si256(G, s), _mm256_sub_epi32(_mm256_slli_epi32(o10, 10), o10)), _mm256_and_si256(cy, RF));
    _mm256_storeu_si256((__m256i *)(p + i), s);
  }
  for (; i < n; i++) p[i] = padd(p[i], c);
}
/* capsule edge rows, 4 or 8 pixels at a time: distance, coverage, colour and the 10:10:10 add all in vectors;
   pixels outside the shape get colour 0, which the saturating add leaves unchanged */
SSE2_FN static void caprow_sse2(u32 *row, int a, int b, const CapRow *c) {
  const __m128 XSv = _mm_set1_ps(c->XS), X0 = _mm_set1_ps(c->x0), DX = _mm_set1_ps(c->dx), DY = _mm_set1_ps(c->dy), IL = _mm_set1_ps(c->invL2);
  const __m128 PY = _mm_set1_ps(c->py), PYDY = _mm_set1_ps(c->py * c->dy), Z = _mm_setzero_ps(), ONE = _mm_set1_ps(1), K256 = _mm_set1_ps(256), I256 = _mm_set1_ps(1.f / 256);
  const __m128 LANE = _mm_setr_ps(0.5f, 1.5f, 2.5f, 3.5f), CR = _mm_set1_ps((float)c->cr), CG = _mm_set1_ps((float)c->cg), CB = _mm_set1_ps((float)c->cb);
  const __m128i M1023 = _mm_set1_epi32(1023), G = _mm_set1_epi32((int)GUARD), S = _mm_set1_epi32((int)0x80000000u), RF = _mm_set1_epi32((int)0xFFC00000u), ZI = _mm_setzero_si128();
  int i = a, lim = RW - 4 < b ? RW - 4 : b;   /* a partial last vector is masked, unless it would run past the row */
  const __m128i LI = _mm_setr_epi32(0, 1, 2, 3);
  for (; i <= lim; i += 4) {
    __m128 X = _mm_sub_ps(_mm_mul_ps(_mm_add_ps(_mm_cvtepi32_ps(_mm_set1_epi32(i)), LANE), XSv), X0);
    __m128 t = _mm_min_ps(_mm_max_ps(_mm_mul_ps(_mm_add_ps(_mm_mul_ps(X, DX), PYDY), IL), Z), ONE);
    __m128 ex = _mm_sub_ps(X, _mm_mul_ps(DX, t)), ey = _mm_sub_ps(PY, _mm_mul_ps(DY, t)), d2 = _mm_add_ps(_mm_mul_ps(ex, ex), _mm_mul_ps(ey, ey));
    __m128 sq = _mm_sqrt_ps(d2), wf, in;
    if (c->soft) { in = _mm_cmplt_ps(d2, _mm_set1_ps(c->R2)); wf = _mm_mul_ps(_mm_sub_ps(ONE, _mm_mul_ps(sq, _mm_set1_ps(c->invR))), K256); }
    else {
      in = _mm_cmplt_ps(d2, _mm_set1_ps(c->rout2));
      __m128 v = _mm_min_ps(_mm_max_ps(_mm_add_ps(_mm_mul_ps(_mm_sub_ps(_mm_set1_ps(c->R), sq), _mm_set1_ps(c->invpix)), _mm_set1_ps(0.5f)), Z), ONE);
      __m128 full = _mm_cmple_ps(d2, _mm_set1_ps(c->rin2));
      wf = _mm_mul_ps(_mm_or_ps(_mm_and_ps(full, ONE), _mm_andnot_ps(full, v)), K256);
    }
    in = _mm_and_ps(in, _mm_castsi128_ps(_mm_cmpgt_epi32(_mm_set1_epi32(b - i + 1), LI)));
    if (!_mm_movemask_ps(in)) continue;
    __m128i w = _mm_and_si128(_mm_cvtps_epi32(wf), _mm_castps_si128(in)); w = _mm_and_si128(w, _mm_cmpgt_epi32(w, ZI));
    __m128 ww = _mm_mul_ps(_mm_cvtepi32_ps(w), I256);
    __m128i r = _mm_cvttps_epi32(_mm_mul_ps(CR, ww)), g = _mm_cvttps_epi32(_mm_mul_ps(CG, ww)), bb = _mm_cvttps_epi32(_mm_mul_ps(CB, ww));
#define MIN1023(v) _mm_or_si128(_mm_and_si128(_mm_cmpgt_epi32(v, M1023), M1023), _mm_andnot_si128(_mm_cmpgt_epi32(v, M1023), v))
    r = MIN1023(r); g = MIN1023(g); bb = MIN1023(bb);
#undef MIN1023
    __m128i col = _mm_or_si128(_mm_or_si128(_mm_slli_epi32(r, 22), _mm_slli_epi32(g, 11)), bb);
    __m128i av = _mm_loadu_si128((const __m128i *)(row + i)), s = _mm_add_epi32(av, col), o = _mm_and_si128(s, G);
    __m128i cy = _mm_cmpgt_epi32(_mm_xor_si128(av, S), _mm_xor_si128(s, S)), o10 = _mm_srli_epi32(o, 10);
    _mm_storeu_si128((__m128i *)(row + i), _mm_or_si128(_mm_or_si128(_mm_andnot_si128(G, s), _mm_sub_epi32(_mm_slli_epi32(o10, 10), o10)), _mm_and_si128(cy, RF)));
  }
  if (i <= b) caprow_c(row, i, b, c);
}
AVX2_FN static void caprow_avx2(u32 *row, int a, int b, const CapRow *c) {
  const __m256 XSv = _mm256_set1_ps(c->XS), X0 = _mm256_set1_ps(c->x0), DX = _mm256_set1_ps(c->dx), DY = _mm256_set1_ps(c->dy), IL = _mm256_set1_ps(c->invL2);
  const __m256 PY = _mm256_set1_ps(c->py), PYDY = _mm256_set1_ps(c->py * c->dy), Z = _mm256_setzero_ps(), ONE = _mm256_set1_ps(1), K256 = _mm256_set1_ps(256), I256 = _mm256_set1_ps(1.f / 256);
  const __m256 LANE = _mm256_setr_ps(0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f), CR = _mm256_set1_ps((float)c->cr), CG = _mm256_set1_ps((float)c->cg), CB = _mm256_set1_ps((float)c->cb);
  const __m256i M1023 = _mm256_set1_epi32(1023), G = _mm256_set1_epi32((int)GUARD), S = _mm256_set1_epi32((int)0x80000000u), RF = _mm256_set1_epi32((int)0xFFC00000u), ZI = _mm256_setzero_si256();
  int i = a, lim = RW - 8 < b ? RW - 8 : b;
  const __m256i LI = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
  for (; i <= lim; i += 8) {
    __m256 X = _mm256_sub_ps(_mm256_mul_ps(_mm256_add_ps(_mm256_cvtepi32_ps(_mm256_set1_epi32(i)), LANE), XSv), X0);
    __m256 t = _mm256_min_ps(_mm256_max_ps(_mm256_mul_ps(_mm256_add_ps(_mm256_mul_ps(X, DX), PYDY), IL), Z), ONE);
    __m256 ex = _mm256_sub_ps(X, _mm256_mul_ps(DX, t)), ey = _mm256_sub_ps(PY, _mm256_mul_ps(DY, t)), d2 = _mm256_add_ps(_mm256_mul_ps(ex, ex), _mm256_mul_ps(ey, ey));
    __m256 sq = _mm256_sqrt_ps(d2), wf, in;
    if (c->soft) { in = _mm256_cmp_ps(d2, _mm256_set1_ps(c->R2), _CMP_LT_OQ); wf = _mm256_mul_ps(_mm256_sub_ps(ONE, _mm256_mul_ps(sq, _mm256_set1_ps(c->invR))), K256); }
    else {
      in = _mm256_cmp_ps(d2, _mm256_set1_ps(c->rout2), _CMP_LT_OQ);
      __m256 v = _mm256_min_ps(_mm256_max_ps(_mm256_add_ps(_mm256_mul_ps(_mm256_sub_ps(_mm256_set1_ps(c->R), sq), _mm256_set1_ps(c->invpix)), _mm256_set1_ps(0.5f)), Z), ONE);
      wf = _mm256_mul_ps(_mm256_blendv_ps(v, ONE, _mm256_cmp_ps(d2, _mm256_set1_ps(c->rin2), _CMP_LE_OQ)), K256);
    }
    in = _mm256_and_ps(in, _mm256_castsi256_ps(_mm256_cmpgt_epi32(_mm256_set1_epi32(b - i + 1), LI)));
    if (!_mm256_movemask_ps(in)) continue;
    __m256i w = _mm256_max_epi32(_mm256_and_si256(_mm256_cvtps_epi32(wf), _mm256_castps_si256(in)), ZI);
    __m256 ww = _mm256_mul_ps(_mm256_cvtepi32_ps(w), I256);
    __m256i r = _mm256_min_epi32(_mm256_cvttps_epi32(_mm256_mul_ps(CR, ww)), M1023), g = _mm256_min_epi32(_mm256_cvttps_epi32(_mm256_mul_ps(CG, ww)), M1023), bb = _mm256_min_epi32(_mm256_cvttps_epi32(_mm256_mul_ps(CB, ww)), M1023);
    __m256i col = _mm256_or_si256(_mm256_or_si256(_mm256_slli_epi32(r, 22), _mm256_slli_epi32(g, 11)), bb);
    __m256i av = _mm256_loadu_si256((const __m256i *)(row + i)), s = _mm256_add_epi32(av, col), o = _mm256_and_si256(s, G);
    __m256i cy = _mm256_cmpgt_epi32(_mm256_xor_si256(av, S), _mm256_xor_si256(s, S)), o10 = _mm256_srli_epi32(o, 10);
    _mm256_storeu_si256((__m256i *)(row + i), _mm256_or_si256(_mm256_or_si256(_mm256_andnot_si256(G, s), _mm256_sub_epi32(_mm256_slli_epi32(o10, 10), o10)), _mm256_and_si256(cy, RF)));
  }
  if (i <= b) caprow_c(row, i, b, c);
}
SSE2_FN static void ovrow32_sse2(u32 *row, int a, int b, const OvRow *c) {
  const __m128 X0 = _mm_set1_ps(c->x0), DX = _mm_set1_ps(c->dx), DY = _mm_set1_ps(c->dy), IL = _mm_set1_ps(c->invL2), PY = _mm_set1_ps(c->py), PYDY = _mm_set1_ps(c->py * c->dy);
  const __m128 Z = _mm_setzero_ps(), ONE = _mm_set1_ps(1), HALF = _mm_set1_ps(0.5f), RR = _mm_set1_ps(c->R), K256 = _mm_set1_ps(256), I16 = _mm_set1_ps(1.f / 65536), LANE = _mm_setr_ps(0.5f, 1.5f, 2.5f, 3.5f);
  const __m128 CR = _mm_set1_ps((float)c->r), CG = _mm_set1_ps((float)c->g), CB = _mm_set1_ps((float)c->b), RO = _mm_set1_ps(c->rout2), RI = _mm_set1_ps(c->rin2);
  const __m128i LI = _mm_setr_epi32(0, 1, 2, 3), ZI = _mm_setzero_si128();
  int x = a, lim = OW - 4 < b ? OW - 4 : b;
  for (; x <= lim; x += 4) {
    __m128 X = _mm_sub_ps(_mm_add_ps(_mm_cvtepi32_ps(_mm_set1_epi32(x)), LANE), X0);
    __m128 t = _mm_min_ps(_mm_max_ps(_mm_mul_ps(_mm_add_ps(_mm_mul_ps(X, DX), PYDY), IL), Z), ONE);
    __m128 ex = _mm_sub_ps(X, _mm_mul_ps(DX, t)), ey = _mm_sub_ps(PY, _mm_mul_ps(DY, t)), d2 = _mm_add_ps(_mm_mul_ps(ex, ex), _mm_mul_ps(ey, ey));
    __m128 in = _mm_and_ps(_mm_cmplt_ps(d2, RO), _mm_castsi128_ps(_mm_cmpgt_epi32(_mm_set1_epi32(b - x + 1), LI)));
    if (!_mm_movemask_ps(in)) continue;
    __m128 v = _mm_min_ps(_mm_max_ps(_mm_add_ps(_mm_sub_ps(RR, _mm_sqrt_ps(d2)), HALF), Z), ONE), full = _mm_cmple_ps(d2, RI);
    __m128i w = _mm_and_si128(_mm_cvtps_epi32(_mm_mul_ps(_mm_or_ps(_mm_and_ps(full, ONE), _mm_andnot_ps(full, v)), K256)), _mm_castps_si128(in));
    w = _mm_and_si128(w, _mm_cmpgt_epi32(w, ZI));
    __m128 wf = _mm_mul_ps(_mm_cvtepi32_ps(w), I16);
    const __m128 C255 = _mm_set1_ps(255);
    __m128i r = _mm_cvttps_epi32(_mm_min_ps(_mm_mul_ps(CR, wf), C255)), g = _mm_cvttps_epi32(_mm_min_ps(_mm_mul_ps(CG, wf), C255)), bb = _mm_cvttps_epi32(_mm_min_ps(_mm_mul_ps(CB, wf), C255));
    __m128i col = _mm_or_si128(_mm_or_si128(_mm_slli_epi32(r, 16), _mm_slli_epi32(g, 8)), bb);
    _mm_storeu_si128((__m128i *)(row + x), _mm_adds_epu8(_mm_loadu_si128((const __m128i *)(row + x)), col));
  }
  if (x <= b) ovrow32_c(row, x, b, c);
}
AVX2_FN static void ovrow32_avx2(u32 *row, int a, int b, const OvRow *c) {
  const __m256 X0 = _mm256_set1_ps(c->x0), DX = _mm256_set1_ps(c->dx), DY = _mm256_set1_ps(c->dy), IL = _mm256_set1_ps(c->invL2), PY = _mm256_set1_ps(c->py), PYDY = _mm256_set1_ps(c->py * c->dy);
  const __m256 Z = _mm256_setzero_ps(), ONE = _mm256_set1_ps(1), HALF = _mm256_set1_ps(0.5f), RR = _mm256_set1_ps(c->R), K256 = _mm256_set1_ps(256), RO = _mm256_set1_ps(c->rout2), RI = _mm256_set1_ps(c->rin2);
  const __m256 LANE = _mm256_setr_ps(0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f);
  const __m256i LI = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7), ZI = _mm256_setzero_si256(), CRi = _mm256_set1_epi32(c->r), CGi = _mm256_set1_epi32(c->g), CBi = _mm256_set1_epi32(c->b);
  int x = a, lim = OW - 8 < b ? OW - 8 : b;
  for (; x <= lim; x += 8) {
    __m256 X = _mm256_sub_ps(_mm256_add_ps(_mm256_cvtepi32_ps(_mm256_set1_epi32(x)), LANE), X0);
    __m256 t = _mm256_min_ps(_mm256_max_ps(_mm256_mul_ps(_mm256_add_ps(_mm256_mul_ps(X, DX), PYDY), IL), Z), ONE);
    __m256 ex = _mm256_sub_ps(X, _mm256_mul_ps(DX, t)), ey = _mm256_sub_ps(PY, _mm256_mul_ps(DY, t)), d2 = _mm256_add_ps(_mm256_mul_ps(ex, ex), _mm256_mul_ps(ey, ey));
    __m256 in = _mm256_and_ps(_mm256_cmp_ps(d2, RO, _CMP_LT_OQ), _mm256_castsi256_ps(_mm256_cmpgt_epi32(_mm256_set1_epi32(b - x + 1), LI)));
    if (!_mm256_movemask_ps(in)) continue;
    __m256 v = _mm256_min_ps(_mm256_max_ps(_mm256_add_ps(_mm256_sub_ps(RR, _mm256_sqrt_ps(d2)), HALF), Z), ONE);
    __m256i w = _mm256_max_epi32(_mm256_and_si256(_mm256_cvtps_epi32(_mm256_mul_ps(_mm256_blendv_ps(v, ONE, _mm256_cmp_ps(d2, RI, _CMP_LE_OQ)), K256)), _mm256_castps_si256(in)), ZI);
    const __m256i M255 = _mm256_set1_epi32(255);
    __m256i rr = _mm256_min_epi32(_mm256_srli_epi32(_mm256_mullo_epi32(CRi, w), 16), M255), gg = _mm256_min_epi32(_mm256_srli_epi32(_mm256_mullo_epi32(CGi, w), 16), M255), bq = _mm256_min_epi32(_mm256_srli_epi32(_mm256_mullo_epi32(CBi, w), 16), M255);
    __m256i col = _mm256_or_si256(_mm256_or_si256(_mm256_slli_epi32(rr, 16), _mm256_slli_epi32(gg, 8)), bq);
    _mm256_storeu_si256((__m256i *)(row + x), _mm256_adds_epu8(_mm256_loadu_si256((const __m256i *)(row + x)), col));
  }
  if (x <= b) ovrow32_c(row, x, b, c);
}
/* composite through the tone LUT with three 8-wide gathers per 8 scene pixels; bloom entries are
   spread to the pixels they cover, and wide tiers replicate each result across 2 or 4 output pixels */
AVX2_FN static void comp32_avx2(u32 *d, const u32 *sp, const BRow *b, const u32 *L, int n4, int xs) {
  const __m256i M22 = _mm256_set1_epi32(0x3FFFFF), M11 = _mm256_set1_epi32(2047);
  const int *Li = (const int *)L;
#define LUT8(s, br, bgb, v) { __m256i t_ = _mm256_add_epi32(_mm256_and_si256(s, M22), bgb); \
    v = _mm256_or_si256(_mm256_or_si256(_mm256_i32gather_epi32(Li, _mm256_add_epi32(_mm256_srli_epi32(s, 22), br), 4), \
        _mm256_i32gather_epi32(Li + 2048, _mm256_srli_epi32(t_, 11), 4)), _mm256_i32gather_epi32(Li + 4096, _mm256_and_si256(t_, M11), 4)); }
  if (xs == 0) {
    const __m256i iR = _mm256_setr_epi32(0, 0, 2, 2, 4, 4, 6, 6), iG = _mm256_setr_epi32(1, 1, 3, 3, 5, 5, 7, 7);
    int k = 0;
    for (; k + 2 <= n4; k += 2) {
      __m256i e = _mm256_loadu_si256((const __m256i *)b), s = _mm256_loadu_si256((const __m256i *)sp), v;
      LUT8(s, _mm256_permutevar8x32_epi32(e, iR), _mm256_permutevar8x32_epi32(e, iG), v);
      _mm256_storeu_si256((__m256i *)d, v); sp += 8; b += 4; d += 8;
    }
    if (k < n4) comp32_c(d, sp, b, L, 1, 0);
  } else if (xs == 1) {
    const __m256i ip = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7), i0 = _mm256_setr_epi32(0, 0, 1, 1, 2, 2, 3, 3), i1 = _mm256_setr_epi32(4, 4, 5, 5, 6, 6, 7, 7);
    int k = 0;
    for (; k + 4 <= n4; k += 4) {
      __m256i p0 = _mm256_permutevar8x32_epi32(_mm256_loadu_si256((const __m256i *)b), ip), p1 = _mm256_permutevar8x32_epi32(_mm256_loadu_si256((const __m256i *)(b + 4)), ip);
      __m256i s = _mm256_loadu_si256((const __m256i *)sp), v;
      LUT8(s, _mm256_permute2x128_si256(p0, p1, 0x20), _mm256_permute2x128_si256(p0, p1, 0x31), v);
      _mm256_storeu_si256((__m256i *)d, _mm256_permutevar8x32_epi32(v, i0)); _mm256_storeu_si256((__m256i *)(d + 8), _mm256_permutevar8x32_epi32(v, i1));
      sp += 8; b += 8; d += 16;
    }
    if (k < n4) comp32_c(d, sp, b, L, n4 - k, 1);
  } else {
    const __m256i ig = _mm256_setr_epi32(0, 4, 8, 12, 16, 20, 24, 28);
    const __m256i q0 = _mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1), q1 = _mm256_setr_epi32(2, 2, 2, 2, 3, 3, 3, 3), q2 = _mm256_setr_epi32(4, 4, 4, 4, 5, 5, 5, 5), q3 = _mm256_setr_epi32(6, 6, 6, 6, 7, 7, 7, 7);
    int k = 0;
    for (; k + 8 <= n4; k += 8) {
      const int *bi = (const int *)b; __m256i s = _mm256_loadu_si256((const __m256i *)sp), v;
      LUT8(s, _mm256_i32gather_epi32(bi, ig, 4), _mm256_i32gather_epi32(bi + 1, ig, 4), v);
      _mm256_storeu_si256((__m256i *)d, _mm256_permutevar8x32_epi32(v, q0)); _mm256_storeu_si256((__m256i *)(d + 8), _mm256_permutevar8x32_epi32(v, q1));
      _mm256_storeu_si256((__m256i *)(d + 16), _mm256_permutevar8x32_epi32(v, q2)); _mm256_storeu_si256((__m256i *)(d + 24), _mm256_permutevar8x32_epi32(v, q3));
      sp += 8; b += 16; d += 32;
    }
    if (k < n4) comp32_c(d, sp, b, L, n4 - k, 2);
  }
#undef LUT8
}
/* SSE2 has no gather: the LUT stays scalar, and the wide tiers store each result with one vector write */
SSE2_FN static void comp32_sse2(u32 *d, const u32 *sp, const BRow *b, const u32 *L, int n4, int xs) {
  if (xs == 0) { comp32_c(d, sp, b, L, n4, 0); return; }
  if (xs == 1) { for (int k = 0; k < n4; k++, sp += 2, b += 2, d += 4) { u32 v0 = cpx32(L, sp[0], b), v1 = cpx32(L, sp[1], b + 1); _mm_storeu_si128((__m128i *)d, _mm_setr_epi32((int)v0, (int)v0, (int)v1, (int)v1)); } return; }
  for (int k = 0; k < n4; k++, sp++, b += 2, d += 4) _mm_storeu_si128((__m128i *)d, _mm_set1_epi32((int)cpx32(L, *sp, b)));
}
#endif
static int gatherFast;   /* AVX2 gathers beat scalar LUT loads on this CPU (measured once at start) */
static void simd_set(int level) {
  simdLevel = level < 0 ? 0 : level > simdMax ? simdMax : level;
  K.comp32 = comp32_c; K.addspan32 = addspan32_c; K.addrow32 = addrow32_c; K.dim32 = dim32_c; K.scale32 = scale32_c; K.glass32 = glass32_c; K.padd_span = padd_span_c; K.caprow = caprow_c; K.emit32 = emit32_c; K.fence = fence_c; K.gather = 0; K.ovrow32 = ovrow32_c;
#if defined(__i386__) || defined(__x86_64__)
  if (simdLevel >= 1) { K.comp32 = comp32_sse2; K.addspan32 = addspan32_sse2; K.addrow32 = addrow32_sse2; K.dim32 = dim32_sse2; K.scale32 = scale32_sse2; K.glass32 = glass32_sse2; K.padd_span = padd_span_sse2; K.caprow = caprow_sse2; K.emit32 = emit32_sse2; K.fence = fence_sse2; K.ovrow32 = ovrow32_sse2; }
  if (simdLevel >= 2) { if (gatherFast) { K.comp32 = comp32_avx2; K.gather = 1; } K.addspan32 = addspan32_avx2; K.addrow32 = addrow32_avx2; K.dim32 = dim32_avx2; K.scale32 = scale32_avx2; K.glass32 = glass32_avx2; K.padd_span = padd_span_avx2; K.caprow = caprow_avx2; K.ovrow32 = ovrow32_avx2; }
#endif
}
static struct { int crt, doAb, doSat, slow, abK, satK, n; float vhs, time, trk; } CP;
/** One output row from scene row y >> rYs. dim: use the scanline LUT levels. */
/* horizontal tape-tracking offset of a row (scene px) */
static int row_vo(int y) {
  if (CP.vhs <= 0) return 0;
  float uy = (y + 0.5f) / OH, dy = uy - (1 - CP.trk); if (dy * dy >= 0.0004f) return 0;
  return mlrint(mexp(-dy * dy * 9000) * 0.006f * CP.vhs * msin(uy * 900 + CP.time * 40) * (OW >> rXs));
}
/* Rows bent only by refraction or tape wobble: fetch each block's displaced source pixels into a
   full-width row first (one offset per block), then tone map that row with the ordinary run kernels. */
static u32 compSrc[NWMAX][OWMAX + 16] __attribute__((aligned(64)));
static const u32 *comp_gather(int y, int by, int vo, u32 *g) {
  int xs = rXs, ys = rYs, wr = wrow[by], bo = by * bw;
  for (int x = 0; x < OW;) {
    int bx = x >> bLg, xe;
    if (bx >= bw - 1) { bx = bw - 1; xe = OW; } else { xe = (bx + 1) << bLg; if (xe > OW) xe = OW; }
    int ox = 0, oy = 0; if (wr) { ox = wdx[bo + bx]; oy = wdy[bo + bx]; }
    int sy = (y + oy) >> ys; sy = sy < 0 ? 0 : sy >= RH ? RH - 1 : sy;
    const u32 *r = scene + (size_t)sy * RW;
    int sa = ((x + ox) >> xs) + vo, sb = ((xe - 1 + ox) >> xs) + vo;
    if (sa >= 0 && sb < RW) {
      if (xs == 0) { const u32 *q = r + sa; for (; x < xe; x++) g[x] = *q++; }
      else for (; x < xe; x++) g[x] = r[((x + ox) >> xs) + vo];
    } else for (; x < xe; x++) { int sx = ((x + ox) >> xs) + vo; sx = sx < 0 ? 0 : sx >= RW ? RW - 1 : sx; g[x] = r[sx]; }
  }
  return g;
}
static void comp_row(int y, int dim, const BRow *br, void *dst, int band) {
  int xs = rXs, ys = rYs, j = y >> ys;
  const u32 *src = scene + (size_t)j * RW;
  int vo = row_vo(y);
  const VRun *vr = &vrun[y * 2 + dim];
  int nr = vr->n, x = 0, by = y >> bLg; if (by > bh - 1) by = bh - 1;
  if (!CP.slow && (wrow[by] || vo)) { src = comp_gather(y, by, vo, compSrc[band]); xs = 0; }
  if (!CP.slow) {
    for (int ri = 0; ri < nr; ri++) {
      int xe = vr->end[ri], n4 = (xe - x) >> 2; if (n4 <= 0) continue;
      const BRow *b = br + (x >> 1);
      if (!fmt32) {
        const u16 *L = LUT[vr->lvl[ri]][0]; u32 *d = (u32 *)((u16 *)dst + x), *de = d + n4 * 2;
        if (xs == 0) { const u32 *sp = src + x; do { d[0] = cpx16(L, sp[0], b) | (cpx16(L, sp[1], b) << 16); d[1] = cpx16(L, sp[2], b + 1) | (cpx16(L, sp[3], b + 1) << 16); sp += 4; b += 2; d += 2; } while (d < de); }
        else if (xs == 1) { const u32 *sp = src + (x >> 1); do { u32 v = cpx16(L, sp[0], b); d[0] = v | (v << 16); v = cpx16(L, sp[1], b + 1); d[1] = v | (v << 16); sp += 2; b += 2; d += 2; } while (d < de); }
        else { const u32 *sp = src + (x >> 2); do { u32 v = cpx16(L, *sp++, b); d[0] = d[1] = v | (v << 16); b += 2; d += 2; } while (d < de); }
      } else {
        K.comp32((u32 *)dst + x, src + (x >> xs), b, LUT32[vr->lvl[ri]][0], n4, xs);
      }
      x += n4 * 4;
    }
    return;
  }
  /* slow path: refraction, aberration, saturation, tape wobble */
  int wr = wrow[by], bo = by * bw;
  for (int ri = 0; ri < nr; ri++) {
    int xe = vr->end[ri], lv = vr->lvl[ri];
    for (; x < xe; x++) {
      int bx = x >> bLg; if (bx > bw - 1) bx = bw - 1;
      int ox = 0, oy = 0; if (wr) { ox = wdx[bo + bx]; oy = wdy[bo + bx]; }
      int sx = ((x + ox) >> xs) + vo, sy = (y + oy) >> ys; sx = sx < 0 ? 0 : sx >= RW ? RW - 1 : sx; sy = sy < 0 ? 0 : sy >= RH ? RH - 1 : sy;
      u32 s = scene[(size_t)sy * RW + sx];
      int r = s >> 22, g = (s >> 11) & 1023, bb = s & 1023;
      if (CP.doAb) {
        int ax = (abX[bo + bx] * CP.abK) >> (16 + xs), ay = (abY[bo + bx] * CP.abK) >> (16 + ys);
        int rx = clampi(sx + ax, 0, RW - 1), ry = clampi(sy + ay, 0, RH - 1), qx = clampi(sx - ax, 0, RW - 1), qy = clampi(sy - ay, 0, RH - 1);
        r = scene[(size_t)ry * RW + rx] >> 22; bb = scene[(size_t)qy * RW + qx] & 1023;
      }
      const BRow *b = br + (x >> 1);
      r += b->r; g += b->gb >> 11; bb += b->gb & 2047;
      if (CP.doSat) { int l = (r * 77 + g * 150 + bb * 29) >> 8; r = l + (((r - l) * CP.satK) >> 8); g = l + (((g - l) * CP.satK) >> 8); bb = l + (((bb - l) * CP.satK) >> 8); r = clampi(r, 0, 2047); g = clampi(g, 0, 2047); bb = clampi(bb, 0, 2047); }
      if (fmt32) { const u32 *L = LUT32[lv][0]; ((u32 *)dst)[x] = L[r] | L[2048 + g] | L[4096 + bb]; }
      else { const u16 *L = LUT[lv][0]; ((u16 *)dst)[x] = (u16)(L[r] | L[2048 + g] | L[4096 + bb]); }
    }
  }
}
/** Output rows [y0, y1): y0 a multiple of 4. One scene row's worth of output is computed per group and copied
    to the rows it covers; scanline rows get a dimmed copy. 32-bit output computes into a cached scratch row
    and streams it out (non-temporal stores at HD sizes, so writes don't first read the destination). */
static u32 compTmp[NWMAX][OWMAX + 16] __attribute__((aligned(64)));
static int compNT;
static void comp_rows(int y0, int y1, BRow *br, int band) {
  int G = 1 << rYs;
  if (fmt32) {
    u32 *tmp = compTmp[band];
    for (int y = y0; y < y1; y += G) {
      if ((y & 3) == 0) bloom_row(y, br);
      comp_row(y, 0, br, tmp, band);
      for (int k = 0; k < G && y + k < y1; k++) K.emit32(OB32 + (size_t)(y + k) * OW, tmp, OW, CP.crt ? scanCls[y + k] : 0, compNT);
    }
    if (compNT) K.fence();
    return;
  }
  for (int y = y0; y < y1; y += G) {
    if ((y & 3) == 0) bloom_row(y, br);
    comp_row(y, CP.crt && G == 1, br, OB16 + (size_t)y * OW, band);
    if (G > 1) {
      for (int k = 1; k < G && y + k < y1; k++) line_copy(y + k, y, CP.crt ? scanCls[y + k] : 0);
      if (CP.crt && scanCls[y]) line_copy(y, y, scanCls[y]);
    }
  }
}
static void comp_job(int i) {
  int a = (OH * i / CP.n) & ~3, b = i == CP.n - 1 ? OH : (OH * (i + 1) / CP.n) & ~3;
  comp_rows(a, b, bloomRows[i], i);
}
/** Bloom + tone map + scanlines + vignette (+ shockwave refraction, aberration, desaturation) into the output,
    split into row bands across the worker threads on multi-core machines. */
static void composite(float aberr, float sat, int crt, float vhs, float time) {
  CP.crt = crt; CP.vhs = vhs; CP.time = time; CP.trk = fractf(time * 0.07f);
  CP.doAb = aberr > 0.6f; CP.doSat = sat < 0.97f; CP.slow = CP.doAb || CP.doSat;
  CP.abK = mlrint(aberr * 0.012f * 1024); CP.satK = mlrint(sat * 256);
  int n = nWork; if (n > NWMAX) n = NWMAX; if (n > OH / 64) n = OH / 64; if (n < 1) n = 1;
  CP.n = n;
  compNT = simdLevel >= 1 && fmt32 && (size_t)OW * OH >= (size_t)1920 * 1080;
  if (n > 1) plat_par(n, comp_job); else comp_rows(0, OH, bloomRows[0], 0);
}

/* ---------------- overlay (HUD + menus) on the output, in UI units ---------------- */
static float ovX, ovY;            /* UI-space origin of the layer being drawn (menus: UOX, UOY) */
static int CMR = 31, CMG = 63, CMB = 31;   /* output channel maxima */
typedef struct { int r, g, b; } DC;   /* display colour in output component units ×256 */
static DC disp_col(C3 c) {
  DC d; int ir = clampi(mlrint(c.r * 512), 0, 4096), ig = clampi(mlrint(c.g * 512), 0, 4096), ib = clampi(mlrint(c.b * 512), 0, 4096);
  d.r = mlrint(curveTab[0][ir] * CMR * 256); d.g = mlrint(curveTab[1][ig] * CMG * 256); d.b = mlrint(curveTab[2][ib] * CMB * 256); return d;
}
/* saturating additive pixel write in either format */
INL void ob_add(size_t i, int r, int g, int b) {
  if (fmt32) OB32[i] = sat_add8(OB32[i], ((u32)(r > 255 ? 255 : r) << 16) | ((u32)(g > 255 ? 255 : g) << 8) | (u32)(b > 255 ? 255 : b));
  else { int q = OB16[i], rr = (q >> 11) + r, gg = ((q >> 5) & 63) + g, bb = (q & 31) + b; OB16[i] = (u16)(((rr > 31 ? 31 : rr) << 11) | ((gg > 63 ? 63 : gg) << 5) | (bb > 31 ? 31 : bb)); }
}
INL u32 ob_pack(int r, int g, int b) { return fmt32 ? ((u32)r << 16) | ((u32)g << 8) | (u32)b : (u32)((r << 11) | (g << 5) | b); }
INL void ov_plot(int x, int y, const DC *c, int w) {
  if ((unsigned)x >= (unsigned)OW || (unsigned)y >= (unsigned)OH || w <= 0) return;
  ob_add((size_t)y * OW + x, (c->r * w) >> 16, (c->g * w) >> 16, (c->b * w) >> 16);
}
static void ov_wu_b(float x0, float y0, float x1, float y1, const DC *c, float gain, int ylo, int yhi) {
  float dx = x1 - x0, dy = y1 - y0, t0 = 0, t1 = 1;
  x0 -= 0.5f; y0 -= 0.5f;
  if (!clip_lb(x0, y0, dx, dy, -1.5f, -1.5f, OW + 0.5f, OH + 0.5f, &t0, &t1)) return;
  float ax = x0 + dx * t0, ay = y0 + dy * t0, bx = x0 + dx * t1, by = y0 + dy * t1;
  int steep = mfabs(dy) > mfabs(dx); float a0, b0, a1, b1;
  if (steep) { a0 = ay; b0 = ax; a1 = by; b1 = bx; } else { a0 = ax; b0 = ay; a1 = bx; b1 = by; }
  if (a0 > a1) { float t = a0; a0 = a1; a1 = t; t = b0; b0 = b1; b1 = t; }
  float len = a1 - a0, slope = len > 1e-4f ? (b1 - b0) / len : 0, k = msqrt(1 + slope * slope) * gain;
  int i0 = mlrint(a0), i1 = mlrint(a1);
  if (i1 < i0) i1 = i0;
  for (int i = i0; i <= i1; i++) {
    float b = b0 + slope * (i - a0); int j = mfloori(b); float f = b - j; int w1 = mlrint(f * k * 256), w0 = mlrint((1 - f) * k * 256);
    if (steep) { if (i >= ylo && i < yhi) { ov_plot(j, i, c, w0); ov_plot(j + 1, i, c, w1); } }
    else { if (j >= ylo && j < yhi) ov_plot(i, j, c, w0); if (j + 1 >= ylo && j + 1 < yhi) ov_plot(i, j + 1, c, w1); }
  }
}
/** Capsule in output pixels: interior spans are filled flat, only the edge pixels get exact coverage. */
static void ov_capsule_b(float x0, float y0, float x1, float y1, float R, const DC *c, int ylo, int yhi) {
  float ext = R + 1;
  int ya = clampi(mfloori(minf(y0, y1) - ext), ylo, yhi - 1), yb = clampi(mfloori(maxf(y0, y1) + ext), ylo, yhi - 1);
  if (maxf(y0, y1) + ext < 0 || minf(y0, y1) - ext > OH) return;
  CapG g; cap_geom(&g, x0, y0, x1, y1);
  float L2 = g.dx * g.dx + g.dy * g.dy, invL2 = L2 > 1e-6f ? 1 / L2 : 0;
  float rin = R - 0.5f, rin2 = rin > 0 ? rin * rin : 0, rout = R + 0.5f, rout2 = rout * rout;
  int ar = c->r >> 8, ag = c->g >> 8, ab = c->b >> 8;
  if (fmt32 && simdLevel) {   /* small strokes (text, icons): vector kernel over the bounding box, no span solving per row */
    int bx0 = clampi(mfloori(minf(x0, x1) - ext), 0, OW - 1), bx1 = clampi(mfloori(maxf(x0, x1) + ext), 0, OW - 1);
    if (bx1 - bx0 <= 48) {
      OvRow o; o.x0 = x0; o.dx = g.dx; o.dy = g.dy; o.invL2 = invL2; o.R = R; o.rin2 = rin2; o.rout2 = rout2; o.r = c->r; o.g = c->g; o.b = c->b;
      for (int y = ya; y <= yb; y++) { o.py = y + 0.5f - y0; K.ovrow32(OB32 + (size_t)y * OW, bx0, bx1, &o); }
      return;
    }
  }
  for (int y = ya; y <= yb; y++) {
    float Y = y + 0.5f, ol, orr, il, ir;
    if (!cap_span(&g, Y, rout, &ol, &orr)) continue;
    int i0 = clampi(mfloori(ol - 0.5f), 0, OW - 1), i1 = clampi(mfloori(orr - 0.5f) + 1, 0, OW - 1), k0 = 1, k1 = 0;
    if (rin > 0 && cap_span(&g, Y, rin, &il, &ir)) { k0 = mfloori(il - 0.5f) + 1; k1 = mfloori(ir - 0.5f); if (k0 < i0) k0 = i0; if (k1 > i1) k1 = i1; }
    size_t row = (size_t)y * OW; float py = Y - y0;
    for (int x = i0; x <= i1; x++) {
      if (x == k0 && k0 <= k1) {
        if (fmt32) { K.addspan32(OB32 + row + x, k1 - x + 1, ((u32)(ar > 255 ? 255 : ar) << 16) | ((u32)(ag > 255 ? 255 : ag) << 8) | (u32)(ab > 255 ? 255 : ab)); x = k1 + 1; }
        else { u16 *p = OB16 + row; for (; x <= k1; x++) { int v = p[x], r = (v >> 11) + ar, gg = ((v >> 5) & 63) + ag, b = (v & 31) + ab; if (r > 31) r = 31; if (gg > 63) gg = 63; if (b > 31) b = 31; p[x] = (u16)((r << 11) | (gg << 5) | b); } }
        x = k1; continue;
      }
      float d2 = cap_dist2(&g, x + 0.5f - x0, py, invL2);
      if (d2 >= rout2) continue;
      float cv = d2 <= rin2 ? 1 : clampf(R - msqrt(d2) + 0.5f, 0, 1);
      ov_plot(x, y, c, mlrint(cv * 256));
    }
  }
}
static void ov_capsule(float x0, float y0, float x1, float y1, float R, const DC *c) { ov_capsule_b(x0, y0, x1, y1, R, c, 0, OH); }
/** Draw a batch of UI-space segments, scaled by US from the current origin, into output rows [ylo, yhi). */
static void draw_overlay_rows(const Batch *b, int ylo, int yhi) {
  float k = US, ox = ovX * US, oy = ovY * US;
  for (int i = 0; i < b->n; i++) {
    const Seg *s = &b->s[i];
    float x0 = s->x0 * k + ox, y0 = s->y0 * k + oy, x1 = s->x1 * k + ox, y1 = s->y1 * k + oy, hw = s->w * k, ext = hw + s->halo * k + 2;
    if (maxf(y0, y1) + ext < ylo || minf(y0, y1) - ext >= yhi) continue;
    DC c = disp_col(s->c); if (!(c.r | c.g | c.b)) continue;
    if (s->halo > 0) { DC h = disp_col(cmul(s->c, 0.25f)); ov_capsule_b(x0, y0, x1, y1, hw + s->halo * k, &h, ylo, yhi); }
    if (hw <= 0.85f) { float dx = x1 - x0, dy = y1 - y0; if (dx * dx + dy * dy < 0.25f) ov_capsule_b(x0, y0, x1, y1, maxf(hw, 0.6f), &c, ylo, yhi); else ov_wu_b(x0, y0, x1, y1, &c, 2 * maxf(hw, 0.2f), ylo, yhi); }
    else ov_capsule_b(x0, y0, x1, y1, hw, &c, ylo, yhi);
  }
}
static const Batch *ovJobB; static int ovJobN;
static void ov_job(int i) { int a = OH * i / ovJobN, b = OH * (i + 1) / ovJobN; draw_overlay_rows(ovJobB, a, b); }
/** Draw a batch of UI-space segments; on multi-core machines at HD sizes the rows are split across threads. */
static void draw_overlay(const Batch *b) {
  int n = nWork; if (n > NWMAX) n = NWMAX; if (n > OH / 180) n = OH / 180;
  if (n > 1 && b->n > 40) { ovJobB = b; ovJobN = n; plat_par(n, ov_job); }
  else draw_overlay_rows(b, 0, OH);
}
INL DC dc_direct(C3 c) { DC d; d.r = clampi(mlrint(c.r * CMR * 256), 0, CMR * 256); d.g = clampi(mlrint(c.g * CMG * 256), 0, CMG * 256); d.b = clampi(mlrint(c.b * CMB * 256), 0, CMB * 256); return d; }
/** Darken the whole output (menus over the game): p = p·k + tint. */
static void ov_dim(float k, C3 tint) {
  int kk = mlrint(k * 256); DC t = dc_direct(tint); int tr = t.r >> 8, tg = t.g >> 8, tb = t.b >> 8; size_t n = (size_t)OW * OH;
  if (fmt32) K.scale32(OB32, n, kk, ((u32)tr << 16) | ((u32)tg << 8) | (u32)tb);   /* k ≤ ~0.5, so no field can overflow */
  else for (size_t i = 0; i < n; i++) { int q = OB16[i], r = (((q >> 11) * kk) >> 8) + tr, g = ((((q >> 5) & 63) * kk) >> 8) + tg, bb = (((q & 31) * kk) >> 8) + tb; OB16[i] = (u16)(((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) | (bb > 31 ? 31 : bb)); }
}
INL void ov_box(float x, float y, float w, float h, int *x0, int *y0, int *x1, int *y1) {
  *x0 = clampi(mlrint((x + ovX) * US), 0, OW); *x1 = clampi(mlrint((x + w + ovX) * US), 0, OW); *y0 = clampi(mlrint((y + ovY) * US), 0, OH); *y1 = clampi(mlrint((y + h + ovY) * US), 0, OH);
}
/** Solid or blended rectangles for the menus (UI units). mode 0: set, 1: add, 2: glass (darken + tint) */
static void ov_rect(float x, float y, float w, float h, C3 col, int mode) {
  int x0, y0, x1, y1; ov_box(x, y, w, h, &x0, &y0, &x1, &y1);
  DC c = dc_direct(col); int r = c.r >> 8, g = c.g >> 8, b = c.b >> 8; u32 v = ob_pack(r, g, b);
  for (int yy = y0; yy < y1; yy++) {
    size_t row = (size_t)yy * OW;
    if (x1 <= x0) break;
    if (mode == 0) { if (fmt32) fill32(OB32 + row + x0, v, x1 - x0); else for (int xx = x0; xx < x1; xx++) OB16[row + xx] = (u16)v; }
    else if (mode == 1) { if (fmt32) K.addspan32(OB32 + row + x0, x1 - x0, v); else for (int xx = x0; xx < x1; xx++) ob_add(row + xx, r, g, b); }
    else if (fmt32) K.glass32(OB32 + row + x0, x1 - x0, v);   /* quarter + tint: tint < 0.75 of range */
    else for (int xx = x0; xx < x1; xx++) { int q = OB16[row + xx], rr = ((q >> 11) >> 2) + r, gg = (((q >> 5) & 63) >> 2) + g, bb = ((q & 31) >> 2) + b; OB16[row + xx] = (u16)(((rr > 31 ? 31 : rr) << 11) | ((gg > 63 ? 63 : gg) << 5) | (bb > 31 ? 31 : bb)); }
  }
}
/** Horizontal gradient bar (title bars, focus highlights): colours at 0, 0.5, 1, additive with alpha. */
static void ov_grad(float x, float y, float w, float h, C3 a, C3 m, C3 b, float alpha) {
  int x0, y0, x1, y1; ov_box(x, y, w, h, &x0, &y0, &x1, &y1);
  float px0 = (x + ovX) * US, pw = maxf(w * US, 1);
  static u32 gcol[OWMAX]; static short gr[OWMAX], gg[OWMAX], gb[OWMAX];
  for (int xx = x0; xx < x1; xx++) {
    float f = (xx - px0) / pw; C3 c = f < 0.5f ? clerp(a, m, f * 2) : clerp(m, b, f * 2 - 2 + 1);
    DC d = dc_direct(cmul(c, alpha)); gr[xx] = (short)(d.r >> 8); gg[xx] = (short)(d.g >> 8); gb[xx] = (short)(d.b >> 8);
    gcol[xx] = ((u32)gr[xx] << 16) | ((u32)gg[xx] << 8) | (u32)gb[xx];
  }
  if (x1 > x0) for (int yy = y0; yy < y1; yy++) {
    if (fmt32) K.addrow32(OB32 + (size_t)yy * OW + x0, gcol + x0, x1 - x0);
    else for (int xx = x0; xx < x1; xx++) ob_add((size_t)yy * OW + xx, gr[xx], gg[xx], gb[xx]);
  }
}
/* ---------------- output size / tier ---------------- */
/** New output size (multiple of 4, ≤ OWMAX×OHMAX) or format: (re)allocate the scene and per-row tables. */
static void render_setsize(int ow, int oh) {
  OW = ow; OH = oh;
  if (scene) plat_free(scene); if (vrun) plat_free(vrun); if (scanCls) plat_free(scanCls);
  scene = (u32 *)plat_alloc((size_t)OW * OH * 4 + 64); vrun = (VRun *)plat_alloc(sizeof(VRun) * 2 * OH); scanCls = (u8 *)plat_alloc(OH);
  if (fmt32) { CMR = CMG = CMB = 255; } else { CMR = 31; CMG = 63; CMB = 31; }
  lutKey.valid = 0;
  vignette_init();
  US = minf(OH / 480.f, OW / 640.f); UW = OW / US; UH = OH / US; UOX = (UW - 640) * 0.5f; UOY = (UH - 480) * 0.5f;
}
/** Scene = output >> (xs, ys); lowBloom doubles the bloom texel. */
static void render_settier(int xs, int ys, int lowBloom) {
  rXs = xs; rYs = ys; bLow = lowBloom; RW = OW >> xs; RH = OH >> ys;
  XS = (float)OW / RW; YS = (float)OH / RH; iXS = 1 / XS; iYS = 1 / YS;
  render_config();
}

/* ---------------- vector font: stroke glyphs on a 4×6 grid ---------------- */
static const char *FONT_RAW[][2] = {
  { "A", "06 01 10 30 41 46,03 43" }, { "B", "06 00 30 41 42 33 03,33 44 45 36 06" }, { "C", "41 30 10 01 05 16 36 45" },
  { "D", "00 30 41 45 36 06 00" }, { "E", "40 00 06 46,03 33" }, { "F", "40 00 06,03 33" }, { "G", "41 30 10 01 05 16 36 45 43 23" },
  { "H", "00 06,40 46,03 43" }, { "I", "10 30,20 26,16 36" }, { "J", "40 45 36 16 05 04" }, { "K", "00 06,40 13 46,03 13" },
  { "L", "00 06 46" }, { "M", "06 00 23 40 46" }, { "N", "06 00 46 40" }, { "O", "10 30 41 45 36 16 05 01 10" },
  { "P", "06 00 30 41 42 33 03" }, { "Q", "10 30 41 45 36 16 05 01 10,24 46" }, { "R", "06 00 30 41 42 33 03,23 46" },
  { "S", "41 30 10 01 02 13 33 44 45 36 16 05" }, { "T", "00 40,20 26" }, { "U", "00 05 16 36 45 40" }, { "V", "00 26 40" },
  { "W", "00 16 23 36 40" }, { "X", "00 46,40 06" }, { "Y", "00 23 40,23 26" }, { "Z", "00 40 06 46" },
  { "0", "10 30 41 45 36 16 05 01 10,14 32" }, { "1", "11 20 26,16 36" }, { "2", "01 10 30 41 42 06 46" },
  { "3", "01 10 30 41 42 33 13,33 44 45 36 16 05" }, { "4", "30 04 44,30 36" }, { "5", "40 00 03 33 44 45 36 06" },
  { "6", "41 30 10 01 05 16 36 45 44 33 03" }, { "7", "00 40 41 16" }, { "8", "10 30 41 42 33 13 02 01 10,13 04 05 16 36 45 44 33" },
  { "9", "05 16 36 45 41 30 10 01 02 13 43" }, { ".", "26" }, { ",", "25 16" }, { ":", "21,25" }, { "!", "20 23,26" }, { "?", "01 10 30 41 42 23 24,26" },
  { "-", "13 33" }, { "+", "13 33,22 24" }, { "x", "12 35,32 15" }, { "/", "06 40" }, { "%", "06 40,01,45" }, { "'", "20 21" }, { "\"", "10 11,30 31" },
  { "(", "30 11 15 36" }, { ")", "10 31 35 16" }, { "<", "40 03 46" }, { ">", "00 43 06" }, { "_", "06 46" }, { "=", "02 42,04 44" },
  { "[", "30 10 16 36" }, { "]", "10 30 36 16" }, { "#", "10 16,30 36,02 42,04 44" }, { "*", "11 35,31 15,03 43" }, { "\xB7", "23" }, { "|", "20 26" },
  { "\x0E", "34 30 41,34 25 15 14 34" }, { "\x10", "00 46 03 00" } /* ▶ */, { "\x11", "40 06 43 40" } /* ◀ */, { "\x12", "02 32,22 13 32 23 22" } /* → */,
  { "\x13", "20 32 43 33 46 23 06 13 03 12 20" } /* ★ */
};
typedef struct { u8 n; u8 st[6][2]; u8 pts[6][14][2]; } Glyph;
static Glyph GL[256]; static u8 hasGL[256];
static void font_init(void) {
  for (int g = 0; g < ARR(FONT_RAW); g++) {
    u8 ch = (u8)FONT_RAW[g][0][0]; const char *s = FONT_RAW[g][1]; Glyph *G = &GL[ch]; G->n = 0; int np = 0;
    G->n = 1; G->st[0][0] = 0;
    while (*s) {
      if (*s == ',') { G->st[G->n - 1][1] = (u8)np; G->n++; np = 0; s++; continue; }
      if (*s == ' ') { s++; continue; }
      G->pts[G->n - 1][np][0] = (u8)(s[0] - '0'); G->pts[G->n - 1][np][1] = (u8)(s[1] - '0'); np++; s += 2;
    }
    G->st[G->n - 1][1] = (u8)np; hasGL[ch] = 1;
  }
  /* lower case maps to upper case */
  for (int c = 'a'; c <= 'z'; c++) if (!hasGL[c] && c != 'x') { GL[c] = GL[c - 32]; hasGL[c] = 1; }
}
enum { AL_LEFT, AL_CENTER, AL_RIGHT };
typedef struct { int align; float spacing, w, halo; int italic; const C3 *vgrad; const float *jit; } FOpt;
static float font_width(const char *str, float size, float spacing) {
  float u = size / 6, w = 0; if (spacing <= 0) spacing = 1;
  for (const u8 *p = (const u8 *)str; *p; p++) w += (*p == ' ' ? 4 : 6 * spacing) * u;
  w -= (6 * spacing - 4) * u; return w > 0 ? w : 0;
}
static float font_draw(Batch *b, const char *str, float x, float y, float size, C3 col, const FOpt *o) {
  static const FOpt DEF = { AL_LEFT, 1, 1, 0, 0, 0, 0 }; if (!o) o = &DEF;
  float sp = o->spacing > 0 ? o->spacing : 1, u = size / 6, tw = font_width(str, size, sp);
  float cx = o->align == AL_CENTER ? x - tw / 2 : o->align == AL_RIGHT ? x - tw : x;
  float lw = size * 0.045f * (o->w > 0 ? o->w : 1), ital = o->italic ? 0.2f : 0; int i = 0;
  for (const u8 *p = (const u8 *)str; *p; p++, i++) {
    if (*p == ' ') { cx += 4 * u; continue; }
    if (hasGL[*p]) {
      const Glyph *G = &GL[*p]; float ox = 0, oy = 0;
      if (o->jit) { ox = o->jit[i * 2]; oy = o->jit[i * 2 + 1]; }
      for (int s = 0; s < G->n; s++) {
        int np = G->st[s][1];
#define GX(q) (cx + ox + G->pts[s][q][0] * u - (G->pts[s][q][1] - 6) * u * ital)
#define GY(q) (y + oy + G->pts[s][q][1] * u)
        if (np == 1) { bdoth(b, GX(0), GY(0), lw * 1.25f, col, o->halo); continue; }
        for (int k = 0; k < np - 1; k++) {
          C3 c = col;
          if (o->vgrad) { float t = (G->pts[s][k][1] + G->pts[s][k + 1][1]) / 12.f; c = clerp(o->vgrad[0], o->vgrad[1], t); }
          blineh(b, GX(k), GY(k), GX(k + 1), GY(k + 1), lw, c, o->halo);
        }
#undef GX
#undef GY
      }
    }
    cx += 6 * sp * u;
  }
  return tw;
}

/* ---------------- SVG path icons (24×24 viewBox) → polylines ---------------- */
static float svg_num(const char **pp) {
  const char *p = *pp; while (*p == ' ' || *p == ',') p++;
  float sign = 1, v = 0, f = 0.1f; int dot = 0;
  if (*p == '-') { sign = -1; p++; } else if (*p == '+') p++;
  while ((*p >= '0' && *p <= '9') || (*p == '.' && !dot)) {
    if (*p == '.') { dot = 1; p++; continue; }
    if (dot) { v += (*p - '0') * f; f *= 0.1f; } else v = v * 10 + (*p - '0');
    p++;
  }
  *pp = p; return v * sign;
}
static void svg_draw(Batch *b, const char *d, float x, float y, float size, float w, C3 col) {
  float k = size / 24, cx = 0, cy = 0, sx = 0, sy = 0; char cmd = 0;
#define NUM() (svg_num(&d))
#define LN(ax, ay, bx2, by2) bline(b, x + (ax) * k, y + (ay) * k, x + (bx2) * k, y + (by2) * k, w, col)
  while (*d) {
    while (*d == ' ' || *d == ',') d++;
    if (!*d) break;
    if ((*d >= 'A' && *d <= 'Z') || (*d >= 'a' && *d <= 'z')) cmd = *d++;
    float nx = cx, ny = cy;
    switch (cmd) {
      case 'M': case 'm': { float a = svg_num(&d), c = svg_num(&d); if (cmd == 'm') { a += cx; c += cy; } cx = sx = a; cy = sy = c; cmd = cmd == 'M' ? 'L' : 'l'; continue; }
      case 'L': case 'l': { float a = svg_num(&d), c = svg_num(&d); if (cmd == 'l') { a += cx; c += cy; } nx = a; ny = c; LN(cx, cy, nx, ny); break; }
      case 'H': case 'h': { float a = svg_num(&d); nx = cmd == 'h' ? cx + a : a; LN(cx, cy, nx, ny); break; }
      case 'V': case 'v': { float a = svg_num(&d); ny = cmd == 'v' ? cy + a : a; LN(cx, cy, nx, ny); break; }
      case 'C': case 'c': {
        float p[6]; for (int i = 0; i < 6; i++) p[i] = svg_num(&d);
        if (cmd == 'c') for (int i = 0; i < 6; i += 2) { p[i] += cx; p[i + 1] += cy; }
        float px = cx, py = cy;
        for (int i = 1; i <= 10; i++) { float t = i / 10.f, it = 1 - t, qx = it * it * it * cx + 3 * it * it * t * p[0] + 3 * it * t * t * p[2] + t * t * t * p[4], qy = it * it * it * cy + 3 * it * it * t * p[1] + 3 * it * t * t * p[3] + t * t * t * p[5]; LN(px, py, qx, qy); px = qx; py = qy; }
        nx = p[4]; ny = p[5]; break;
      }
      case 'A': case 'a': {
        float rx = svg_num(&d), ry = svg_num(&d), rot = svg_num(&d) * PI / 180, la = svg_num(&d), sw = svg_num(&d), ex = svg_num(&d), ey = svg_num(&d);
        if (cmd == 'a') { ex += cx; ey += cy; }
        rx = mfabs(rx); ry = mfabs(ry); if (rx < 1e-4f || ry < 1e-4f) { LN(cx, cy, ex, ey); nx = ex; ny = ey; break; }
        float cs = mcos(rot), sn = msin(rot), dx2 = (cx - ex) / 2, dy2 = (cy - ey) / 2, x1p = cs * dx2 + sn * dy2, y1p = -sn * dx2 + cs * dy2;
        float lam = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry); if (lam > 1) { float s = msqrt(lam); rx *= s; ry *= s; }
        float num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p, den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
        float co = den > 0 ? msqrt(maxf(0, num / den)) : 0; if ((la != 0) == (sw != 0)) co = -co;
        float cxp = co * rx * y1p / ry, cyp = -co * ry * x1p / rx, ccx = cs * cxp - sn * cyp + (cx + ex) / 2, ccy = sn * cxp + cs * cyp + (cy + ey) / 2;
        float t1 = matan2((y1p - cyp) / ry, (x1p - cxp) / rx), t2 = matan2((-y1p - cyp) / ry, (-x1p - cxp) / rx), dt = t2 - t1;
        if (sw != 0 && dt < 0) dt += TAU; else if (sw == 0 && dt > 0) dt -= TAU;
        int n = 4 + (int)(mfabs(dt) * 5); float px = cx, py = cy;
        for (int i = 1; i <= n; i++) { float t = t1 + dt * i / n, qx = ccx + rx * mcos(t) * cs - ry * msin(t) * sn, qy = ccy + rx * mcos(t) * sn + ry * msin(t) * cs; if (i == n) { qx = ex; qy = ey; } LN(px, py, qx, qy); px = qx; py = qy; }
        nx = ex; ny = ey; break;
      }
      case 'Z': case 'z': LN(cx, cy, sx, sy); nx = sx; ny = sy; break;
      default: d++; continue;
    }
    cx = nx; cy = ny;
  }
#undef NUM
#undef LN
}
/* =========================================================================
   FIELD — the arena itself pulls: a shallow spacetime basin toward the
   centre plus a tidal vector that turns once every eight bars.
   ========================================================================= */
static struct { float B, T, tx, ty, ang; } Field = { 46, 26, 0, 0, 0 };
static void field_update(double beat) { Field.ang = (float)(beat / 32) * TAU; Field.tx = mcos(Field.ang) * Field.T; Field.ty = msin(Field.ang) * Field.T; }
INL float field_ax(float x) { return (ARENA_W / 2 - x) / (ARENA_W / 2) * Field.B + Field.tx; }
INL float field_ay(float y) { return (ARENA_H / 2 - y) / (ARENA_W / 2) * Field.B + Field.ty; }

/* =========================================================================
   GRID — spring-mass membrane in true 3D (fixed 60 Hz, interpolated).
   Rest shape is a shallow bowl (the basin); wells sink funnels into it,
   blasts bulge it up toward the camera, and the tide sways it.
   ========================================================================= */
#define G_SP 30
#define G_COLS 61
#define G_ROWS 41
#define G_N (G_COLS * G_ROWS)
static struct {
  float px[G_N], py[G_N], pz[G_N], ox[G_N], oy[G_N], oz[G_N], vx[G_N], vy[G_N], vz[G_N], ax[G_N], ay[G_N], az[G_N];
  float dmp[G_N], rx[G_N], ry[G_N], rz[G_N], ak[G_N], ad[G_N];
  u16 sa[G_N * 2], sb[G_N * 2]; int ns;
  float acc, step, scale;
  float data[G_N * 4];
  u16 idx[G_N * 4];
} Grid;
static float gp_x[G_N], gp_y[G_N], gp_w[G_N];
static void grid_init(void) {
  Grid.ns = 0; Grid.scale = 0.4f; Grid.step = 1.f / 60;
  for (int r = 0; r < G_ROWS; r++) for (int c = 0; c < G_COLS; c++) {
    int i = r * G_COLS + c; float x = (float)c * G_SP, y = (float)r * G_SP, u = (x - ARENA_W / 2) / (ARENA_W / 2), v = (y - ARENA_H / 2) / (ARENA_H / 2);
    Grid.px[i] = Grid.rx[i] = Grid.ox[i] = x; Grid.py[i] = Grid.ry[i] = Grid.oy[i] = y;
    Grid.rz[i] = Grid.pz[i] = Grid.oz[i] = -26 * maxf(0, 1 - u * u) * maxf(0, 1 - v * v); Grid.dmp[i] = 0.98f;
    int border = c == 0 || r == 0 || c == G_COLS - 1 || r == G_ROWS - 1, maj = c % 5 == 0 && r % 5 == 0;
    Grid.ak[i] = border ? 0.1f : maj ? 0.006f : 0.0015f; Grid.ad[i] = border ? 0.1f : maj ? 0.03f : 0.01f;
    if (c < G_COLS - 1) { Grid.sa[Grid.ns] = (u16)i; Grid.sb[Grid.ns++] = (u16)(i + 1); }
    if (r < G_ROWS - 1) { Grid.sa[Grid.ns] = (u16)i; Grid.sb[Grid.ns++] = (u16)(i + G_COLS); }
  }
  int n = 0;
  for (int r = 0; r < G_ROWS; r++) for (int c = 0; c < G_COLS; c++) {
    int i = r * G_COLS + c;
    if (c < G_COLS - 1) { Grid.idx[n * 2] = (u16)i; Grid.idx[n * 2 + 1] = (u16)((i + 1) | (r % 5 == 0 ? 0x8000 : 0)); n++; }
    if (r < G_ROWS - 1) { Grid.idx[n * 2] = (u16)i; Grid.idx[n * 2 + 1] = (u16)((i + G_COLS) | (c % 5 == 0 ? 0x8000 : 0)); n++; }
  }
  gridData = Grid.data; gridCols = G_COLS; gridRows = G_ROWS; gridIdx = Grid.idx; gridN = n; gpx = gp_x; gpy = gp_y; gpw = gp_w;
}
static void grid_stepfn(void) {
  const float REST = G_SP * 0.95f, K = 0.28f, D = 0.06f;
  memcpy(Grid.ox, Grid.px, sizeof Grid.px); memcpy(Grid.oy, Grid.py, sizeof Grid.py); memcpy(Grid.oz, Grid.pz, sizeof Grid.pz);
  for (int s = 0; s < Grid.ns; s++) {
    int a = Grid.sa[s], b = Grid.sb[s];
    float dx = Grid.px[a] - Grid.px[b], dy = Grid.py[a] - Grid.py[b], dz = Grid.pz[a] - Grid.pz[b], L = msqrt(dx * dx + dy * dy + dz * dz);
    if (L <= REST) continue;
    float k = (L - REST) / L; dx *= k; dy *= k; dz *= k;
    float fx = K * dx - (Grid.vx[b] - Grid.vx[a]) * D, fy = K * dy - (Grid.vy[b] - Grid.vy[a]) * D, fz = K * dz - (Grid.vz[b] - Grid.vz[a]) * D;
    Grid.ax[a] -= fx; Grid.ay[a] -= fy; Grid.az[a] -= fz; Grid.ax[b] += fx; Grid.ay[b] += fy; Grid.az[b] += fz;
  }
  float tx = Field.tx * 0.00022f, ty = Field.ty * 0.00022f;
  for (int a = 0; a < G_N; a++) {
    float k = Grid.ak[a], d = Grid.ad[a];
    Grid.ax[a] -= k * (Grid.px[a] - Grid.rx[a]) + Grid.vx[a] * d - tx; Grid.ay[a] -= k * (Grid.py[a] - Grid.ry[a]) + Grid.vy[a] * d - ty; Grid.az[a] -= k * (Grid.pz[a] - Grid.rz[a]) + Grid.vz[a] * d;
  }
  for (int i = 0; i < G_N; i++) {
    float x = Grid.vx[i] + Grid.ax[i], y = Grid.vy[i] + Grid.ay[i], z = Grid.vz[i] + Grid.az[i] - (Grid.pz[i] - Grid.rz[i]) * 0.014f - Grid.vz[i] * 0.05f;
    float m2 = x * x + y * y + z * z;
    if (m2 > 900) { float k = 30 / msqrt(m2); x *= k; y *= k; z *= k; }
    Grid.px[i] += x; Grid.py[i] += y; Grid.pz[i] += z;
    Grid.ax[i] = Grid.ay[i] = Grid.az[i] = 0;
    float d = Grid.dmp[i];
    if (m2 < 1e-6f) x = y = z = 0;
    Grid.vx[i] = x * d; Grid.vy[i] = y * d; Grid.vz[i] = z * d; Grid.dmp[i] = 0.98f;
  }
}
static void grid_update(float dt) { Grid.acc += dt; int n = 0; while (Grid.acc >= Grid.step && n < 4) { grid_stepfn(); Grid.acc -= Grid.step; n++; } if (n == 4) Grid.acc = 0; }
#define GRID_RANGE(x, y, r) int c0 = clampi(mfloori(((x) - (r)) / G_SP) - 1, 0, G_COLS - 1), c1 = clampi(mfloori(((x) + (r)) / G_SP) + 2, 0, G_COLS - 1), r0 = clampi(mfloori(((y) - (r)) / G_SP) - 1, 0, G_ROWS - 1), r1 = clampi(mfloori(((y) + (r)) / G_SP) + 2, 0, G_ROWS - 1)
static void grid_directed(float fx, float fy, float fz, float x, float y, float radius) {
  float r2 = radius * radius, s = Grid.scale; GRID_RANGE(x, y, radius);
  for (int rr = r0; rr <= r1; rr++) for (int c = c0; c <= c1; c++) { int i = rr * G_COLS + c; float d2 = (Grid.px[i] - x) * (Grid.px[i] - x) + (Grid.py[i] - y) * (Grid.py[i] - y);
    if (d2 < r2) { float k = 10 * s / (10 + msqrt(d2) * 0.5f) * (1 - d2 / r2); Grid.ax[i] += fx * k; Grid.ay[i] += fy * k; Grid.az[i] += fz * k; } }
}
/** Gravity well: pull inward and sink the fabric into a funnel. */
static void grid_implode(float force, float x, float y, float radius) {
  float r2 = radius * radius, s = Grid.scale; GRID_RANGE(x, y, radius);
  for (int rr = r0; rr <= r1; rr++) for (int c = c0; c <= c1; c++) { int i = rr * G_COLS + c; float dx = x - Grid.px[i], dy = y - Grid.py[i], d2 = dx * dx + dy * dy;
    if (d2 < r2) { float f = 1 - d2 / r2, k = 10 * force * s / (400 + d2 * 0.25f) * f; Grid.ax[i] += dx * k * 0.5f; Grid.ay[i] += dy * k * 0.5f; Grid.az[i] -= k * 12 * f; Grid.dmp[i] *= 1 - 0.4f * f; } }
}
/** Blast outward; a source below the floor bulges the membrane up toward the viewer. */
static void grid_explode(float force, float x, float y, float radius, float z) {
  float r2 = radius * radius, s = Grid.scale; GRID_RANGE(x, y, radius);
  for (int rr = r0; rr <= r1; rr++) for (int c = c0; c <= c1; c++) { int i = rr * G_COLS + c; float dx = Grid.px[i] - x, dy = Grid.py[i] - y, dz = Grid.pz[i] - Grid.rz[i] - z, d2 = dx * dx + dy * dy;
    if (d2 < r2) { float f = 1 - d2 / r2, k = 200 * force * s / (40000 + d2 + dz * dz) * f * f; Grid.ax[i] += dx * k; Grid.ay[i] += dy * k; Grid.az[i] += dz * k; Grid.dmp[i] *= 1 - 0.4f * f; } }
}
#define GEXP(f, x, y, r) grid_explode(f, x, y, r, -30)
static void grid_reset(void) { for (int i = 0; i < G_N; i++) { Grid.px[i] = Grid.ox[i] = Grid.rx[i]; Grid.py[i] = Grid.oy[i] = Grid.ry[i]; Grid.pz[i] = Grid.oz[i] = Grid.rz[i]; Grid.vx[i] = Grid.vy[i] = Grid.vz[i] = 0; } }
static void grid_pack(void) {
  float a = Grid.acc / Grid.step; float *d = Grid.data;
  for (int i = 0; i < G_N; i++, d += 4) {
    float x = Grid.ox[i] + (Grid.px[i] - Grid.ox[i]) * a, y = Grid.oy[i] + (Grid.py[i] - Grid.oy[i]) * a, z = Grid.oz[i] + (Grid.pz[i] - Grid.oz[i]) * a;
    float dx = x - Grid.rx[i], dy = y - Grid.ry[i], dz = z - Grid.rz[i];
    d[0] = x; d[1] = y; d[2] = z; d[3] = minf(1, msqrt(dx * dx + dy * dy + dz * dz * 0.5f) / 30);
  }
}

/* =========================================================================
   PARTICLES — CPU pool, oldest recycled first. Curl turbulence comes from a
   pre-computed periodic flow field instead of per-particle noise.
   flags: 1 wells+field, 2 no wall bounce, 4 fade in, 8 curl, 16 gravity + floor bounce
   ========================================================================= */
#define PMAX 4096
typedef struct { float x, y, z, life, vx, vy, vz, maxl, w, streak, hx, hy; C3 c; u8 drag, flags; u16 seed; } Part;
static Part parts[PMAX]; static int pHead, pCap = PMAX, pLive;
static float partsBudget = 1;
static float flowX[64 * 64], flowY[64 * 64], dragTab[101];
static void parts_init(void) {
  /* periodic value noise on a 16×16 lattice → curl, sampled 4× per unit */
  for (int j = 0; j < 64; j++) for (int i = 0; i < 64; i++) {
    float x = i / 4.f, y = j / 4.f, e = 0.35f;
#define VNP(px, py) vn(mfmod(px + 64, 16), mfmod(py + 64, 16))
    float a = VNP(x, y + e), b = VNP(x, y - e), c = VNP(x + e, y), d = VNP(x - e, y);
#undef VNP
    flowX[j * 64 + i] = (a - b) / (2 * e); flowY[j * 64 + i] = (d - c) / (2 * e);
  }
}
static void emit(float x, float y, float z, float vx, float vy, float vz, float life, float r, float g, float b, float w, float drag, float streak, int flags) {
  Part *p = &parts[pHead]; pHead = (pHead + 1) % pCap;
  if (life > 2.9f) life = 2.9f;
  p->x = x; p->y = y; p->z = z; p->vx = vx; p->vy = vy; p->vz = vz; p->life = p->maxl = life; p->c = c3(r, g, b); p->w = w;
  p->drag = (u8)clampi(mlrint((drag - 0.5f) * 200), 0, 100); p->streak = streak; p->flags = (u8)flags; p->seed = (u16)rnd_u32();
  p->hx = (frand() - 0.5f) * 10; p->hy = (frand() - 0.5f) * 10;
}
static void parts_clear(void) { for (int i = 0; i < PMAX; i++) parts[i].life = 0; pHead = 0; }
static void parts_setCap(int n) { if (n == pCap) return; pCap = clampi(n, 256, PMAX); parts_clear(); }
typedef struct { float x, y, pr, pull, r; } WellU;
static void parts_update(float dt, float time, float bass, const WellU *wells, int nw) {
  if (dt <= 0) return;
  for (int i = 0; i <= 100; i++) dragTab[i] = mpow(0.5f + i * 0.005f, dt * 60);
  float fx = Field.B, ftx = Field.tx, fty = Field.ty, curlk = 240 * (0.35f + bass * 2.2f) * dt, t12 = time * 0.12f;
  int live = 0;
  for (int i = 0; i < pCap; i++) {
    Part *p = &parts[i]; if (p->life <= 0) continue;
    p->life -= dt; if (p->life <= 0) continue; live++;
    int fl = p->flags;
    if (fl & 1) {
      for (int k = 0; k < nw; k++) {
        float dx = wells[k].x - p->x, dy = wells[k].y - p->y, d2 = dx * dx + dy * dy;
        if (d2 < wells[k].pr * wells[k].pr && d2 > 1) { float dd = msqrt(d2), a = wells[k].pull * 2e7f / (d2 + 4000) * dt; p->vx += dx / dd * a - dy / dd * a * 0.35f; p->vy += dy / dd * a + dx / dd * a * 0.35f; if (dd < wells[k].r * 0.6f && p->life > 0.05f) p->life = 0.05f; }
      }
      p->vx += (ARENA_W * 0.5f - p->x) / (ARENA_W * 0.5f) * fx * dt + ftx * dt; p->vy += (ARENA_H * 0.5f - p->y) / (ARENA_W * 0.5f) * fx * dt + fty * dt;
    }
    if (fl & 8) {
      float u = (p->x * 0.0045f + t12) * 4, v = (p->y * 0.0045f + t12) * 4; int iu = mfloori(u), iv = mfloori(v); float fu = u - iu, fv = v - iv;
      int a = ((iv & 63) << 6) | (iu & 63), b = ((iv & 63) << 6) | ((iu + 1) & 63), c = (((iv + 1) & 63) << 6) | (iu & 63), d = (((iv + 1) & 63) << 6) | ((iu + 1) & 63);
      float cx = lerpf(lerpf(flowX[a], flowX[b], fu), lerpf(flowX[c], flowX[d], fu), fv), cy = lerpf(lerpf(flowY[a], flowY[b], fu), lerpf(flowY[c], flowY[d], fu), fv);
      p->vx += cx * curlk; p->vy += cy * curlk;
    }
    if (fl & 16) p->vz -= 900 * dt;
    float k = dragTab[p->drag]; p->vx *= k; p->vy *= k; p->vz *= k;
    float nx = p->x + p->vx * dt, ny = p->y + p->vy * dt, nz = p->z + p->vz * dt;
    if ((fl & 16) && nz < 0) { nz = -nz; p->vz = -p->vz * 0.45f; p->vx *= 0.82f; p->vy *= 0.82f; }
    if (!(fl & 2)) {
      if (nx < 0) { nx = -nx; p->vx = -p->vx * 0.6f; } else if (nx > ARENA_W) { nx = 2 * ARENA_W - nx; p->vx = -p->vx * 0.6f; }
      if (ny < 0) { ny = -ny; p->vy = -p->vy * 0.6f; } else if (ny > ARENA_H) { ny = 2 * ARENA_H - ny; p->vy = -p->vy * 0.6f; }
    }
    p->x = nx; p->y = ny; p->z = nz;
  }
  pLive = live;
}
static void parts_draw_b(const RBand *rb, const float *vp, float kick, float high, float time) {
  u32 tstep = (u32)(time * 24);
  for (int i = 0; i < pCap; i++) {
    Part *p = &parts[i]; if (p->life <= 0) continue;
    float t = clampf(p->life / maxf(p->maxl, 1e-3f), 0, 1), a = t < 0.5f ? t * 2 : 1; a *= a;
    if (p->flags & 4) a *= minf(1, (1 - t) * 6);
    float hx = p->x + p->hx * kick, hy = p->y + p->hy * kick;
    float sx, sy, w, tx, ty, tw;
    if (!project(vp, hx, hy, p->z, &sx, &sy, &w) || w < 1) continue;
    if (!project(vp, hx - p->vx * p->streak, hy - p->vy * p->streak, p->z - p->vz * p->streak, &tx, &ty, &tw)) { tx = sx; ty = sy; }
    u32 hsh = ((u32)p->seed + tstep) * 2654435761u;
    float sparkle = 1 + high * 1.2f * ((hsh >> 24) > 153 ? 1 : 0);
    float wpx = maxf(1, p->w * (0.5f + 0.5f * t) / (w * CamPxk)), k = a * sparkle;
    if ((sy * iYS < rb->y0 - 3 && ty * iYS < rb->y0 - 3) || (sy * iYS > rb->y1 + 2 && ty * iYS > rb->y1 + 2)) continue;
    int cr = cvt(mlrint(p->c.r * k * 256)), cg = cvt(mlrint(p->c.g * k * 256)), cb = cvt(mlrint(p->c.b * k * 256));
    if (!(cr | cg | cb)) continue;
    rs_wu_b(rb, sx, sy, tx, ty, cr, cg, cb, wpx, wpx * 0.4f);
  }
}
static void parts_add(float x, float y, float z, float vx, float vy, float vz, float life, C3 c, float w, float drag, float streak, int flags) { emit(x, y, z, vx, vy, vz, life, c.r, c.g, c.b, w, drag, streak, flags); }
typedef struct { float angle, spread, vary, jit, vx, vy, streak, w, drag, z, up0, up1; int flags, hasAngle, hasSpread, hasUp, hasFlags, hasZ, hasStreak, hasW, hasDrag; } BurstO;
static void burst(float X, float Y, float n, C3 col, float smin, float smax, float lmin, float lmax, const BurstO *o) {
  static const BurstO D0; if (!o) o = &D0;
  int cnt = mlrint(n * partsBudget);
  float a0 = o->hasAngle ? o->angle : 0, spread = o->hasSpread ? o->spread : TAU, z = o->hasZ ? o->z : LIFT, streak = o->hasStreak ? o->streak : 0.035f;
  int flags = o->hasFlags ? o->flags : 9;
  for (int k = 0; k < cnt; k++) {
    float a = a0 + (frand() - 0.5f) * spread, s = rndr(smin, smax), hue = o->vary ? 1 + (frand() - 0.5f) * o->vary : 1, vz = o->hasUp ? rndr(o->up0, o->up1) : 0;
    emit(X + (o->jit ? rndr(-o->jit, o->jit) : 0), Y + (o->jit ? rndr(-o->jit, o->jit) : 0), z, mcos(a) * s + o->vx, msin(a) * s + o->vy, vz, rndr(lmin, lmax),
      col.r * hue, col.g * hue, col.b * hue, o->hasW ? o->w : 1.3f, o->hasDrag ? o->drag : 0.93f, streak, flags);
  }
}
/* burst option helpers */
static BurstO BO(void) { BurstO b; mzero(&b, sizeof b); return b; }
static BurstO BOang(float a, float spread) { BurstO b = BO(); b.hasAngle = 1; b.angle = a; b.hasSpread = 1; b.spread = spread; return b; }
static BurstO BOvary(float v) { BurstO b = BO(); b.vary = v; return b; }
static BurstO BOup(float u0, float u1, int flags) { BurstO b = BO(); b.hasUp = 1; b.up0 = u0; b.up1 = u1; b.hasFlags = 1; b.flags = flags; return b; }
/** Explosion: flat turbulent sparks + 3D debris that bounces on the floor, tinted across the vapor palette. */
static void blast(float x, float y, float n, C3 col, float speed, float life) {
  BurstO o = BOvary(0.4f); o.hasW = 1; o.w = 1.4f; burst(x, y, n, col, 120, speed, 0.35f, life, &o);
  o = BO(); o.hasW = 1; o.w = 1.1f; burst(x, y, n * 0.35f, c3(1.4f, 1.4f, 1.4f), 200, speed * 1.3f, 0.15f, 0.35f, &o);
  o = BOup(150, 520, 17); o.hasW = 1; o.w = 1.2f; o.hasDrag = 1; o.drag = 0.97f; o.hasStreak = 1; o.streak = 0.025f; burst(x, y, n * 0.4f, col, 60, speed * 0.6f, 0.6f, life * 1.6f, &o);
  if (n > 30) { o = BOvary(0.3f); o.hasW = 1; o.w = 1.2f; burst(x, y, n * 0.25f, c3(1.2f, 0.35f, 1.0f), 80, speed * 0.7f, 0.4f, life, &o); }
}

/* =========================================================================
   FX — rings, refraction shockwaves, floating text
   ========================================================================= */
typedef struct { float x, y, z, r0, r1, t, dur, w; C3 col; int seg, ease; } Ring;
typedef struct { float x, y, maxR, t, dur, str; } WaveFx;
typedef struct { float x, y, size, t, dur, vy; C3 col; char str[40]; } TextFx;
static Ring fxRings[256]; static int nRings;
static WaveFx fxWaves[6]; static int nWaves;
static TextFx fxTexts[40]; static int nTexts;
static void fx_ring2(float x, float y, float r0, float r1, float dur, C3 col, float w, int seg, int ease, float z) {
  if (nRings >= ARR(fxRings)) { memmove(fxRings, fxRings + 1, sizeof(Ring) * (ARR(fxRings) - 1)); nRings--; }
  Ring *r = &fxRings[nRings++]; r->x = x; r->y = y; r->z = z; r->r0 = r0; r->r1 = r1; r->t = 0; r->dur = dur; r->col = col; r->w = w; r->seg = seg; r->ease = ease;
}
#define fx_ring(x, y, r0, r1, dur, col, w, seg) fx_ring2(x, y, r0, r1, dur, col, w, seg, 1, LIFT)
static void fx_wave(float x, float y, float maxR, float dur, float str) { if (nWaves < 6) { WaveFx *w = &fxWaves[nWaves++]; w->x = x; w->y = y; w->maxR = maxR; w->t = 0; w->dur = dur; w->str = str; } }
static void fx_text(float x, float y, const char *s, float size, C3 col, float dur) {
  if (nTexts >= 40) { memmove(fxTexts, fxTexts + 1, sizeof(TextFx) * 39); nTexts--; }
  TextFx *t = &fxTexts[nTexts++]; t->x = x; t->y = y; t->size = size; t->col = col; t->t = 0; t->dur = dur; t->vy = -40;
  int i = 0; while (s[i] && i < 39) { t->str[i] = s[i]; i++; } t->str[i] = 0;
}
static void fx_update(float dt) {
  int j = 0; for (int i = 0; i < nRings; i++) { fxRings[i].t += dt; if (fxRings[i].t < fxRings[i].dur) fxRings[j++] = fxRings[i]; } nRings = j;
  j = 0; for (int i = 0; i < nWaves; i++) { fxWaves[i].t += dt; if (fxWaves[i].t < fxWaves[i].dur) fxWaves[j++] = fxWaves[i]; } nWaves = j;
  j = 0; for (int i = 0; i < nTexts; i++) { fxTexts[i].t += dt; fxTexts[i].y += fxTexts[i].vy * dt; if (fxTexts[i].t < fxTexts[i].dur) fxTexts[j++] = fxTexts[i]; } nTexts = j;
}
static void fx_clear(void) { nRings = nWaves = nTexts = 0; }
static void fx_drawWorld(Batch *b) {
  for (int i = 0; i < nRings; i++) {
    Ring *r = &fxRings[i]; float k = r->t / r->dur, e = r->ease ? easeOutCubic(k) : k, a = (1 - k) * (1 - k);
    b->z = r->z; bcircle(b, r->x, r->y, lerpf(r->r0, r->r1, e), r->seg, r->w * (1 - k * 0.5f), cmul(r->col, a), 0);
  }
  b->z = LIFT;
}

/* =========================================================================
   CAMERA — perspective over the floor. Play is straight top-down; the
   title looks out to the horizon, a run starts with a dive down to
   top-down, and death lifts the camera back toward the sunset.
   ========================================================================= */
static struct {
  float x, y, viewW, viewH, zoom, kick, trauma, rot, sx, sy, t, tilt, fov, D, lx, ly;
  float view[16], proj[16], vp[16], inv[16], refl[16], pxk, eye[3];
} Cam = { ARENA_W / 2, ARENA_H / 2, 1600, 1000, 1, 0, 0, 0, 0, 0, 0, 1.4f, 50 * PI / 180, 1000, 0, 0 };
static void cam_shake(float a) { Cam.trauma = minf(1.2f, Cam.trauma + a * (Set.shake / 7.f)); }
static void cam_punch(float z) { Cam.kick = maxf(Cam.kick, z); }
static void cam_fit(void) { float aspect = (float)OW / OH, vh = 930; if (vh * aspect < 960) vh = 960 / aspect; Cam.viewH = minf(vh, 1900); Cam.viewW = Cam.viewH * aspect; }
static void cam_update(float dt, float tx, float ty, float lead, int clampView) {
  Cam.t += dt;
  float vw = Cam.viewW / Cam.zoom, vh = Cam.viewH / Cam.zoom * 0.9f, M = 70;
#define CA(v, view, size) ((view) >= (size) + M * 2 ? (size) / 2 : clampf(v, (view) / 2 - M, (size) - (view) / 2 + M))
  float gx = clampView ? CA(tx, vw, ARENA_W) : tx, gy = clampView ? CA(ty, vh, ARENA_H) : ty;
#undef CA
  Cam.x = damp(Cam.x, gx, lead, dt); Cam.y = damp(Cam.y, gy, lead, dt);
  Cam.kick = damp(Cam.kick, 0, 7, dt);
  Cam.trauma = maxf(0, Cam.trauma - dt * 1.4f);
  float s = Cam.trauma * Cam.trauma, t = Cam.t * 38;
  Cam.sx = (msin(t * 1.13f) + msin(t * 2.71f + 1.3f) * 0.5f) * 22 * s;
  Cam.sy = (mcos(t * 1.37f + 0.4f) + msin(t * 2.33f + 2.1f) * 0.5f) * 22 * s;
  Cam.rot = msin(t * 0.91f + 3.3f) * 0.022f * s;
}
static void cam_build(void) {
  Cam.fov = lerpf(36, 50, clampf(Cam.tilt / 1.4f, 0, 1)) * PI / 180;
  float z = Cam.zoom * (1 + Cam.kick), th = Cam.tilt, aspect = (float)OW / OH;
  float D = Cam.D = (Cam.viewH / 2) / mtan(Cam.fov / 2) / z;
  float tx = Cam.x + Cam.sx, ty = Cam.y + Cam.sy;
  float bx = msin(Cam.lx), by = msin(th) + msin(Cam.ly), bz = mcos(th), bl = msqrt(bx * bx + by * by + bz * bz); bx /= bl; by /= bl; bz /= bl;
  float *e = Cam.eye; e[0] = tx + bx * D; e[1] = ty + by * D; e[2] = bz * D;
  float d0 = -by, ux = -bx * d0, uy = -1 - by * d0, uz = -bz * d0, ul = msqrt(ux * ux + uy * uy + uz * uz); ux /= ul; uy /= ul; uz /= ul;
  float rx = by * uz - bz * uy, ry = bz * ux - bx * uz, rz = bx * uy - by * ux;
  float cr = mcos(Cam.rot), sr = msin(Cam.rot);
  float R[3] = { rx * cr + ux * sr, ry * cr + uy * sr, rz * cr + uz * sr }, U[3] = { -rx * sr + ux * cr, -ry * sr + uy * cr, -rz * sr + uz * cr }, Bk[3] = { bx, by, bz };
  m4_basis(Cam.view, R, U, Bk, e);
  m4_perspective(Cam.proj, Cam.fov, aspect, maxf(5, D * 0.05f), D * 60 + 30000);
  m4_mul(Cam.vp, Cam.proj, Cam.view);
  m4_invert(Cam.inv, Cam.vp);
  memcpy(Cam.refl, Cam.vp, 64); for (int i = 8; i < 12; i++) Cam.refl[i] = -Cam.vp[i];
  Cam.pxk = 2 * mtan(Cam.fov / 2) / OH;
  CamPxk = Cam.pxk; memcpy(CamVP, Cam.vp, 64); memcpy(CamInv, Cam.inv, 64); memcpy(CamEye, Cam.eye, 12);
}
/** Screen (output px) → point on the plane z. */
static void cam_toWorld(float sx, float sy, float z, float *wx, float *wy) {
  float nx = sx / OW * 2 - 1, ny = 1 - sy / OH * 2, a[4], b[4];
  m4_apply(Cam.inv, nx, ny, -1, a); m4_apply(Cam.inv, nx, ny, 1, b);
  float ox = a[0] / a[3], oy = a[1] / a[3], oz = a[2] / a[3], dx = b[0] / b[3] - ox, dy = b[1] / b[3] - oy, dz = b[2] / b[3] - oz;
  float t = mfabs(dz) > 1e-6f ? (z - oz) / dz : 0; if (t < 0) t = 0;
  *wx = ox + dx * t; *wy = oy + dy * t;
}
/** World → screen (output px); 0 when behind the camera. */
static int cam_toScreen(float x, float y, float z, float *sx, float *sy) {
  float c[4]; m4_apply(Cam.vp, x, y, z, c); if (c[3] < 1) return 0;
  *sx = (c[0] / c[3] + 1) / 2 * OW; *sy = (1 - c[1] / c[3]) / 2 * OH; return 1;
}
/** World → HUD units (UI space without the menu origin). */
static int cam_toUI(float x, float y, float z, float *ux, float *uy) { if (!cam_toScreen(x, y, z, ux, uy)) return 0; *ux /= US; *uy /= US; return 1; }
static int cam_waves(Wave *out) {
  int n = 0;
  for (int i = 0; i < nWaves; i++) {
    WaveFx *w = &fxWaves[i]; float k = w->t / w->dur, r = easeOutCubic(k) * w->maxR, ax, ay, bx, by;
    if (!cam_toScreen(w->x, w->y, 0, &ax, &ay) || !cam_toScreen(w->x + r, w->y, 0, &bx, &by)) continue;
    out[n].x = ax / OW; out[n].y = 1 - ay / OH; out[n].r = mfabs(bx - ax) / OH; out[n].s = w->str * (1 - k) * (1 - k); n++;
  }
  return n;
}
/* =========================================================================
   GAME STATE + ENTITIES
   ========================================================================= */
static const C3 COL_player = { 0.85f, 1.0f, 1.05f }, COL_bullet = { 1.0f, 0.82f, 0.45f }, COL_accent = { 1.0f, 0.35f, 0.95f }, COL_geom = { 0.35f, 1.0f, 0.82f },
  COL_ebullet = { 1.0f, 0.28f, 0.6f }, COL_heal = { 0.35f, 1.0f, 0.7f }, COL_bomb = { 1.0f, 0.6f, 0.25f }, COL_shield = { 0.45f, 0.85f, 1.0f },
  COL_perfect = { 1.0f, 0.88f, 0.45f }, COL_arc = { 0.65f, 0.85f, 1.0f }, COL_boom = { 1.0f, 0.5f, 0.3f }, COL_missile = { 1.0f, 0.45f, 0.4f }, COL_orbit = { 0.55f, 1.0f, 0.95f };
static const float SH_player[] = { 19, 0, -8, 12.5f, -14, 9, -7, 3.5f, -11, 0, -7, -3.5f, -14, -9, -8, -12.5f };
static const float SH_diamond[] = { 1, 0, 0, 0.62f, -1, 0, 0, -0.62f };
static const float SH_tri[] = { 1, 0, -0.72f, 0.78f, -0.72f, -0.78f };
static const float SH_chevron[] = { 1, 0, -0.85f, 0.8f, -0.35f, 0, -0.85f, -0.8f };
static const float SH_dart[] = { 1.25f, 0, -0.8f, 0.5f, -0.35f, 0, -0.8f, -0.5f };
static const float SH_bowtie[] = { 0.9f, 0.75f, 0.9f, -0.75f, -0.9f, 0.75f, -0.9f, -0.75f };
static const float SH_spear[] = { 1.15f, 0, 0.15f, 0.72f, -0.85f, 0.42f, -0.6f, 0, -0.85f, -0.42f, 0.15f, -0.72f };
#define POLY(b, S, x, y, a, sc, w, c) bpoly(b, S, ARR(S) / 2, x, y, a, sc, w, c, 1)

/* ENERGY — dashes spend it, bullet time drains it; it refills over time and on kills. */
#define E_MAX 100.f
#define E_REGEN 11.f
#define DASH_COST 25.f
#define DASH_CD 0.22f
#define BT_DRAIN 32.f
#define BT_MIN 15.f
typedef struct { const char *name; int key; float win; C3 col; float refund, R, dmg, inv; } DashTier;
static const DashTier DASH_TIERS[3] = {
  { "PERFECT", 0, 0.040f, { 1.6f, 1.4f, 0.5f }, 35, 175, 4, 0.45f },
  { "GREAT", 1, 0.080f, { 0.5f, 1.4f, 1.7f }, 18, 115, 2.5f, 0.34f },
  { "GOOD", 2, 0.125f, { 1.1f, 0.6f, 1.6f }, 8, 70, 1.5f, 0.28f }
};
typedef struct {
  int fireSub, spread, pierce, bounce, homing, rear, orbit, chain, boom, shield, maxHp, dashDmg, perfect, bombsMax, missile, nova, accent;
  float dmg, magnet, dashCd, speed, keep;
  int hpBase, bombBase; float dmgBase, speedBase;
} Stats;
static Stats baseStats(const u8 *m) {
  Stats s; mzero(&s, sizeof s);
  s.hpBase = 3 + m[0]; s.bombBase = 3 + m[1]; s.dmgBase = 1 + 0.06f * m[2]; s.speedBase = 1 + 0.04f * m[3];
  s.fireSub = 4; s.dmg = s.dmgBase; s.maxHp = s.hpBase; s.magnet = 1; s.dashCd = DASH_CD; s.bombsMax = s.bombBase; s.speed = s.speedBase; s.keep = 0.5f + 0.1f * m[4];
  return s;
}
typedef struct { float x, y, a, t; } Trail;
typedef struct {
  float x, y, vx, vy, r, hitR, energy, ang, aim, inv, dashT, dashCd, ddx, ddy, deadT;
  int hp, bombs, shield, shieldBars, alive, dashGen;
  Trail trail[24]; int ntrail;
} Player;

enum { ET_MOTE, ET_DART, ET_FLICKER, ET_LUNGER, ET_CLUSTER, ET_SHARD, ET_GNAT, ET_BEACON, ET_BOLA, ET_VORTEX, ET_MORTAR, ET_N };
typedef struct { float hp, r, score, geoms; C3 col; u8 bounce, big, isWell; } EType;
static const EType ETS[ET_N] = {
  { 1, 16, 25, 1, { 0.78f, 0.5f, 1.0f }, 1, 0, 0 }, { 2, 15, 50, 1, { 0.2f, 0.75f, 1.0f }, 0, 0, 0 }, { 2, 16, 100, 2, { 0.3f, 1.0f, 0.62f }, 0, 0, 0 },
  { 3, 18, 100, 2, { 1.0f, 0.52f, 0.2f }, 0, 0, 0 }, { 5, 22, 75, 2, { 1.0f, 0.28f, 0.72f }, 0, 0, 0 }, { 1, 9, 30, 1, { 1.0f, 0.52f, 0.85f }, 0, 0, 0 },
  { 1, 9, 20, 0.5f, { 1.0f, 0.35f, 0.42f }, 0, 0, 0 }, { 8, 22, 150, 3, { 0.18f, 1.0f, 0.88f }, 0, 0, 0 }, { 10, 10, 250, 4, { 1.0f, 0.82f, 0.3f }, 0, 1, 0 },
  { 18, 26, 300, 5, { 1.0f, 0.22f, 0.4f }, 0, 0, 1 }, { 9, 20, 200, 3, { 0.72f, 1.0f, 0.32f }, 0, 1, 0 }
};
typedef struct {
  u32 id; u8 type, dead; int phase, bombGen, dashGen;
  float x, y, vx, vy, r, hp, maxHp, ang, t, spawn, spawnMax, flash, seed, score, geoms, orbitT;
  C3 col;
  float juke, cool, dashing, orbit, aim, len, spin, absorbed, pr, recoil;
} Enemy;
typedef struct { float x, y, px, py, vx, vy, dmg, r, life; int pierce, bounce; u8 accent, drone; u32 hit[6]; int nhit; } Bullet;
typedef struct { float x, y, vx, vy, r, life, t; C3 col; } EBullet;
typedef struct { float x0, y0, x1, y1, h, x, y, z, R, f; double b0; int beats, done; C3 col; } Shell;
enum { PK_GEOM, PK_HEAL, PK_BOMB };
typedef struct { int type; float x, y, vx, vy, t, life, ang; } Pickup;
typedef struct { float x, y, vx, vy, t, life, dmg, delay; u32 target; } Missile;
typedef struct { float pts[14]; float t, dur; } Bolt;
typedef struct { float x, y, t, r, dmg; C3 col; int done; } Boom;
typedef struct { float x, y, r, pr, pull, ppull, bpull; int boss; } Well;
typedef struct { char text[48], sub[64]; float t, dur; C3 col; } Banner;
typedef struct { float t, out; int kind, idx, rush, k; char name[32], sig[40]; C3 col; } Warning;

#define MAXE 320
#define MAXB 1200
#define MAXEB 760
enum { GS_BOOT, GS_TITLE, GS_PLAY, GS_UPGRADE, GS_OVER };
static struct {
  int state, paused; float time, timeScale, slowT, slowTo, simDt;
  Player p; Stats st; int upg[24];
  Enemy en[MAXE]; int nen; u32 nextId; int bombGen;
  Bullet bu[MAXB]; int nbu;
  EBullet eb[MAXEB]; int neb;
  Shell sh[64]; int nsh;
  Pickup pk[260]; int npk;
  Missile mi[64]; int nmi;
  Bolt bo[64]; int nbo;
  Boom bm[128]; int nbm;
  Well wells[16]; int nwells;
  struct { float x, y, r; int bossHit, gen, on; } bombWave;
  i64 score, hiScore; float shownScore; int mult, wave, kills, maxMult, perfects, chain; float chainT, playTime;
  i64 lastTick; int hasLastTick;
  struct { float speed, hp; } diff;
  int god, demo, won, bossRush, banked, bossBeaten, afterBoss, draftAfterBoss, rerolls, startedAt, tutorial;
  float flash; C3 flashCol; float aberr, danger, multPulse, scorePulse, beatPulse, hitstopT, noBombMsg, overT, titleT, clearT, bt, demoBomb;
  double visBeat; int btOn; i64 demoBar;
  Banner banner; int hasBanner;
  Warning warn; int hasWarn;
  int swarm[MAXE]; int nswarm;
  float tiltGoal;
  int palIdx; float pal[PALN][3];
} Game;
static float dashAge;  /* seconds since the dash input arrived (judged against the beat as heard then) */

/* forward declarations: guardians, flow, sound */
static int boss_damage(float dmg, int cause);
static int boss_damagePart(int part, float dmg, int cause);
static int boss_live(void);
static int boss_targetable(void);
static int boss_exists(void);
static int boss_pos(float *x, float *y, float *r);
static float boss_maxHp(void);
static void boss_hits(void);
static int boss_wellInfo(Well *w);
static void game_onPlayerDeath(void);
enum { CA_SHOT, CA_ARC, CA_BOMB, CA_DASH, CA_ORBIT, CA_PERFECT, CA_NOVA, CA_MISSILE, CA_BOOM, CA_OVERLOAD, CA_SHOCK, CA_PART, CA_DEBUG };

/* ---------------- hittables + spatial hash ---------------- */
enum { HF_VULN = 1, HF_SOLID = 2, HF_HARM = 4 };
#define BOSS_ID 0xFFFFFF00u
typedef struct { float x, y, r; int boss; Enemy *e; int part, f; u32 stamp; } Hit;
#define MAXHIT 1600
#define HC 18
#define HR 12
static Hit hits[MAXHIT]; static int nhits;
static int hcell[HC * HR]; static struct { int hit, next; } hent[MAXHIT * 4]; static int nhent; static u32 hstamp;
static void hit_add(float x, float y, float r, Enemy *e, int boss, int part, int f) {
  if (nhits >= MAXHIT) return; Hit *h = &hits[nhits++]; h->x = x; h->y = y; h->r = r; h->e = e; h->boss = boss; h->part = part; h->f = f; h->stamp = 0;
}
static void hash_build(void) {
  for (int i = 0; i < HC * HR; i++) hcell[i] = -1;
  nhent = 0;
  for (int i = 0; i < nhits; i++) {
    Hit *h = &hits[i];
    int c0 = clampi(mfloori((h->x - h->r) / 100), 0, HC - 1), c1 = clampi(mfloori((h->x + h->r) / 100), 0, HC - 1), r0 = clampi(mfloori((h->y - h->r) / 100), 0, HR - 1), r1 = clampi(mfloori((h->y + h->r) / 100), 0, HR - 1);
    for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) { if (nhent >= ARR(hent)) return; hent[nhent].hit = i; hent[nhent].next = hcell[r * HC + c]; hcell[r * HC + c] = nhent++; }
  }
}
typedef int (*HitFn)(Hit *h, void *ctx);
static void hash_query(float x0, float y0, float x1, float y1, HitFn fn, void *ctx) {
  u32 q = ++hstamp;
  int c0 = clampi(mfloori(minf(x0, x1) / 100), 0, HC - 1), c1 = clampi(mfloori(maxf(x0, x1) / 100), 0, HC - 1), r0 = clampi(mfloori(minf(y0, y1) / 100), 0, HR - 1), r1 = clampi(mfloori(maxf(y0, y1) / 100), 0, HR - 1);
  for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++)
    for (int k = hcell[r * HC + c]; k >= 0; k = hent[k].next) { Hit *h = &hits[hent[k].hit]; if (h->stamp == q) continue; h->stamp = q; if (!fn(h, ctx)) return; }
}
INL int hit_dead(const Hit *h) { return h->boss ? !boss_live() : h->e->dead; }
INL u32 hit_id(const Hit *h) { return h->boss ? BOSS_ID : h->e->id; }

/* ---------------- enemies ---------------- */
static Enemy *spawnEnemy(int type, float x, float y, float spawn);
static void killEnemy(Enemy *e, int cause);
static void enemyShot(float x, float y, float a, float spd, float r, C3 col);
static void addScore(i64 n);
static void spawnPickup(int type, float x, float y);
static Enemy *findEnemy(u32 id) { for (int i = 0; i < Game.nen; i++) if (Game.en[i].id == id) return &Game.en[i]; return NULL; }
INL void towardPlayer(const Enemy *e, float *tx, float *ty, float *d) { float dx = Game.p.x - e->x, dy = Game.p.y - e->y, m = mhypot(dx, dy); if (m == 0) m = 1; *tx = dx / m; *ty = dy / m; *d = m; }
static void steer(Enemy *e, float tx, float ty, float maxSpd, float accel, float dt) {
  float dvx = tx * maxSpd - e->vx, dvy = ty * maxSpd - e->vy, m = mhypot(dvx, dvy);
  if (m > 0) { float k = minf(1, accel * dt / m); e->vx += dvx * k; e->vy += dvy * k; }
}
/** 0→1 over the off-beat before an enemy's attack beat (telegraph). */
static float chargeOf(const Enemy *e) { float ph = mfmod((float)Game.visBeat + e->phase, 2); if (ph < 0) ph += 2; return ph > 1 ? ph - 1 : 0; }
static void bola_orbs(const Enemy *e, float *o) { float ca = mcos(e->ang) * e->len, sa = msin(e->ang) * e->len; o[0] = e->x + ca; o[1] = e->y + sa; o[2] = e->x - ca; o[3] = e->y - sa; }

static void enemy_init(Enemy *e) {
  switch (e->type) {
    case ET_MOTE: { float a = rnd(TAU); e->vx = mcos(a) * 80; e->vy = msin(a) * 80; break; }
    case ET_FLICKER: e->juke = 0; e->cool = 0; break;
    case ET_LUNGER: e->dashing = 0; break;
    case ET_BEACON: e->orbit = chance(0.5f) ? 1 : -1; e->aim = 0; break;
    case ET_BOLA: e->len = 72; e->spin = 2.0f * (chance(0.5f) ? 1 : -1); break;
    case ET_VORTEX: e->absorbed = 0; e->pr = 430; e->vx = rndr(-12, 12); e->vy = rndr(-12, 12); e->ang = 0; break;
    case ET_MORTAR: e->orbit = chance(0.5f) ? 1 : -1; e->aim = 0; e->recoil = 0; break;
  }
}
static void enemy_update(Enemy *e, float dt) {
  float tx, ty, td; towardPlayer(e, &tx, &ty, &td);
  float spd = Game.diff.speed;
  switch (e->type) {
    case ET_MOTE: {
      e->ang += dt * 2.2f;
      float a = matan2(e->vy, e->vx) + msin(e->t * 1.1f + e->seed) * 0.9f * dt, s = 85 * spd;
      e->vx = damp(e->vx, mcos(a) * s, 3, dt); e->vy = damp(e->vy, msin(a) * s, 3, dt); break;
    }
    case ET_DART: steer(e, tx, ty, 205 * spd, 380, dt); e->ang = matan2(e->vy, e->vx); break;
    case ET_FLICKER: {
      steer(e, tx, ty, 175 * spd, 300, dt); e->ang += dt * 1.5f;
      e->cool -= dt; e->juke -= dt;
      if (e->cool > 0 && e->juke <= 0) break;
      float ex = 0, ey = 0;
      for (int i = 0; i < Game.nbu; i++) {
        Bullet *b = &Game.bu[i]; float dx = e->x - b->x, dy = e->y - b->y; if (dx * dx + dy * dy > 170 * 170) continue;
        float bv = mhypot(b->vx, b->vy); if (bv == 0) bv = 1; float ux = b->vx / bv, uy = b->vy / bv;
        if (dx * ux + dy * uy < 0) continue;
        float perp = dx * -uy + dy * ux;
        if (mfabs(perp) < 40) { float s = perp >= 0 ? 1 : -1; ex += -uy * s; ey += ux * s; }
      }
      if (ex != 0 || ey != 0) { if (e->juke <= 0) { e->juke = 0.28f; e->cool = 1.1f; } float m = mhypot(ex, ey); e->vx += ex / m * 2400 * dt; e->vy += ey / m * 2400 * dt; }
      float sp = mhypot(e->vx, e->vy); if (sp > 420) { e->vx *= 420 / sp; e->vy *= 420 / sp; }
      break;
    }
    case ET_LUNGER:
      if (e->dashing > 0) { e->dashing -= dt; float k = mexp(-3.2f * dt); e->vx *= k; e->vy *= k; }
      else { steer(e, tx, ty, 45, 200, dt); e->ang = lerpAngle(e->ang, matan2(ty, tx), minf(1, dt * 8)); }
      break;
    case ET_CLUSTER: steer(e, tx, ty, 105 * spd, 200, dt); e->ang += dt * 0.9f; break;
    case ET_SHARD: { float w = msin(e->t * 9 + e->seed) * 0.6f, c = mcos(w), s = msin(w); steer(e, tx * c - ty * s, tx * s + ty * c, 290 * spd, 700, dt); e->ang += dt * 7; break; }
    case ET_GNAT: {
      float sx = 0, sy = 0, ax = 0, ay = 0, cx = 0, cy = 0; int n = 0;
      for (int i = 0; i < Game.nswarm; i++) {
        Enemy *o = &Game.en[Game.swarm[i]]; if (o == e) continue;
        float dx = o->x - e->x, dy = o->y - e->y, d2 = dx * dx + dy * dy;
        if (d2 > 110 * 110) continue;
        n++; ax += o->vx; ay += o->vy; cx += dx; cy += dy;
        if (d2 < 900 && d2 > 0.01f) { float d = msqrt(d2); sx -= dx / d * (30 - d); sy -= dy / d * (30 - d); }
        if (n > 12) break;
      }
      float gx = tx, gy = ty;
      if (n) { gx += ax / n / 300 * 0.6f + cx / n / 110 * 0.4f + sx * 0.06f; gy += ay / n / 300 * 0.6f + cy / n / 110 * 0.4f + sy * 0.06f; }
      float m = mhypot(gx, gy); if (m == 0) m = 1;
      steer(e, gx / m, gy / m, 255 * spd, 520, dt); e->ang = matan2(e->vy, e->vx); break;
    }
    case ET_BEACON: {
      float radial = clampf((td - 360) / 120, -1, 1), gx = tx * radial - ty * e->orbit * 0.6f, gy = ty * radial + tx * e->orbit * 0.6f, m = mhypot(gx, gy); if (m == 0) m = 1;
      steer(e, gx / m, gy / m, 110 * spd * minf(1, m), 160, dt);
      e->ang += dt * 0.7f; e->aim = lerpAngle(e->aim, matan2(ty, tx), minf(1, dt * 5));
      if (e->x < 60 || e->x > ARENA_W - 60 || e->y < 60 || e->y > ARENA_H - 60) { e->orbit *= -1; e->x = clampf(e->x, 61, ARENA_W - 61); e->y = clampf(e->y, 61, ARENA_H - 61); }
      break;
    }
    case ET_BOLA:
      steer(e, tx, ty, 115 * spd, 170, dt);
      e->ang += e->spin * (0.7f + 2.2f * Game.beatPulse) * dt;
      e->len = damp(e->len, 72 + 16 * msin(e->t * 1.3f + e->seed), 3, dt); break;
    case ET_VORTEX: {
      e->r = 26 + e->absorbed * 2.2f;
      e->ang += dt * (1.4f + e->absorbed * 0.15f);
      float k = mexp(-0.2f * dt); e->vx *= k; e->vy *= k;
      grid_implode(18 + msin(e->t * 3) * 6 + e->absorbed * 2, e->x, e->y, 260);
      for (int i = 0; i < Game.nen; i++) {
        Enemy *o = &Game.en[i];
        if (o == e || o->dead || ETS[o->type].isWell || o->spawn > 0) continue;
        float dx = e->x - o->x, dy = e->y - o->y, d2 = dx * dx + dy * dy;
        if (d2 >= e->pr * e->pr) continue;
        float d = msqrt(d2); if (d == 0) d = 1; float a = 520 * (1 - d / e->pr) * dt;
        o->vx += dx / d * a; o->vy += dy / d * a;
        if (d < e->r + o->r * 0.3f) {
          o->dead = 1; e->absorbed++; e->hp += 3; e->maxHp += 3;
          burst(o->x, o->y, 14, o->col, 50, 200, 0.3f, 0.6f, NULL);
          fx_ring(e->x, e->y, e->r * 2, e->r, 0.3f, e->col, 1.5f, 48);
          if (e->absorbed >= 10) { killEnemy(e, CA_OVERLOAD); return; }
        }
      }
      break;
    }
    case ET_MORTAR: {
      float radial = clampf((td - 540) / 140, -1, 1), gx = tx * radial - ty * e->orbit * 0.4f, gy = ty * radial + tx * e->orbit * 0.4f, m = mhypot(gx, gy); if (m == 0) m = 1;
      steer(e, gx / m, gy / m, 80 * spd * minf(1, m), 120, dt);
      e->aim = lerpAngle(e->aim, matan2(ty, tx), minf(1, dt * 4));
      e->recoil = maxf(0, e->recoil - dt * 3); e->ang += dt * 0.4f; break;
    }
  }
}
static void lobShell(float x0, float y0, float x1, float y1, int beats, C3 col);
static void enemy_beat(Enemy *e, i64 beat) {
  if (e->spawn > 0) return;
  switch (e->type) {
    case ET_LUNGER: {
      if ((beat + e->phase) % 2 == 1) return;
      float tx, ty, td; towardPlayer(e, &tx, &ty, &td); float s = 660 * Game.diff.speed;
      e->vx = tx * s; e->vy = ty * s; e->ang = matan2(ty, tx); e->dashing = 0.5f;
      BurstO o = BOang(e->ang + PI, 0.8f); burst(e->x, e->y, 10, e->col, 100, 300, 0.2f, 0.4f, &o); break;
    }
    case ET_BEACON: {
      if ((beat + e->phase) % 2 == 1) { sfx_charge(spb() * 0.95f, e->x); return; }
      int n = Game.wave >= 8 ? 5 : 3; float ox = e->x + mcos(e->aim) * 24, oy = e->y + msin(e->aim) * 24;
      for (int i = 0; i < n; i++) enemyShot(ox, oy, e->aim + (i - (n - 1) / 2.f) * 0.2f, 280, 7, COL_ebullet);
      BurstO o = BOang(e->aim, 0.7f); burst(ox, oy, 8, e->col, 100, 260, 0.15f, 0.3f, &o); break;
    }
    case ET_BOLA: if (beat % 4 == 0) e->spin *= -1; break;
    case ET_VORTEX: fx_ring2(e->x, e->y, e->r * 3.2f, e->r * 0.8f, spb() * 0.9f, cmul(e->col, 0.5f), 1.2f, 40, 1, 2); break;
    case ET_MORTAR: {
      int k = (int)((beat + e->phase * 2) % 4);
      if (k == 3) sfx_charge(spb() * 0.9f, e->x);
      if (k != 0) return;
      Player *p = &Game.p; float lead = spb() * 2 * 0.55f;
      lobShell(e->x + mcos(e->aim) * 26, e->y + msin(e->aim) * 26, clampf(p->x + p->vx * lead, 30, ARENA_W - 30), clampf(p->y + p->vy * lead, 30, ARENA_H - 30), 2, e->col);
      e->recoil = 1;
      BurstO o = BOup(200, 500, 17); o.hasAngle = 1; o.angle = e->aim; o.hasSpread = 1; o.spread = 0.6f; burst(e->x, e->y, 10, e->col, 80, 260, 0.2f, 0.4f, &o); break;
    }
  }
}
static void enemy_hits(Enemy *e) {
  if (e->type != ET_BOLA) return;
  float o[4]; bola_orbs(e, o);
  hit_add(o[0], o[1], 13, e, 0, 1, HF_VULN | HF_SOLID | HF_HARM); hit_add(o[2], o[3], 13, e, 0, 2, HF_VULN | HF_SOLID | HF_HARM);
  for (int i = 1; i < 6; i++) { float f = i / 6.f; if (mfabs(f - 0.5f) > 0.1f) hit_add(lerpf(o[0], o[2], f), lerpf(o[1], o[3], f), 6, e, 0, -1, HF_HARM); }
}
static void enemy_death(Enemy *e, int cause) {
  if (e->type == ET_CLUSTER) {
    for (int i = 0; i < 3; i++) { float a = e->ang + i * TAU / 3; Enemy *m = spawnEnemy(ET_SHARD, e->x + mcos(a) * 14, e->y + msin(a) * 14, 0); if (m) { m->vx = mcos(a) * 320; m->vy = msin(a) * 320; } }
  } else if (e->type == ET_BOLA) {
    float o[4]; bola_orbs(e, o);
    for (int k = 0; k < 2; k++) if (Game.nbm < ARR(Game.bm)) { Boom *b = &Game.bm[Game.nbm++]; b->x = o[k * 2]; b->y = o[k * 2 + 1]; b->t = 0; b->r = 0; b->dmg = 0; b->col = e->col; b->done = 0; }
    for (int i = 0; i <= 10; i++) { float f = i / 10.f; burst(lerpf(o[0], o[2], f), lerpf(o[1], o[3], f), 3, c3(1.4f, 1.1f, 0.5f), 40, 220, 0.2f, 0.5f, NULL); }
  } else if (e->type == ET_VORTEX) {
    if (cause == CA_OVERLOAD) for (int i = 0; i < 10; i++) { float a = i / 10.f * TAU; Enemy *m = spawnEnemy(ET_GNAT, e->x + mcos(a) * 20, e->y + msin(a) * 20, 0); if (m) { m->vx = mcos(a) * 400; m->vy = msin(a) * 400; } }
    grid_explode(160, e->x, e->y, 420, -120);
    fx_wave(e->x, e->y, 360, 0.7f, 1.4f);
    e->score += e->absorbed * 50;
  }
}
static void enemy_draw(Enemy *e, Batch *b, C3 c, float k) {
  switch (e->type) {
    case ET_MOTE: {
      bcircle(b, e->x, e->y, e->r * 0.8f * k, 14, 1.5f, c, e->ang);
      float s = e->r * 0.5f * k;
      for (int i = 0; i < 2; i++) { float a = e->ang * 1.7f + i * PI / 2, ca = mcos(a) * s, sa = msin(a) * s; bline(b, e->x - ca, e->y - sa, e->x + ca, e->y + sa, 1.3f, c); }
      break;
    }
    case ET_DART: {
      float s = e->r * k * (1 + Game.beatPulse * 0.15f), ca = mcos(e->ang), sa = msin(e->ang);
      POLY(b, SH_dart, e->x, e->y, e->ang, s, 1.7f, c);
      bline(b, e->x - ca * s * 0.4f, e->y - sa * s * 0.4f, e->x - ca * s * 1.3f, e->y - sa * s * 1.3f, 1, cmul(c, 0.6f)); break;
    }
    case ET_FLICKER: POLY(b, SH_bowtie, e->x, e->y, e->ang, e->r * 0.85f * k, 1.6f, c); bdot(b, e->x, e->y, 2.4f * k, cmul(c, 1.4f)); break;
    case ET_LUNGER: {
      float charge = chargeOf(e), g = 1 + charge * 1.4f;
      POLY(b, SH_spear, e->x, e->y, e->ang, e->r * k, 1.8f, cmul(c, g));
      bdot(b, e->x + mcos(e->ang) * 3, e->y + msin(e->ang) * 3, 2.2f * k, cmul(c, g));
      if (charge > 0.5f && e->spawn <= 0) { float a = (charge - 0.5f) * 2, tx, ty, td; towardPlayer(e, &tx, &ty, &td); bline(b, e->x, e->y, e->x + tx * 120 * a, e->y + ty * 120 * a, 0.8f, cmul(c, a * 0.6f)); }
      break;
    }
    case ET_CLUSTER:
      for (int i = 0; i < 3; i++) { float a = e->ang + i * TAU / 3, d = e->r * 0.45f * k; bcircle(b, e->x + mcos(a) * d, e->y + msin(a) * d, e->r * 0.42f * k, 12, 1.6f, c, e->t); }
      bdot(b, e->x, e->y, 2.5f * k, cmul(c, 1.4f)); break;
    case ET_SHARD: POLY(b, SH_tri, e->x, e->y, e->ang, e->r * k, 1.4f, c); break;
    case ET_GNAT: {
      float flap = 0.75f + 0.25f * msin(e->t * 30 + e->seed), pts[6] = { -0.7f, 0.9f * flap, 0.9f, 0, -0.7f, -0.9f * flap };
      bpoly(b, pts, 3, e->x, e->y, e->ang, e->r * 1.1f * k, 1.4f, c, 0);
      bdot(b, e->x + mcos(e->ang) * 6 * k, e->y + msin(e->ang) * 6 * k, 1.8f, cmul(c, 1.4f)); break;
    }
    case ET_BEACON: {
      float charge = chargeOf(e), g = 1 + charge * charge * 1.5f, ca = mcos(e->aim), sa = msin(e->aim), R = e->r * k;
      bcircle(b, e->x, e->y, R, 24, 1.7f, c, 0);
      for (int i = 0; i < 4; i++) { float a = e->ang + i * PI / 2 + PI / 4; bline(b, e->x + mcos(a) * R * 0.62f, e->y + msin(a) * R * 0.62f, e->x + mcos(a) * R * 0.86f, e->y + msin(a) * R * 0.86f, 1.4f, c); }
      bcircle(b, e->x, e->y, R * 0.3f, 10, 1.4f, cmul(c, g), 0);
      bline(b, e->x + ca * R * 0.3f, e->y + sa * R * 0.3f, e->x + ca * (R + 12), e->y + sa * (R + 12), 2.2f, cmul(c, g));
      if (charge > 0.6f && e->spawn <= 0) { float a = (charge - 0.6f) * 2.5f * 0.35f; bline(b, e->x, e->y, e->x + ca * 900, e->y + sa * 900, 0.6f, cmul(c, a)); }
      break;
    }
    case ET_BOLA: {
      float o[4]; bola_orbs(e, o); float nx = -msin(e->ang), ny = mcos(e->ang), hum = 0.6f + 0.6f * Game.beatPulse;
      float mx1 = lerpf(e->x, o[0], k), my1 = lerpf(e->y, o[1], k), mx2 = lerpf(e->x, o[2], k), my2 = lerpf(e->y, o[3], k), px = mx1, py = my1;
      for (int i = 1; i <= 8; i++) { float f = i / 8.f, w = i < 8 ? msin(e->t * 40 + i * 2.1f) * 4 : 0, qx = lerpf(mx1, mx2, f) + nx * w, qy = lerpf(my1, my2, f) + ny * w; bline(b, px, py, qx, qy, 1.1f, c3(c.r * hum, c.g * hum, c.b * hum * 1.3f)); px = qx; py = qy; }
      float ms[4] = { mx1, my1, mx2, my2 };
      for (int j = 0; j < 2; j++) { bcircle(b, ms[j * 2], ms[j * 2 + 1], 11 * k, 14, 2, c, -e->ang); bdot(b, ms[j * 2], ms[j * 2 + 1], 3.2f * k, cmul(c, 1.5f)); }
      POLY(b, SH_diamond, e->x, e->y, e->ang, 7 * k, 1.3f, c); break;
    }
    case ET_VORTEX:
      bcircle(b, e->x, e->y, e->r * 0.55f * k * (1 + Game.beatPulse * 0.15f), 20, 2.2f, c, 0);
      for (int i = 0; i < 3; i++) {
        float px = 0, py = 0;
        for (int j = 0; j <= 10; j++) {
          float f = j / 10.f, rr = e->r * (0.55f + 1.35f * f) * k, a = -e->ang + i * TAU / 3 + f * 2.4f, x = e->x + mcos(a) * rr, y = e->y + msin(a) * rr, fa = 1 - f * 0.75f;
          if (j) bline(b, px, py, x, y, 1.4f, cmul(c, fa));
          px = x; py = y;
        }
      }
      break;
    case ET_MORTAR: {
      float ch = chargeOf(e), g = 1 + ch * ch * 1.2f, ca = mcos(e->aim), sa = msin(e->aim), R = e->r * k, rl = 1 - e->recoil * 0.35f;
      bngon(b, e->x, e->y, R, 8, e->ang, 1.8f, c);
      bngon(b, e->x, e->y, R * 0.55f, 8, -e->ang, 1.2f, cmul(c, 0.7f));
      bline(b, e->x - sa * 5, e->y + ca * 5, e->x - sa * 5 + ca * R * 1.5f * rl, e->y + ca * 5 + sa * R * 1.5f * rl, 1.6f, cmul(c, g));
      bline(b, e->x + sa * 5, e->y - ca * 5, e->x + sa * 5 + ca * R * 1.5f * rl, e->y - ca * 5 + sa * R * 1.5f * rl, 1.6f, cmul(c, g));
      bdot(b, e->x, e->y, 3 * k, cmul(c, g * 1.3f)); break;
    }
  }
}
/** A shell arcs through the air for `beats` beats, so it always lands on the grid. The floor shows where. */
static void lobShell(float x0, float y0, float x1, float y1, int beats, C3 col) {
  if (Game.nsh >= ARR(Game.sh)) return;
  Shell *s = &Game.sh[Game.nsh++]; s->x0 = x0; s->y0 = y0; s->x1 = x1; s->y1 = y1; s->b0 = gameBeat(); s->beats = beats; s->h = 160 + mhypot(x1 - x0, y1 - y0) * 0.22f; s->col = col; s->x = x0; s->y = y0; s->z = LIFT; s->R = 74; s->f = 0; s->done = 0;
  sfx_enemyFire(x0);
}
static void player_hurt(void);
static void updateShells(void) {
  double gb = gameBeat(); Player *p = &Game.p;
  for (int i = 0; i < Game.nsh; i++) {
    Shell *s = &Game.sh[i]; float f = clampf((float)((gb - s->b0) / s->beats), 0, 1);
    s->x = lerpf(s->x0, s->x1, f); s->y = lerpf(s->y0, s->y1, f); s->z = LIFT + s->h * 4 * f * (1 - f); s->f = f;
    if (f < 1) continue;
    s->done = 1;
    if (p->alive && dist2(p->x, p->y, s->x1, s->y1) < (s->R + p->hitR) * (s->R + p->hitR)) player_hurt();
    for (int k = 0; k < 6; k++) { float a = k * TAU / 6 + rnd(0.5f); enemyShot(s->x1, s->y1, a, 150, 6, COL_ebullet); }
    blast(s->x1, s->y1, 40, s->col, 600, 0.8f);
    fx_ring2(s->x1, s->y1, 10, s->R, 0.35f, cmul(s->col, 1.4f), 2.5f, 40, 1, 1);
    fx_wave(s->x1, s->y1, s->R * 1.6f, 0.4f, 1);
    grid_explode(90, s->x1, s->y1, 200, -80);
    sfx_explode(1.2f, s->x1); cam_shake(0.12f);
  }
  int j = 0; for (int i = 0; i < Game.nsh; i++) if (!Game.sh[i].done) Game.sh[j++] = Game.sh[i]; Game.nsh = j;
}
static void drawShells(Batch *b, Batch *fb) {
  for (int i = 0; i < Game.nsh; i++) {
    Shell *s = &Game.sh[i]; C3 c = s->col; float f = s->f;
    fb->z = 1;
    bcircle(fb, s->x1, s->y1, s->R, 32, 1.2f, cmul(c, 0.5f * (0.4f + f)), 0);
    bcircle(fb, s->x1, s->y1, s->R * (1 - f) + 4, 28, 1.6f, cmul(c, f * 1.3f), 0);
    fb->z = LIFT;
    b->z = s->z; bdoth(b, s->x, s->y, 6, cmul(c, 1.6f), 4); bcircle(b, s->x, s->y, 9, 10, 1, c, f * 20);
    b->z = LIFT;
  }
}
static Enemy *spawnEnemy(int type, float x, float y, float spawn) {
  if (Game.nen >= MAXE) return NULL;
  Enemy *e = &Game.en[Game.nen++]; mzero(e, sizeof *e);
  const EType *T = &ETS[type];
  e->id = ++Game.nextId; e->type = (u8)type; e->x = clampf(x, 30, ARENA_W - 30); e->y = clampf(y, 30, ARENA_H - 30);
  e->r = T->r; e->hp = mfloor(T->hp * Game.diff.hp + 0.999f); e->ang = rnd(TAU); e->spawn = spawn < 0 ? spb() * 1.5f : spawn;
  e->seed = rnd(100); e->phase = rndi(0, 1); e->score = T->score; e->geoms = T->geoms; e->col = T->col; e->orbitT = -10;
  e->maxHp = e->hp; e->spawnMax = e->spawn;
  enemy_init(e);
  if (e->spawn > 0) sfx_spawn(e->x);
  return e;
}
#define EB_CAP 700
static void enemyShot(float x, float y, float a, float spd, float r, C3 col) {
  if (Game.neb >= EB_CAP) return;
  EBullet *b = &Game.eb[Game.neb++]; b->x = x; b->y = y; b->vx = mcos(a) * spd; b->vy = msin(a) * spd; b->r = r; b->life = 4.5f; b->col = col; b->t = 0;
  sfx_enemyFire(x);
}

/* ---------------- damage + death ---------------- */
static int damageEnemy(Enemy *e, float dmg, float kx, float ky, int cause) {
  if (e->dead || e->spawn > 0) return 0;
  e->hp -= dmg; e->flash = 0.07f; e->vx += kx; e->vy += ky;
  if (e->hp <= 0) { killEnemy(e, cause); return 1; }
  sfx_hit(e->x);
  return 0;
}
static void arcFrom(float x, float y, int lvl);
static void killEnemy(Enemy *e, int cause) {
  if (e->dead) return;
  e->dead = 1;
  const EType *T = &ETS[e->type]; C3 c = e->col; int big = e->r > 20 || T->big;
  enemy_death(e, cause);
  Game.kills++;
  Game.p.energy = minf(E_MAX, Game.p.energy + 2);
  i64 pts = (i64)(e->score * Game.mult + 0.5f);
  addScore(pts);
  char buf[32]; if (big || pts >= 5000) { s_fmt(buf, pts); fx_text(e->x, e->y - e->r - 6, buf, big ? 18 : 13, cmul(c, 1.5f), 0.9f); }
  float g = e->geoms; int ng = g < 1 ? (chance(g) ? 1 : 0) : (int)g;
  for (int i = 0; i < ng; i++) spawnPickup(PK_GEOM, e->x, e->y);
  int anyHeal = 0; for (int i = 0; i < Game.npk; i++) if (Game.pk[i].type == PK_HEAL) anyHeal = 1;
  if (chance(Game.p.hp < Game.st.maxHp ? 0.008f : 0.002f) && !anyHeal) spawnPickup(PK_HEAL, e->x, e->y);
  blast(e->x, e->y, 22 + e->r * 1.6f, c, 520 + e->r * 8, 0.9f);
  fx_ring(e->x, e->y, e->r * 0.5f, e->r * 3.2f, 0.35f, cmul(c, 1.2f), 1.6f, 28);
  fx_ring2(e->x, e->y, e->r * 0.3f, e->r * 2.4f, 0.5f, cmul(c, 0.5f), 1.2f, 28, 1, 1);
  grid_explode(big ? 70 : 32, e->x, e->y, big ? 220 : 130, -30);
  cam_shake(big ? 0.22f : 0.07f);
  if (big) { fx_wave(e->x, e->y, 160, 0.45f, 0.8f); Game.hitstopT = maxf(Game.hitstopT, 0.045f); }
  sfx_explode(big ? 1.6f : 0.8f, e->x);
  Game.chain = Game.chainT > 0 ? Game.chain + 1 : 0; Game.chainT = 0.55f;
  int place = beatPlace();
  sfx_killNote(Game.chain, big, e->x, place);
  if (place == BP_DOWN) fx_ring2(e->x, e->y, e->r, e->r * 4.5f, 0.4f, c3(1.6f, 1.3f, 0.5f), 1.4f, 32, 1, 1);
  if (Game.st.chain && cause != CA_ARC) arcFrom(e->x, e->y, Game.st.chain);
  if (Game.st.boom && cause != CA_BOMB && Game.nbm < ARR(Game.bm)) { Boom *b = &Game.bm[Game.nbm++]; b->x = e->x; b->y = e->y; b->t = -0.05f; b->r = 70 + 22.f * Game.st.boom; b->dmg = 1.5f + Game.st.boom * 1.5f; b->col = COL_boom; b->done = 0; }
}
static void jagged(float *pts, float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0, L = mhypot(dx, dy); if (L == 0) L = 1; float nx = -dy / L, ny = dx / L;
  pts[0] = x0; pts[1] = y0;
  for (int i = 1; i < 6; i++) { float t = i / 6.f, o = rndr(-1, 1) * L * 0.12f; pts[i * 2] = x0 + dx * t + nx * o; pts[i * 2 + 1] = y0 + dy * t + ny * o; }
  pts[12] = x1; pts[13] = y1;
}
static void arcFrom(float x, float y, int lvl) {
  int idx[MAXE]; float d2s[MAXE]; int n = 0;
  for (int i = 0; i < Game.nen; i++) { Enemy *o = &Game.en[i]; if (o->dead || o->spawn > 0) continue; float d2 = dist2(x, y, o->x, o->y); if (d2 < 240 * 240) { idx[n] = i; d2s[n] = d2; n++; } }
  for (int i = 1; i < n; i++) for (int j = i; j > 0 && d2s[j] < d2s[j - 1]; j--) { float t = d2s[j]; d2s[j] = d2s[j - 1]; d2s[j - 1] = t; int u = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = u; }
  float px = x, py = y; int m = lvl + 1 < n ? lvl + 1 : n;
  u32 ids[8]; for (int i = 0; i < m && i < 8; i++) ids[i] = Game.en[idx[i]].id;
  for (int i = 0; i < m && i < 8; i++) {
    Enemy *o = findEnemy(ids[i]); if (!o) continue;
    if (Game.nbo < ARR(Game.bo)) { Bolt *b = &Game.bo[Game.nbo++]; jagged(b->pts, px, py, o->x, o->y); b->t = 0; b->dur = 0.18f; }
    float ox = o->x, oy = o->y;
    damageEnemy(o, 1.5f + lvl, 0, 0, CA_ARC);
    px = ox; py = oy;
  }
}
static void addScore(i64 n) {
  i64 before = Game.score;
  Game.score += n; Game.scorePulse = 1;
  if (Game.hiScore > 0 && before <= Game.hiScore && Game.score > Game.hiScore && Game.p.alive && !Game.demo) {
    s_cpy(Game.banner.text, "NEW RECORD"); s_cpy(Game.banner.sub, "KEEP GOING"); Game.banner.t = 0; Game.banner.dur = 2.2f; Game.banner.col = c3(1.6f, 1.4f, 0.4f); Game.hasBanner = 1;
    sfx_milestone();
  }
}

/* ---------------- pickups ---------------- */
static int directorCleared(void);
static void spawnPickup(int type, float x, float y) {
  if (Game.npk >= ARR(Game.pk)) { memmove(Game.pk, Game.pk + 1, sizeof(Pickup) * (ARR(Game.pk) - 1)); Game.npk--; }
  float a = rnd(TAU), s = rndr(60, 200);
  Pickup *g = &Game.pk[Game.npk++]; g->type = type; g->x = x; g->y = y; g->vx = mcos(a) * s; g->vy = msin(a) * s; g->t = 0; g->life = type == PK_GEOM ? 7 : 12; g->ang = rnd(TAU);
}
static void collect(Pickup *g) {
  Player *p = &Game.p; char buf[48];
  if (g->type == PK_GEOM) {
    Game.mult = Game.mult + 1 > 9999 ? 9999 : Game.mult + 1; if (Game.mult > Game.maxMult) Game.maxMult = Game.mult; Game.multPulse = 1;
    sfx_geom(Game.mult, g->x);
    static const int MS[] = { 25, 50, 100, 200, 300, 500, 750, 1000, 1500, 2000, 3000, 5000 };
    for (int i = 0; i < ARR(MS); i++) if (Game.mult == MS[i]) {
      s_printf(buf, "x%d MULTIPLIER", Game.mult); fx_text(p->x, p->y - 56, buf, 16, c3(1.6f, 1.4f, 0.4f), 1.3f);
      fx_ring(p->x, p->y, 16, 140, 0.5f, c3(1.4f, 1.2f, 0.3f), 2, 48); sfx_milestone();
    }
    burst(g->x, g->y, 5, COL_geom, 40, 160, 0.15f, 0.3f, NULL);
  } else if (g->type == PK_HEAL) {
    int whole = p->hp >= Game.st.maxHp;
    if (!whole) p->hp++; else addScore(5000LL * Game.mult);
    sfx_heal(); fx_text(p->x, p->y - 34, whole ? "BONUS" : "HULL +1", 14, COL_heal, 1.1f);
    fx_ring(p->x, p->y, 10, 90, 0.5f, COL_heal, 2, 48);
  } else {
    p->bombs = p->bombs + 1 > Game.st.bombsMax ? Game.st.bombsMax : p->bombs + 1; sfx_heal();
    fx_text(p->x, p->y - 34, "BOMB +1", 14, COL_bomb, 1.1f);
  }
}
static void updatePickups(float dt) {
  Player *p = &Game.p; int vac = directorCleared() == 2; float mr = vac ? 4000 : 150 * (1 + 0.6f * (Game.st.magnet - 1));
  for (int i = Game.npk - 1; i >= 0; i--) {
    Pickup *g = &Game.pk[i];
    g->t += dt; g->ang += dt * 3;
    float k = mexp(-2.5f * dt); g->vx *= k; g->vy *= k;
    g->vx += field_ax(g->x) * dt * 1.6f; g->vy += field_ay(g->y) * dt * 1.6f;
    if (p->alive) {
      float dx = p->x - g->x, dy = p->y - g->y, d = mhypot(dx, dy);
      if (d < mr && d > 0) { float a = vac ? 3200 * dt : (1 - d / mr) * 2600 * dt + 180 * dt; g->vx += dx / d * a; g->vy += dy / d * a; }
      if (d < p->r + 14) { Pickup c = *g; memmove(&Game.pk[i], &Game.pk[i + 1], sizeof(Pickup) * (Game.npk - i - 1)); Game.npk--; collect(&c); continue; }
    }
    for (int w = 0; w < Game.nwells; w++) { Well *W = &Game.wells[w]; if (W->boss) continue; float dx = W->x - g->x, dy = W->y - g->y, d2 = dx * dx + dy * dy; if (d2 < W->pr * W->pr) { float d = msqrt(d2); if (d == 0) d = 1; g->vx += dx / d * 300 * dt; g->vy += dy / d * 300 * dt; if (d < W->r) g->t = g->life; } }
    g->x = clampf(g->x + g->vx * dt, 8, ARENA_W - 8); g->y = clampf(g->y + g->vy * dt, 8, ARENA_H - 8);
    if (g->t >= g->life) { memmove(&Game.pk[i], &Game.pk[i + 1], sizeof(Pickup) * (Game.npk - i - 1)); Game.npk--; }
  }
}

/* =========================================================================
   PLAYER — movement, beat-judged dash, bombs, weapons, augments
   ========================================================================= */
typedef struct { int mode; float moveX, moveY, aimX, aimY; int aimStick, fire, dash, bomb, slow, pause, any; float mouseX, mouseY; int mouseSeen; } InputState;
static InputState In;
static void orbitPos(int i, float *x, float *y) { Player *p = &Game.p; int n = Game.st.orbit; float a = (float)Game.visBeat * TAU / 4 + i * TAU / n; *x = p->x + mcos(a) * 54; *y = p->y + msin(a) * 54; }
static int hitHittable(Hit *h, float dmg, float kx, float ky, int cause) {
  if (h->boss) return boss_damagePart(h->part, dmg, cause);
  return damageEnemy(h->e, dmg, kx, ky, cause);
}
typedef struct { float x, y; } OrbCtx;
static int orbit_cb(Hit *h, void *ctx) {
  OrbCtx *o = ctx;
  if (!(h->f & HF_VULN) || hit_dead(h) || dist2(o->x, o->y, h->x, h->y) > (h->r + 10) * (h->r + 10)) return 1;
  float *last = h->boss ? NULL : &h->e->orbitT; static float bossOrbitT = -10;
  if (!last) last = &bossOrbitT;
  if (Game.time - *last < 0.25f) return 1;
  *last = Game.time;
  hitHittable(h, 1.2f * Game.st.dmg, 0, 0, CA_ORBIT);
  burst(o->x, o->y, 4, COL_orbit, 60, 200, 0.1f, 0.25f, NULL);
  return 1;
}
static int dash_cb(Hit *h, void *ctx) {
  (void)ctx; Player *p = &Game.p;
  if (!(h->f & HF_VULN) || hit_dead(h) || dist2(p->x, p->y, h->x, h->y) > (h->r + 22) * (h->r + 22)) return 1;
  if (!h->boss) { if (h->e->dashGen == p->dashGen) return 1; h->e->dashGen = p->dashGen; }
  else { static int bossDashGen = -1; if (bossDashGen == p->dashGen) return 1; bossDashGen = p->dashGen; }
  hitHittable(h, 4 * Game.st.dashDmg * Game.st.dmg, p->ddx * 200, p->ddy * 200, CA_DASH);
  return 1;
}
static void player_dash(void);
static void player_bomb(void);
static void player_update(float dt, float rdt) {
  Player *p = &Game.p; Stats *st = &Game.st;
  if (!p->alive) { p->deadT += rdt; return; }
  if (In.mode == 0) { float wx, wy; cam_toWorld(In.mouseX, In.mouseY, LIFT, &wx, &wy); float dx = wx - p->x, dy = wy - p->y; if (dx * dx + dy * dy > 9) p->aim = matan2(dy, dx); }
  else if (In.aimStick) p->aim = matan2(In.aimY, In.aimX);
  else if (In.moveX != 0 || In.moveY != 0) p->aim = lerpAngle(p->aim, matan2(In.moveY, In.moveX), minf(1, dt * 6));
  p->ang = p->aim;
  float spd = 420 * st->speed;
  if (p->dashT > 0) {
    p->dashT -= dt;
    float k = 1500 * (0.4f + 0.6f * clampf(p->dashT / 0.16f, 0, 1));
    p->vx = p->ddx * k; p->vy = p->ddy * k;
    if (p->ntrail < ARR(p->trail)) { Trail *t = &p->trail[p->ntrail++]; t->x = p->x; t->y = p->y; t->a = p->ang; t->t = 0; }
    if (st->dashDmg) hash_query(p->x - 40, p->y - 40, p->x + 40, p->y + 40, dash_cb, NULL);
  } else {
    float coast = (In.moveX != 0 || In.moveY != 0) ? 0.45f : 1;
    p->vx = damp(p->vx, In.moveX * spd + field_ax(p->x) * coast, 13, dt); p->vy = damp(p->vy, In.moveY * spd + field_ay(p->y) * coast, 13, dt);
  }
  for (int i = 0; i < Game.nwells; i++) {
    Well *w = &Game.wells[i]; float dx = w->x - p->x, dy = w->y - p->y, d = mhypot(dx, dy); if (d == 0) d = 1;
    if (d < w->pr) { float a = w->ppull * (1 - d / w->pr) * dt; p->vx += dx / d * a; p->vy += dy / d * a; }
  }
  p->x += p->vx * dt; p->y += p->vy * dt;
  float m = p->r;
  if (p->x < m) { p->x = m; p->vx = maxf(0, p->vx); } if (p->x > ARENA_W - m) { p->x = ARENA_W - m; p->vx = minf(0, p->vx); }
  if (p->y < m) { p->y = m; p->vy = maxf(0, p->vy); } if (p->y > ARENA_H - m) { p->y = ARENA_H - m; p->vy = minf(0, p->vy); }
  for (int i = 0; i < p->ntrail; i++) p->trail[i].t += dt;
  int j = 0; for (int i = 0; i < p->ntrail; i++) if (p->trail[i].t <= 0.25f) p->trail[j++] = p->trail[i]; p->ntrail = j;
  p->inv = maxf(0, p->inv - dt); p->dashCd = maxf(0, p->dashCd - dt);
  if (!Game.btOn) p->energy = minf(E_MAX, p->energy + E_REGEN * rdt);
  if (In.dash && p->dashCd <= 0) player_dash();
  if (In.bomb) { if (p->bombs > 0) player_bomb(); else if (Game.noBombMsg <= 0) { fx_text(p->x, p->y - 40, "NO BOMBS", 12, c3(0.8f, 0.3f, 0.4f), 0.7f); Game.noBombMsg = 0.8f; } }
  float sp = mhypot(p->vx, p->vy);
  if (sp > 60) {
    float back = matan2(-p->vy, -p->vx); C3 hue = hsl(fractf(0.82f + (float)Game.visBeat * 0.0625f), 1, 0.62f);
    if (chance(minf(1, sp / 300) * 0.95f * partsBudget)) emit(p->x + mcos(back) * 10, p->y + msin(back) * 10, LIFT, mcos(back + rndr(-0.3f, 0.3f)) * rndr(80, 220) + p->vx * 0.2f, msin(back + rndr(-0.3f, 0.3f)) * rndr(80, 220) + p->vy * 0.2f, rndr(-20, 60), rndr(0.25f, 0.5f), hue.r * 1.4f, hue.g * 1.4f, hue.b * 1.4f, 1.4f, 0.9f, 0.02f, 9);
    grid_directed(p->vx * 0.0035f, p->vy * 0.0035f, 0, p->x, p->y, 70);
  }
  for (int i = 0; i < st->orbit; i++) { OrbCtx o; orbitPos(i, &o.x, &o.y); hash_query(o.x - 40, o.y - 40, o.x + 40, o.y + 40, orbit_cb, &o); }
}
static void player_dash(void) {
  Player *p = &Game.p; Stats *st = &Game.st; float cost = DASH_COST * mpow(0.8f, (float)st->dashDmg);
  if (p->energy < cost) { if (Game.noBombMsg <= 0) { fx_text(p->x, p->y - 40, "NO ENERGY", 12, c3(0.5f, 0.6f, 1.1f), 0.6f); Game.noBombMsg = 0.6f; } return; }
  p->energy -= cost;
  float dx = In.moveX, dy = In.moveY;
  if (mhypot(dx, dy) < 0.2f) { dx = mcos(p->aim); dy = msin(p->aim); }
  float m = mhypot(dx, dy); p->ddx = dx / m; p->ddy = dy / m;
  p->dashT = 0.16f; p->inv = maxf(p->inv, 0.26f); p->dashCd = DASH_CD; p->dashGen++;
  /* judged against the beat as heard when the input arrived, including the calibration offset */
  double vb = visBeatAt(dashAge); float off = (float)((vb - (double)(i64)(vb + (vb >= 0 ? 0.5 : -0.5))) * spb()), ao = mfabs(off), w = 1 + 0.4f * st->perfect;
  int tier = -1; for (int i = 0; i < 3; i++) if (ao <= DASH_TIERS[i].win * w) { tier = i; break; }
  sfx_dash(tier);
  BurstO bo = BOang(matan2(-p->ddy, -p->ddx), 1.4f); burst(p->x, p->y, 18, c3(0.6f, 0.9f, 1.2f), 100, 380, 0.2f, 0.45f, &bo);
  if (tier < 0) { grid_explode(25, p->x, p->y, 130, -20); fx_text(p->x, p->y - 38, ao < 0.3f ? (off < 0 ? "EARLY" : "LATE") : "OFF BEAT", 10, c3(0.6f, 0.55f, 0.85f), 0.5f); return; }
  const DashTier *T = &DASH_TIERS[tier];
  p->energy = minf(E_MAX, p->energy + T->refund); p->inv = maxf(p->inv, T->inv);
  float R = T->R * (1 + 0.5f * st->perfect); C3 c = T->col;
  grid_explode(tier == 0 ? 90 : tier == 1 ? 55 : 30, p->x, p->y, R * 1.8f, -20);
  fx_ring(p->x, p->y, 16, R, 0.42f, c, tier == 0 ? 3 : 2, 64);
  if (tier == 0) {
    Game.perfects++;
    fx_ring(p->x, p->y, 10, R * 0.7f, 0.3f, c3(1.5f, 0.6f, 1.6f), 1.5f, 48);
    fx_ring2(p->x, p->y, 20, R * 1.1f, 0.6f, c3(0.4f, 1.2f, 1.6f), 2, 64, 1, 1);
    fx_wave(p->x, p->y, R * 1.2f, 0.5f, 1.6f);
    cam_punch(0.035f); cam_shake(0.25f);
    Game.flash = maxf(Game.flash, 0.12f); Game.flashCol = c3(1, 0.7f, 0.8f);
    BurstO o1 = BOvary(0.3f); burst(p->x, p->y, 40, COL_perfect, 300, 900, 0.25f, 0.5f, &o1);
    BurstO o2 = BOup(200, 600, 17); burst(p->x, p->y, 30, c3(1.4f, 0.5f, 1.5f), 100, 500, 0.5f, 1.0f, &o2);
  } else {
    if (tier == 1) { fx_wave(p->x, p->y, R, 0.4f, 0.9f); cam_shake(0.12f); }
    BurstO o = BOvary(0.3f); burst(p->x, p->y, tier == 1 ? 24 : 12, c, 200, 600, 0.2f, 0.4f, &o);
  }
  int struck = 0, downed = 0;
  for (int i = 0; i < Game.nen; i++) {
    Enemy *e = &Game.en[i]; if (e->dead || e->spawn > 0) continue;
    float d = mhypot(e->x - p->x, e->y - p->y);
    if (d < R + e->r) { struck++; float k = 1 - d / (R + e->r), dd = d > 0 ? d : 1; if (damageEnemy(e, T->dmg * st->dmg, (e->x - p->x) / dd * 700 * k, (e->y - p->y) / dd * 700 * k, CA_PERFECT)) downed++; }
  }
  for (int i = 0; i < Game.neb; i++) { EBullet *b = &Game.eb[i]; if (dist2(b->x, b->y, p->x, p->y) < R * R) { struck++; b->life = 0; burst(b->x, b->y, 4, c, 50, 200, 0.1f, 0.3f, NULL); } }
  { float bx, by, br; if (boss_targetable() && boss_pos(&bx, &by, &br) && dist2(bx, by, p->x, p->y) < (R + br) * (R + br)) { struck++; if (boss_damage(T->dmg * 1.5f * st->dmg, CA_PERFECT)) downed++; } }
  if (struck) addScore((i64)(3 - tier) * 50 * (struck < 10 ? struck : 10) * Game.mult);
  if (tier == 0 && downed) { Game.mult = Game.mult + 1 > 9999 ? 9999 : Game.mult + 1; if (Game.mult > Game.maxMult) Game.maxMult = Game.mult; Game.multPulse = 1; }
  char buf[24]; s_printf(buf, "%s%s", T->name, tier == 0 && downed ? " +1" : "");
  fx_text(p->x, p->y - 42, buf, tier == 0 ? 17 : 14, c, 0.8f);
}
static void player_bomb(void) {
  Player *p = &Game.p;
  p->bombs--; p->inv = maxf(p->inv, 1.0f);
  Game.bombWave.x = p->x; Game.bombWave.y = p->y; Game.bombWave.r = 0; Game.bombWave.bossHit = 0; Game.bombWave.gen = ++Game.bombGen; Game.bombWave.on = 1;
  for (int i = 0; i < Game.neb; i++) Game.eb[i].life = 0;
  Game.nsh = 0;
  sfx_bomb(Game.demo);
  cam_shake(Game.demo ? 0.2f : 0.9f); cam_punch(Game.demo ? 0.02f : 0.06f);
  fx_wave(p->x, p->y, 1500, 1.3f, 2.6f);
  for (int i = 0; i < 4; i++) fx_ring2(p->x, p->y, 10, 900 + i * 300.f, 0.9f + i * 0.2f, i % 2 ? c3(0.4f, 1.2f, 1.6f) : c3(1.6f, 0.5f, 1.4f), 4 - i * 0.6f, 96, 1, i % 2 ? 1 : LIFT);
  Game.flash = Set.flash ? 0.6f : 0.2f; Game.flashCol = c3(0.9f, 0.6f, 1.0f);
  grid_explode(600, p->x, p->y, 900, -200);
  BurstO o1 = BOvary(0.6f); o1.hasW = 1; o1.w = 1.6f; burst(p->x, p->y, 220, c3(1.2f, 0.6f, 1.6f), 400, 1600, 0.5f, 1.4f, &o1);
  BurstO o2 = BOup(300, 900, 17); o2.hasW = 1; o2.w = 1.5f; burst(p->x, p->y, 120, c3(0.5f, 1.2f, 1.6f), 100, 900, 0.8f, 1.8f, &o2);
  Game.slowTo = minf(0.35f, Game.slowT > 0 ? Game.slowTo : 1); Game.slowT = maxf(Game.slowT, 0.25f);
}
static void game_slow(float scale, float dur) { Game.slowTo = minf(scale, Game.slowT > 0 ? Game.slowTo : 1); Game.slowT = maxf(Game.slowT, dur); }
static void clearSpace(float R) {
  Player *p = &Game.p;
  for (int i = 0; i < Game.neb; i++) if (dist2(Game.eb[i].x, Game.eb[i].y, p->x, p->y) < R * R) Game.eb[i].life = 0;
  for (int i = 0; i < Game.nen; i++) {
    Enemy *e = &Game.en[i]; if (e->dead) continue;
    float dx = e->x - p->x, dy = e->y - p->y, d = mhypot(dx, dy); if (d == 0) d = 1;
    if (d < R) { if (e->r < 12 && e->spawn <= 0) killEnemy(e, CA_SHOCK); else { e->vx += dx / d * 900; e->vy += dy / d * 900; } }
  }
}
static void player_die(void);
static void player_hurt(void) {
  Player *p = &Game.p;
  if (!p->alive || p->inv > 0 || p->dashT > 0 || Game.god) return;
  if (p->shield > 0) {
    p->shield--; p->shieldBars = 0; p->inv = 1.2f;
    fx_ring(p->x, p->y, 30, 220, 0.5f, COL_shield, 3, 6); fx_wave(p->x, p->y, 260, 0.5f, 1.4f);
    fx_text(p->x, p->y - 40, "SHIELD BROKEN", 13, COL_shield, 0.9f);
    sfx_explode(1.2f, p->x); cam_shake(0.4f); clearSpace(240);
    burst(p->x, p->y, 40, COL_shield, 200, 600, 0.3f, 0.6f, NULL);
    return;
  }
  p->hp--; p->inv = 2.2f;
  int kept = (int)mfloor(Game.mult * Game.st.keep); if (kept < 1) kept = 1; int lost = Game.mult - kept;
  Game.mult = kept;
  sfx_playerHit();
  cam_shake(0.85f); cam_punch(0.05f);
  Game.flash = Set.flash ? 0.55f : 0.18f; Game.flashCol = c3(1.0f, 0.1f, 0.3f); Game.aberr = 1.6f;
  fx_wave(p->x, p->y, 520, 0.8f, 2.2f);
  fx_ring(p->x, p->y, 20, 320, 0.6f, c3(1.6f, 0.3f, 0.5f), 3, 64);
  grid_explode(320, p->x, p->y, 520, -160);
  blast(p->x, p->y, 90, c3(1.4f, 0.5f, 0.7f), 900, 1.1f);
  if (lost > 0) { char buf[24]; s_printf(buf, "COMBO -%d", lost); fx_text(p->x, p->y - 44, buf, 14, c3(1.5f, 0.4f, 0.5f), 1.0f); }
  clearSpace(300);
  game_slow(0.15f, 0.45f);
  if (p->hp <= 0) player_die();
}
static void player_die(void) {
  Player *p = &Game.p;
  p->alive = 0; p->deadT = 0;
  sfx_death(); cam_shake(1.2f);
  Game.flash = Set.flash ? 0.6f : 0.2f; Game.flashCol = c3(1, 0.75f, 0.95f);
  for (int i = 0; i < 5; i++) fx_ring(p->x, p->y, 10, 300 + i * 220.f, 0.8f + i * 0.25f, c3(1.6f, 1.2f - i * 0.2f, 1.6f), 3, 72);
  fx_wave(p->x, p->y, 1200, 1.6f, 3);
  grid_explode(900, p->x, p->y, 1200, -300);
  blast(p->x, p->y, 300, c3(1.4f, 1.1f, 1.5f), 1500, 2.2f);
  game_slow(0.2f, 2.0f);
  game_onPlayerDeath();
}
static void addBullet(float x, float y, float px, float py, float vx, float vy, float dmg, float r, float life, int pierce, int bounce, int accent, int drone) {
  if (Game.nbu >= MAXB) return;
  Bullet *b = &Game.bu[Game.nbu++]; b->x = x; b->y = y; b->px = px; b->py = py; b->vx = vx; b->vy = vy; b->dmg = dmg; b->r = r; b->life = life; b->pierce = pierce; b->bounce = bounce; b->accent = (u8)accent; b->drone = (u8)drone; b->nhit = 0;
}
static void player_fire(i64 tick) {
  Player *p = &Game.p; Stats *st = &Game.st;
  if (!p->alive || !In.fire) return;
  int accent = tick % TPB == 0, big = accent && st->accent > 0;
  float spd = 1280, dmg = st->dmg * (big ? 1 + 1.5f * st->accent : 1);
  float a = p->aim, ca = mcos(a), sa = msin(a), nx = -sa, ny = ca;
  float late = clampf((float)(snd_now() - tickTime(tick)), 0, 0.12f);
#define MAKE(ang, ox, oy) do { float _vx = mcos(ang) * spd + p->vx * 0.15f, _vy = msin(ang) * spd + p->vy * 0.15f; addBullet(p->x + (ox) + _vx * late, p->y + (oy) + _vy * late, p->x + (ox), p->y + (oy), _vx, _vy, dmg, big ? 6 : 4, 1.1f - late, st->pierce, st->bounce, big, 0); } while (0)
  if (st->spread == 0) { MAKE(a, ca * 16 + nx * 6, sa * 16 + ny * 6); MAKE(a, ca * 16 - nx * 6, sa * 16 - ny * 6); }
  else { int n = 2 + st->spread; for (int i = 0; i < n; i++) MAKE(a + (i - (n - 1) / 2.f) * 0.075f, ca * 16, sa * 16); }
  if (st->rear >= 1) MAKE(a + PI, -ca * 14, -sa * 14);
  if (st->rear >= 2) { MAKE(a + PI - 0.35f, -ca * 14, -sa * 14); MAKE(a + PI + 0.35f, -ca * 14, -sa * 14); }
#undef MAKE
  C3 col = accent ? c3(1.3f, 1.0f, 0.9f) : COL_bullet;
  if (chance(0.6f * partsBudget)) emit(p->x + ca * 16, p->y + sa * 16, LIFT, ca * 260 + rndr(-60, 60), sa * 260 + rndr(-60, 60), 0, 0.08f, col.r, col.g, col.b, 1.4f, 0.8f, 0.02f, 0);
  sfx_shot(tickTime(tick), accent);
}
static int nearestEnemy(float x, float y, float maxD, float *tx, float *ty, u32 *id);
static void player_beat(void) {
  Player *p = &Game.p; Stats *st = &Game.st;
  if (!p->alive) return;
  for (int i = 0; i < st->orbit; i++) {
    float ox, oy, tx, ty; orbitPos(i, &ox, &oy);
    if (!nearestEnemy(ox, oy, 560, &tx, &ty, NULL)) continue;
    float a = matan2(ty - oy, tx - ox);
    addBullet(ox, oy, ox, oy, mcos(a) * 1100, msin(a) * 1100, st->dmg * 1.5f, 4, 0.8f, 0, 0, 0, 1);
    fx_ring(ox, oy, 4, 16, 0.15f, COL_orbit, 1.2f, 12);
  }
}
static void player_bar(int bar) {
  Player *p = &Game.p; Stats *st = &Game.st;
  if (!p->alive) return;
  if (st->missile) {
    int n = st->missile * 2;
    for (int i = 0; i < n && Game.nmi < ARR(Game.mi); i++) { float a = p->aim + PI + (i - (n - 1) / 2.f) * 0.5f; Missile *m = &Game.mi[Game.nmi++]; m->x = p->x; m->y = p->y; m->vx = mcos(a) * 420; m->vy = msin(a) * 420; m->t = 0; m->life = 3; m->target = 0; m->dmg = 4 * st->dmg; m->delay = i * 0.05f; }
  }
  if (st->nova && bar % (st->nova >= 2 ? 2 : 4) == 0) {
    const float R = 300;
    fx_ring(p->x, p->y, 20, R, 0.5f, c3(0.6f, 1.2f, 1.6f), 3, 64); fx_ring2(p->x, p->y, 20, R, 0.7f, c3(1.4f, 0.4f, 1.4f), 2, 64, 1, 1); fx_wave(p->x, p->y, R, 0.5f, 1.2f);
    grid_explode(160, p->x, p->y, 380, -80);
    for (int i = 0; i < Game.nen; i++) { Enemy *e = &Game.en[i]; if (!e->dead && e->spawn <= 0 && dist2(e->x, e->y, p->x, p->y) < (R + e->r) * (R + e->r)) damageEnemy(e, 4 * st->dmg, 0, 0, CA_NOVA); }
    { float bx, by, br; if (boss_targetable() && boss_pos(&bx, &by, &br) && dist2(bx, by, p->x, p->y) < (R + br) * (R + br)) boss_damage(8 * st->dmg, CA_NOVA); }
    sfx_explode(0.8f, p->x);
  }
  if (st->shield && p->shield < 1 && ++p->shieldBars >= (st->shield >= 2 ? 4 : 8)) {
    p->shield = 1; p->shieldBars = 0; fx_ring(p->x, p->y, 60, 26, 0.4f, COL_shield, 2, 6); fx_text(p->x, p->y - 38, "SHIELD", 11, COL_shield, 0.7f);
  }
}
static void player_draw(Batch *b, Batch *fb) {
  Player *p = &Game.p; Stats *st = &Game.st;
  if (!p->alive) return;
  int blink = p->inv > 0 && p->dashT <= 0 && (int)mfloor(Game.time * 18) % 2 == 0;
  float k = blink ? 0.3f : 1.6f;
  for (int i = 0; i < p->ntrail; i++) { Trail *t = &p->trail[i]; float a = (1 - t->t / 0.25f) * 0.5f; POLY(b, SH_player, t->x, t->y, t->a, 1, 1.2f, c3(1.4f * a, 0.5f * a, 1.6f * a)); }
  POLY(b, SH_player, p->x, p->y, p->ang, 1, 1.9f, cmul(COL_player, k));
  float hk = 1.3f + 0.5f * Game.bt;
  bdot(b, p->x, p->y, p->hitR + 1.2f, c3(0.9f * hk, 0.25f * hk, 0.6f * hk)); bdot(b, p->x, p->y, p->hitR - 0.6f, c3(1.9f * hk, 1.9f * hk, 1.9f * hk));
  if (Game.bt > 0.05f) bcircle(b, p->x, p->y, p->hitR + 5, 16, 0.8f, c3(0.6f * Game.bt, 1.2f * Game.bt, 1.5f * Game.bt), 0);
  if (p->shield > 0) bngon(b, p->x, p->y, 27 + Game.beatPulse * 3, 6, Game.time * 0.8f, 1.3f, cmul(COL_shield, 0.9f));
  /* metronome ring on the floor: closes on the ship exactly on the beat (dash timing aid) */
  fb->z = 1.5f;
  float cost = DASH_COST * mpow(0.8f, (float)st->dashDmg); int ready = p->energy >= cost && p->dashCd <= 0;
  if (ready) {
    float f = (float)(Game.visBeat - mfloor((float)Game.visBeat)), r = 24 + 62 * (1 - f), a = 0.12f + f * f * 0.75f;
    bcircle(fb, p->x, p->y, r, 40, 1.4f, c3(1.2f * a, 0.45f * a, 1.3f * a), 0);
    bcircle(fb, p->x, p->y, 24, 40, 0.8f, c3(0.25f, 0.12f, 0.35f), 0);
    if (f < 0.14f) { float fl = (1 - f / 0.14f) * 0.9f; bcircle(fb, p->x, p->y, 24, 40, 1.8f, c3(1.3f * fl, 1.15f * fl, 0.6f * fl), 0); }
  }
  float ef = p->energy / E_MAX; C3 ec = Game.btOn ? c3(1.5f, 0.4f, 1.4f) : ready ? c3(0.4f, 1.1f, 1.5f) : c3(0.6f, 0.25f, 0.4f);
  barc(fb, p->x, p->y, 30, -PI / 2, -PI / 2 + TAU * ef, 40, 1.2f, cmul(ec, 0.7f));
  fb->z = LIFT;
  for (int i = 0; i < st->orbit; i++) { float ox, oy; orbitPos(i, &ox, &oy); bngon(b, ox, oy, 7, 4, Game.time * 4, 1.4f, cmul(COL_orbit, 1.5f)); bdot(b, ox, oy, 2, cmul(COL_orbit, 2)); }
}

/* ---------------- projectiles ---------------- */
typedef struct { Bullet *b; } BuCtx;
static int bullet_cb(Hit *h, void *ctx) {
  Bullet *b = ((BuCtx *)ctx)->b;
  if (!(h->f & (HF_VULN | HF_SOLID)) || hit_dead(h)) return 1;
  u32 id = hit_id(h); for (int i = 0; i < b->nhit; i++) if (b->hit[i] == id) return 1;
  if (segDist2(b->px, b->py, b->x, b->y, h->x, h->y) > (h->r + b->r) * (h->r + b->r)) return 1;
  float s = mhypot(b->vx, b->vy); if (s == 0) s = 1;
  if (h->f & HF_VULN) {
    hitHittable(h, b->dmg, b->vx / s * 40, b->vy / s * 40, CA_SHOT);
    BurstO o = BOang(matan2(-b->vy, -b->vx), 1.6f); burst(b->x, b->y, b->accent ? 7 : 3, b->accent ? COL_accent : COL_bullet, 80, 300, 0.08f, 0.2f, &o);
    if (b->pierce > 0) { b->pierce--; if (b->nhit < 6) b->hit[b->nhit++] = id; return 1; }
    b->life = 0; return 0;
  }
  b->life = 0;
  BurstO o = BOang(matan2(-b->vy, -b->vx), 2); burst(b->x, b->y, 4, c3(0.9f, 0.9f, 1.1f), 60, 220, 0.1f, 0.2f, &o);
  return 0;
}
typedef struct { float x, y; int hit; } MiCtx;
static int missile_probe(Hit *h, void *ctx) { MiCtx *m = ctx; if ((h->f & (HF_VULN | HF_SOLID)) && !hit_dead(h) && dist2(m->x, m->y, h->x, h->y) < (h->r + 8) * (h->r + 8)) { m->hit = 1; return 0; } return 1; }
typedef struct { float x, y, R, dmg; int cause; } AoeCtx;
static int aoe_cb(Hit *h, void *ctx) { AoeCtx *a = ctx; if ((h->f & HF_VULN) && !hit_dead(h) && dist2(a->x, a->y, h->x, h->y) < (a->R + h->r) * (a->R + h->r)) hitHittable(h, a->dmg, 0, 0, a->cause); return 1; }
static void updateBullets(float wdt, float pdt) {
  Stats *st = &Game.st; float dt = pdt;
  for (int i = 0; i < Game.nbu; i++) {
    Bullet *b = &Game.bu[i];
    b->life -= dt; b->px = b->x; b->py = b->y;
    if (st->homing && !b->drone) {
      float tx, ty;
      if (nearestEnemy(b->x, b->y, 380, &tx, &ty, NULL)) {
        float cur = matan2(b->vy, b->vx), s = mhypot(b->vx, b->vy), lim = st->homing * 5 * dt;
        float na = cur + clampf(wrapAngle(matan2(ty - b->y, tx - b->x) - cur), -lim, lim);
        b->vx = mcos(na) * s; b->vy = msin(na) * s;
      }
    }
    b->vx += field_ax(b->x) * dt * 1.4f; b->vy += field_ay(b->y) * dt * 1.4f;
    for (int w = 0; w < Game.nwells; w++) { Well *W = &Game.wells[w]; float dx = W->x - b->x, dy = W->y - b->y, d2 = dx * dx + dy * dy; if (d2 < W->pr * W->pr) { float d = msqrt(d2); if (d == 0) d = 1; float a = W->bpull * 3e7f / (d2 + 6000) * dt; b->vx += dx / d * a; b->vy += dy / d * a; } }
    b->x += b->vx * dt; b->y += b->vy * dt;
    int wall = 0;
    if (b->x < 0 || b->x > ARENA_W) { if (b->bounce > 0) { b->bounce--; b->vx = -b->vx; b->x = clampf(b->x, 0, ARENA_W); b->nhit = 0; } else wall = 1; }
    if (b->y < 0 || b->y > ARENA_H) { if (b->bounce > 0) { b->bounce--; b->vy = -b->vy; b->y = clampf(b->y, 0, ARENA_H); b->nhit = 0; } else wall = 1; }
    if (wall) {
      b->life = 0; b->x = clampf(b->x, 0, ARENA_W); b->y = clampf(b->y, 0, ARENA_H);
      BurstO o = BO(); o.hasFlags = 1; o.flags = 0; burst(b->x, b->y, 3, COL_bullet, 60, 260, 0.1f, 0.25f, &o);
      grid_explode(5, b->x, b->y, 60, 0);
      continue;
    }
    if (b->life <= 0) continue;
    float r = b->r; BuCtx c = { b };
    hash_query(minf(b->px, b->x) - r - 40, minf(b->py, b->y) - r - 40, maxf(b->px, b->x) + r + 40, maxf(b->py, b->y) + r + 40, bullet_cb, &c);
    if (b->life > 0) grid_explode(0.9f, b->x, b->y, 45, 0);
  }
  { int j = 0; for (int i = 0; i < Game.nbu; i++) if (Game.bu[i].life > 0) Game.bu[j++] = Game.bu[i]; Game.nbu = j; }
  Player *p = &Game.p;
  dt = wdt;
  for (int i = 0; i < Game.neb; i++) {
    EBullet *b = &Game.eb[i];
    b->life -= dt; b->t += dt;
    b->vx += field_ax(b->x) * dt; b->vy += field_ay(b->y) * dt;
    b->x += b->vx * dt; b->y += b->vy * dt;
    if (b->x < -10 || b->x > ARENA_W + 10 || b->y < -10 || b->y > ARENA_H + 10) { b->life = 0; continue; }
    if (p->alive && p->inv <= 0 && p->dashT <= 0 && dist2(b->x, b->y, p->x, p->y) < (b->r * 0.75f + p->hitR) * (b->r * 0.75f + p->hitR)) { b->life = 0; player_hurt(); }
  }
  { int j = 0; for (int i = 0; i < Game.neb; i++) if (Game.eb[i].life > 0) Game.eb[j++] = Game.eb[i]; Game.neb = j; }
  dt = pdt;
  for (int i = 0; i < Game.nmi; i++) {
    Missile *m = &Game.mi[i];
    if (m->delay > 0) { m->delay -= dt; continue; }
    m->t += dt; m->life -= dt;
    float tx = 0, ty = 0, tr; int has = 0; Enemy *te = m->target && m->target != BOSS_ID ? findEnemy(m->target) : NULL;
    int valid = m->target == BOSS_ID ? boss_live() : (te && !te->dead);
    if (!valid) { u32 id = 0; has = nearestEnemy(m->x, m->y, 1400, &tx, &ty, &id); m->target = has ? id : 0; }
    else if (m->target == BOSS_ID) has = boss_pos(&tx, &ty, &tr);
    else { tx = te->x; ty = te->y; has = 1; }
    float s = minf(900, 420 + m->t * 900), a = matan2(m->vy, m->vx);
    if (has) a += clampf(wrapAngle(matan2(ty - m->y, tx - m->x) - a), -7 * dt, 7 * dt);
    m->vx = mcos(a) * s; m->vy = msin(a) * s; m->x += m->vx * dt; m->y += m->vy * dt;
    if (chance(0.8f * partsBudget)) emit(m->x, m->y, LIFT, -m->vx * 0.1f + rndr(-30, 30), -m->vy * 0.1f + rndr(-30, 30), rndr(0, 40), 0.35f, 0.8f, 0.3f, 0.5f, 1.6f, 0.9f, 0, 8);
    MiCtx mc = { m->x, m->y, m->x < 0 || m->y < 0 || m->x > ARENA_W || m->y > ARENA_H || m->life <= 0 };
    if (!mc.hit) hash_query(m->x - 30, m->y - 30, m->x + 30, m->y + 30, missile_probe, &mc);
    if (mc.hit) {
      m->life = 0; AoeCtx ac = { m->x, m->y, 70, m->dmg, CA_MISSILE };
      hash_query(m->x - 70, m->y - 70, m->x + 70, m->y + 70, aoe_cb, &ac);
      fx_ring(m->x, m->y, 8, 70, 0.3f, COL_missile, 2, 24);
      burst(m->x, m->y, 16, COL_missile, 100, 400, 0.2f, 0.45f, NULL);
      grid_explode(40, m->x, m->y, 120, -30);
      sfx_explode(0.6f, m->x);
    }
  }
  { int j = 0; for (int i = 0; i < Game.nmi; i++) if (Game.mi[i].life > 0) Game.mi[j++] = Game.mi[i]; Game.nmi = j; }
  for (int i = 0; i < Game.nbm; i++) {
    Boom *x = &Game.bm[i];
    x->t += dt;
    if (x->t < 0 || x->done) continue;
    x->done = 1;
    if (x->dmg > 0) { AoeCtx ac = { x->x, x->y, x->r, x->dmg, CA_BOOM }; hash_query(x->x - x->r, x->y - x->r, x->x + x->r, x->y + x->r, aoe_cb, &ac); fx_ring(x->x, x->y, 6, x->r, 0.3f, x->col, 2, 32); }
    burst(x->x, x->y, 14, x->col, 80, 360, 0.2f, 0.5f, NULL);
    grid_explode(30, x->x, x->y, 110, -30);
    sfx_explode(0.5f, x->x);
  }
  { int j = 0; for (int i = 0; i < Game.nbm; i++) if (!Game.bm[i].done) Game.bm[j++] = Game.bm[i]; Game.nbm = j; }
  { int j = 0; for (int i = 0; i < Game.nbo; i++) { Game.bo[i].t += dt; if (Game.bo[i].t < Game.bo[i].dur) Game.bo[j++] = Game.bo[i]; } Game.nbo = j; }
  if (Game.bombWave.on) {
    float R = Game.bombWave.r += 2100 * dt; float bx = Game.bombWave.x, by = Game.bombWave.y;
    for (int i = 0; i < Game.nen; i++) {
      Enemy *e = &Game.en[i]; if (e->dead || e->bombGen == Game.bombWave.gen) continue;
      if (dist2(e->x, e->y, bx, by) < (R + e->r) * (R + e->r)) { e->bombGen = Game.bombWave.gen; if (e->spawn > 0) e->spawn = 0.01f; damageEnemy(e, 40, 0, 0, CA_BOMB); }
    }
    { float qx, qy, qr; if (!Game.bombWave.bossHit && boss_exists() && boss_pos(&qx, &qy, &qr) && dist2(qx, qy, bx, by) < R * R) { Game.bombWave.bossHit = 1; boss_damage(boss_maxHp() * 0.06f, CA_BOMB); } }
    for (int i = 0; i < Game.neb; i++) if (dist2(Game.eb[i].x, Game.eb[i].y, bx, by) < R * R) Game.eb[i].life = 0;
    if (R > 2400) Game.bombWave.on = 0;
  }
}
static void drawProjectiles(Batch *b) {
  for (int i = 0; i < Game.nbu; i++) {
    Bullet *x = &Game.bu[i]; C3 c = x->accent ? COL_accent : COL_bullet; float s = mhypot(x->vx, x->vy); if (s == 0) s = 1; float L = x->accent ? 34 : 26, k = x->accent ? 2.2f : 1.8f;
    bline(b, x->x, x->y, x->x - x->vx / s * L, x->y - x->vy / s * L, x->accent ? 2.6f : 1.5f, cmul(c, k));
    bdot(b, x->x, x->y, x->accent ? 3.2f : 2.1f, c3(1.6f, 1.5f, 1.3f));
  }
  for (int i = 0; i < Game.neb; i++) {
    EBullet *e = &Game.eb[i]; float pulse = 1 + msin(e->t * 20) * 0.15f;
    bdot(b, e->x, e->y, e->r * 0.45f, c3(1.6f, 1.4f, 1.5f));
    bcircle(b, e->x, e->y, e->r * pulse, 8, 1.5f, cmul(e->col, 1.8f), e->t * 4);
  }
  for (int i = 0; i < Game.nmi; i++) { Missile *m = &Game.mi[i]; if (m->delay <= 0) POLY(b, SH_chevron, m->x, m->y, matan2(m->vy, m->vx), 8, 1.5f, c3(1.8f, 0.7f, 0.8f)); }
  for (int i = 0; i < Game.nbo; i++) {
    Bolt *bo = &Game.bo[i]; float a = 1 - bo->t / bo->dur;
    for (int k = 0; k < 12; k += 2) bline(b, bo->pts[k], bo->pts[k + 1], bo->pts[k + 2], bo->pts[k + 3], 1.6f, cmul(COL_arc, 2 * a));
  }
}
/* =========================================================================
   GUARDIANS — five multi-phase bosses scored to the beat.
     05 PRISM               4/4  · a light-splitting crystal with two counter-rotating shard triads
     10 CAROUSEL            12/8 · a ring of twelve cars, a lead-aiming 3-shooter and CW/CCW sprayers
     15 ECLIPSE             4/4  · a dark disc on three elliptical orbits; time dilation in phase III
     20 ECLIPSE OMEGA       4/4 → 12/8 → dilation · spirals, twice the lasers, a pointed laser you
                            must dash through on the beat, and it dashes at you in phase III
     25 ECLIPSE OMEGA PRIME the last level: a bigger, tankier Omega — danmaku and four times the lasers
   ========================================================================= */
enum { BK_PRISM, BK_CAROUSEL, BK_ECLIPSE, BK_OMEGA, BK_PRIME };
#define BOSS_LAST 25
static const char *BNAMES[5] = { "PRISM", "CAROUSEL", "ECLIPSE", "ECLIPSE OMEGA", "ECLIPSE OMEGA PRIME" };
static const C3 BCOLS[5] = { { 1.0f, 0.85f, 1.1f }, { 1.0f, 0.72f, 0.25f }, { 0.55f, 0.45f, 1.0f }, { 1.0f, 0.24f, 0.48f }, { 1.0f, 0.92f, 1.2f } };
static const char *BSIG[5] = { "4/4 \xB7 D DORIAN", "12/8 \xB7 C# HARMONIC MINOR", "4/4 \xB7 Bb PHRYGIAN", "4/4 \x12 12/8 \x12 DILATION", "DANMAKU \xB7 4/4 \x12 12/8 \x12 DILATION" };
static int boss_kindFor(int wave) { return clampi(mlrint(wave / 5.f) - 1, 0, 4); }
typedef struct { float fire, move, spawn, hp; } BMods;
static BMods boss_mods(int kind) { BMods m = { 1, 1, 1, 1 }; if (kind == BK_OMEGA) { m.move = 1.4f; m.spawn = 1.5f; m.hp = 1.6f; } if (kind == BK_PRIME) { m.move = 1.55f; m.spawn = 2; m.hp = 3.4f; } return m; }
INL int isEcl(int k) { return k >= BK_ECLIPSE; }
INL C3 rainbow(float h, float k) { return cmul(hsl(fractf(h), 0.95f, 0.62f), k); }
static const float SH_shard[] = { 1.35f, 0, 0, 0.42f, -0.6f, 0, 0, -0.42f };
typedef struct { float a, R, dir, spd, hue, hp, max, flash; int alive; } PNode;
typedef struct { float hp, max, r, flash, glow, a, x, y; int alive; } Car;
typedef struct { float a, dir, fa, x, y; int on; } Sprayer;
typedef struct { float tilt, A, Bm, dir, phi; int moons; } Orbit;
typedef struct { float angles[5]; int nang, state; float rot, spin; double b0; float warnB, durB, wf, ff, w; C3 col; } Laser;
typedef struct { double fireBeat, lockBeat; float ang, t, w, dur; int state; C3 col; } Aimed;
static struct {
  int on, kind, phase, dead;
  float x, y, vx, vy, t, spawn, spawnMax, deathT, flash, shiftT, scale, r, hp, maxHp; BMods mods;
  PNode nodes[6]; float spin, tx, ty, charge, cx, cy, warn, warnMax, breathe;
  float rot, R, dir; Car segs[12]; Sprayer spr[2];
  Orbit orbits[3]; Laser las[8]; int nlas; Aimed aim[6]; int naim; float orb, spiral, sa, sb, hue;
} Boss;
static int boss_exists(void) { return Boss.on; }
static int boss_live(void) { return Boss.on && !Boss.dead; }
static int boss_targetable(void) { return Boss.on && !Boss.dead && Boss.spawn <= 0; }
static int boss_pos(float *x, float *y, float *r) { if (!Boss.on) return 0; *x = Boss.x; *y = Boss.y; *r = Boss.r; return 1; }
static float boss_maxHp(void) { return Boss.maxHp; }
static int attackLive(void) { return Boss.on && !Boss.dead && Boss.spawn <= 0 && Boss.shiftT <= 0; }

static void boss_spawn(int kind, float scale) {
  mzero(&Boss, sizeof Boss);
  BMods mods = boss_mods(kind); float sp = spb(), hs = scale * mods.hp;
  Boss.on = 1; Boss.kind = kind; Boss.x = ARENA_W / 2; Boss.y = ARENA_H / 2 - 120; Boss.phase = 1; Boss.spawn = Boss.spawnMax = sp * 6; Boss.scale = scale; Boss.mods = mods; Boss.r = 48;
  if (kind == BK_PRISM) {
    Boss.hp = 620 * hs; Boss.r = 44;
    static const float hues[6] = { 0, 0.33f, 0.66f, 0.12f, 0.5f, 0.82f };
    for (int i = 0; i < 6; i++) { PNode *n = &Boss.nodes[i]; n->a = (i % 3) * TAU / 3 + (i >= 3 ? PI / 3 : 0); n->R = i < 3 ? 100 : 178; n->dir = i < 3 ? 1 : -1; n->spd = i < 3 ? 1 : 0.6f; n->hue = hues[i]; n->hp = n->max = 45 * hs; n->alive = 1; }
    Boss.tx = ARENA_W / 2; Boss.ty = ARENA_H / 2; Boss.breathe = 1;
  } else if (kind == BK_CAROUSEL) {
    Boss.hp = 800 * hs; Boss.r = 34; Boss.y = ARENA_H / 2 - 40; Boss.R = 160; Boss.dir = 1;
    for (int i = 0; i < 12; i++) { Car *c = &Boss.segs[i]; c->hp = c->max = 26 * hs; c->alive = 1; c->r = 17; c->a = i * TAU / 12; c->x = Boss.x; c->y = Boss.y; }
    Boss.spr[0].a = 0; Boss.spr[0].dir = 1; Boss.spr[0].fa = 0; Boss.spr[1].a = PI; Boss.spr[1].dir = -1; Boss.spr[1].fa = PI;
  } else {
    int om = kind != BK_ECLIPSE, pr = kind == BK_PRIME;
    Boss.hp = (pr ? 1300 : 900) * hs; Boss.r = pr ? 92.f : om ? 66.f : 58.f; Boss.y = ARENA_H / 2;
    for (int k = 0; k < 3; k++) { Orbit *o = &Boss.orbits[k]; o->tilt = k * PI / 3; o->A = pr ? 230.f : om ? 172.f : 150.f; o->Bm = pr ? 96.f : om ? 74.f : 62.f; o->dir = k % 2 ? -1 : 1; o->phi = k * 1.1f; o->moons = pr ? 3 : 2; }
  }
  Boss.maxHp = Boss.hp;
  sfx_bossRoar(); cam_shake(0.6f);
  fx_wave(Boss.x, Boss.y, 900, 1.6f, 2);
  grid_explode(400, Boss.x, Boss.y, 800, -200);
}
static int phaseOf(void) { return Boss.hp > Boss.maxHp * 0.66f ? 1 : Boss.hp > Boss.maxHp * 0.33f ? 2 : 3; }
static void boss_kill(void);
static void setBanner(const char *t, const char *s, float dur, C3 col) { s_cpy(Game.banner.text, t); s_cpy(Game.banner.sub, s); Game.banner.t = 0; Game.banner.dur = dur; Game.banner.col = col; Game.hasBanner = 1; }
static int boss_damage(float dmg, int cause) {
  if (!Boss.on || Boss.dead || Boss.spawn > 0 || Boss.shiftT > 0) return 0;
  Boss.hp -= dmg; Boss.flash = 0.04f;
  if (cause != CA_BOMB) sfx_hit(Boss.x);
  if (Boss.hp <= 0) { boss_kill(); return 1; }
  int np = phaseOf();
  if (np != Boss.phase) {
    Boss.phase = np; Boss.shiftT = spb() * 2;
    sfx_bossRoar(); cam_shake(0.7f); fx_wave(Boss.x, Boss.y, 700, 1.0f, 2.2f);
    fx_ring(Boss.x, Boss.y, Boss.r, 600, 0.8f, BCOLS[Boss.kind], 4, 80);
    grid_explode(200, Boss.x, Boss.y, 650, -100);
    for (int i = 0; i < Game.neb; i++) Game.eb[i].life = 0;
    char buf[16]; s_printf(buf, "PHASE %s", roman(np)); fx_text(Boss.x, Boss.y - Boss.r - 50, buf, 22, cmul(BCOLS[Boss.kind], 1.6f), 1.4f);
    if (Boss.kind == BK_PRISM && np == 3) for (int i = 0; i < 6; i++) if (!Boss.nodes[i].alive) { Boss.nodes[i].alive = 1; Boss.nodes[i].hp = Boss.nodes[i].max * 0.5f; }
    if (Boss.kind == BK_CAROUSEL) for (int i = 0; i < 2; i++) if (np >= 2 + i) fx_ring(Boss.x, Boss.y, 80, 10, 0.5f, c3(1.6f, 1.4f, 0.8f), 2, 24);
    if (Boss.kind == BK_OMEGA || Boss.kind == BK_PRIME) {
      Score s; mzero(&s, sizeof s); s.has = SC_STEPS; s.steps = np == 2 ? 3 : 4; setScore(&s);
      if (np == 2) setBanner("TRIPLET STORM", "DASH THROUGH THE POINTED LASER ON THE BEAT", 2.6f, c3(1.7f, 0.5f, 0.9f));
    }
    if (isEcl(Boss.kind) && np == 3) setBanner("TIME DILATION", Boss.kind == BK_ECLIPSE ? "THE CLOSER YOU FALL, THE FASTER THE BEAT" : "THE CLOSER YOU FALL, THE FASTER THE BEAT \xB7 IT DASHES", 2.6f, c3(0.9f, 0.6f, 1.7f));
  }
  return 0;
}
static void nodePos(int i, float *x, float *y) { PNode *n = &Boss.nodes[i]; float a = n->a + Boss.spin * n->dir * n->spd, R = n->R * Boss.breathe; *x = Boss.x + mcos(a) * R; *y = Boss.y + msin(a) * R; }
static void partBlast(float x, float y, C3 col, float s) {
  blast(x, y, 40 * s, col, 600, 0.8f);
  fx_ring(x, y, 10, 90 * s, 0.4f, col, 2, 32);
  grid_explode(90 * s, x, y, 220, -30);
  sfx_explode(1.3f * s, x); cam_shake(0.18f);
  for (int i = 0; i < 3; i++) spawnPickup(PK_GEOM, x, y);
}
static int boss_damagePart(int part, float dmg, int cause) {
  if (part == 0) return boss_damage(dmg, cause);
  if (!Boss.on || Boss.dead || Boss.spawn > 0 || part < 0) return 0;
  if (Boss.kind == BK_PRISM) {
    PNode *n = &Boss.nodes[part - 1]; if (part > 6 || !n->alive) return 0;
    n->hp -= dmg; n->flash = 0.06f; sfx_hit(Boss.x);
    if (n->hp <= 0) { n->alive = 0; float x, y; nodePos(part - 1, &x, &y); partBlast(x, y, rainbow(n->hue, 1.25f), 1); addScore(500LL * Game.mult); boss_damage(Boss.maxHp * 0.03f, CA_PART); }
  } else if (Boss.kind == BK_CAROUSEL) {
    Car *s = &Boss.segs[part - 1]; if (part > 12 || !s->alive) return 0;
    s->hp -= dmg; s->flash = 0.06f; sfx_hit(s->x);
    if (s->hp <= 0) { s->alive = 0; partBlast(s->x, s->y, BCOLS[Boss.kind], 0.8f); addScore(300LL * Game.mult); boss_damage(Boss.maxHp * 0.03f, CA_PART); }
  }
  return 0;
}
static void boss_kill(void) {
  Boss.dead = 1; Boss.deathT = 0; Boss.hp = 0;
  for (int i = 0; i < Game.neb; i++) Game.eb[i].life = 0;
  Boss.nlas = 0; Boss.naim = 0;
  S.warpOn = 0;
  sfx_bossRoar(); game_slow(0.3f, 1.2f);
}
typedef void (*MoonFn)(float x, float y, int k);
static void moons(MoonFn fn) {
  for (int k = 0; k < 3; k++) {
    Orbit *o = &Boss.orbits[k]; float c = mcos(o->tilt + Boss.spin * 0.15f), s = msin(o->tilt + Boss.spin * 0.15f);
    for (int m = 0; m < o->moons; m++) { float ph = o->phi + m * TAU / o->moons, ex = mcos(ph) * o->A, ey = msin(ph) * o->Bm; fn(Boss.x + ex * c - ey * s, Boss.y + ex * s + ey * c, k); }
  }
}
static void placeCars(void) {
  for (int i = 0; i < 12; i++) { Car *sg = &Boss.segs[i]; float a = Boss.rot + sg->a; sg->x = Boss.x + mcos(a) * Boss.R; sg->y = Boss.y + msin(a) * Boss.R; }
  for (int i = 0; i < 2; i++) { Sprayer *s = &Boss.spr[i]; float a = -Boss.rot * 0.6f + s->a; s->x = Boss.x + mcos(a) * 58; s->y = Boss.y + msin(a) * 58; }
}
static void game_onBossDefeated(void);
static void boss_update(float dt) {
  if (!Boss.on) return;
  Boss.t += dt; Boss.flash = maxf(0, Boss.flash - dt); Boss.shiftT = maxf(0, Boss.shiftT - dt);
  C3 bcol = BCOLS[Boss.kind]; int rb = Boss.kind == BK_PRIME || Boss.kind == BK_PRISM;
  if (Boss.dead) {
    Boss.deathT += dt;
    if (frand() < dt * 14) {
      float a = rnd(TAU), r = rnd(Boss.r * 2.5f), x = Boss.x + mcos(a) * r, y = Boss.y + msin(a) * r; C3 col = rb ? rainbow(frand(), 1.25f) : bcol;
      blast(x, y, 30, col, 700, 0.8f); fx_ring(x, y, 5, rndr(40, 110), 0.35f, col, 2, 24);
      sfx_explode(rndr(0.8f, 1.6f), x); cam_shake(0.15f); grid_explode(80, x, y, 200, -30);
    }
    if (Boss.deathT > 2.2f) {
      blast(Boss.x, Boss.y, 400, bcol, 2000, 2.4f);
      burst(Boss.x, Boss.y, 150, c3(1.5f, 1.5f, 1.5f), 600, 2400, 0.4f, 1.0f, NULL);
      for (int i = 0; i < 5; i++) fx_ring2(Boss.x, Boss.y, 20, 400 + i * 260.f, 0.9f + i * 0.25f, Boss.kind == BK_PRIME ? rainbow(i / 5.f, 1.6f) : cmul(bcol, 1.5f), 4, 96, 1, i % 2 ? 1 : LIFT);
      fx_wave(Boss.x, Boss.y, 1600, 1.8f, 3.2f);
      grid_explode(1200, Boss.x, Boss.y, 1300, -400);
      cam_shake(1.2f); Game.flash = Set.flash ? 0.8f : 0.25f; Game.flashCol = c3(1, 0.85f, 1);
      sfx_bomb(0); S.gate[G_EX] = 0; sfx_explode(3, Boss.x);
      i64 pts = (i64)(50000 * Boss.scale * Boss.mods.hp + 0.5f) * Game.mult;
      addScore(pts);
      char buf[32]; s_fmt(buf, pts); fx_text(Boss.x, Boss.y - 30, buf, 30, c3(1.6f, 1.5f, 0.6f), 2.2f);
      for (int i = 0; i < 30; i++) spawnPickup(PK_GEOM, Boss.x + rndr(-40, 40), Boss.y + rndr(-40, 40));
      spawnPickup(PK_HEAL, Boss.x, Boss.y); spawnPickup(PK_BOMB, Boss.x, Boss.y);
      Game.kills++; Boss.on = 0;
      game_onBossDefeated();
    }
    return;
  }
  if (Boss.spawn > 0) {
    Boss.spawn -= dt;
    if (chance(partsBudget)) for (int i = 0; i < 4; i++) { float a = rnd(TAU), r = rndr(200, 420); C3 c = rb ? rainbow(frand(), 1.5f) : cmul(bcol, 1.5f); emit(Boss.x + mcos(a) * r, Boss.y + msin(a) * r, LIFT + rnd(80), -mcos(a) * r * 1.6f, -msin(a) * r * 1.6f, 0, 0.6f, c.r, c.g, c.b, 1.4f, 0.99f, 0.03f, 6); }
    grid_implode(30, Boss.x, Boss.y, 500);
    if (Boss.kind == BK_CAROUSEL) placeCars();
    return;
  }
  Player *p = &Game.p; float mdt = dt * Boss.mods.move, margin = Boss.r;
  if (Boss.kind == BK_PRISM) {
    Boss.spin += mdt * TAU / (spb() * (Boss.phase == 1 ? 8 : Boss.phase == 2 ? 5 : 3.5f));
    Boss.breathe = damp(Boss.breathe, Boss.phase == 3 ? 1 + 0.12f * msin(Boss.t * 2) : 1, 3, dt);
    if (Boss.charge > 0) { Boss.charge -= dt; Boss.vx = Boss.cx * 760; Boss.vy = Boss.cy * 760; grid_directed(Boss.vx * 0.01f, Boss.vy * 0.01f, 0, Boss.x, Boss.y, 150); }
    else { float dx = Boss.tx - Boss.x, dy = Boss.ty - Boss.y, m = mhypot(dx, dy); if (m == 0) m = 1; Boss.vx = damp(Boss.vx, dx / m * minf(90, m), 2, dt); Boss.vy = damp(Boss.vy, dy / m * minf(90, m), 2, dt); }
    Boss.warn = maxf(0, Boss.warn - dt);
    for (int i = 0; i < 6; i++) Boss.nodes[i].flash = maxf(0, Boss.nodes[i].flash - dt);
  } else if (Boss.kind == BK_CAROUSEL) {
    float vb = (float)Game.visBeat, f = vb - mfloor(vb), lunge = (1 - f) * (1 - f) * (1 - f);
    Boss.rot += Boss.dir * mdt * TAU / (spb() * 8) * (0.5f + 2.4f * lunge) * (Boss.shiftT > 0 ? 0.3f : 1) * (1 + 0.3f * (Boss.phase - 1));
    Boss.R = damp(Boss.R, 150 + 34 * msin(vb / 4 * TAU) + (Boss.phase == 3 ? 30 : 0), 3, dt);
    static const float SP[4] = { 0, 55, 75, 95 };
    float dx = p->x - Boss.x, dy = p->y - Boss.y, m = mhypot(dx, dy); if (m == 0) m = 1; float s = SP[Boss.phase] * (0.4f + lunge);
    Boss.vx = damp(Boss.vx, dx / m * s, 2, dt); Boss.vy = damp(Boss.vy, dy / m * s, 2, dt);
    margin = minf(Boss.R * 0.75f, ARENA_H * 0.4f);
    Boss.spr[0].on = Boss.phase >= 2; Boss.spr[1].on = Boss.phase >= 3;
    placeCars();
    for (int i = 0; i < 12; i++) { Boss.segs[i].flash = maxf(0, Boss.segs[i].flash - dt); Boss.segs[i].glow = maxf(0, Boss.segs[i].glow - dt * 3); }
    grid_directed(Boss.vx * 0.006f, Boss.vy * 0.006f, 0, Boss.x, Boss.y, 120);
  } else {
    int pr = Boss.kind == BK_PRIME, om = Boss.kind != BK_ECLIPSE;
    Boss.orb += mdt; Boss.spiral += mdt * (pr ? 1.3f : 0.9f); Boss.hue += dt * 0.12f;
    float tx = ARENA_W / 2 + mcos(Boss.orb * 0.17f) * (pr ? 170 : 240), ty = ARENA_H / 2 + msin(Boss.orb * 0.23f) * (pr ? 100 : 150);
    if (Boss.charge > 0) { Boss.charge -= dt; Boss.vx = Boss.cx * (pr ? 980 : 880); Boss.vy = Boss.cy * (pr ? 980 : 880); grid_directed(Boss.vx * 0.012f, Boss.vy * 0.012f, 0, Boss.x, Boss.y, 200); if (chance(0.6f)) { BurstO o = BOang(matan2(-Boss.cy, -Boss.cx), 1); burst(Boss.x, Boss.y, 3, bcol, 60, 200, 0.3f, 0.6f, &o); } }
    else { Boss.vx = (tx - Boss.x) * 1.5f; Boss.vy = (ty - Boss.y) * 1.5f; }
    Boss.spin += mdt * TAU / (spb() * (Boss.phase == 3 ? 2 : 4));
    for (int k = 0; k < 3; k++) Boss.orbits[k].phi += Boss.orbits[k].dir * mdt * TAU / (spb() * (pr ? 3 : 4));
    Boss.warn = maxf(0, Boss.warn - dt);
    grid_implode(35 + Boss.phase * 8.f + (om ? 10 : 0), Boss.x, Boss.y, 420 + (pr ? 120.f : 0));
    /* gravitational time dilation: the music's tempo tracks how far you've fallen in */
    if (Boss.phase == 3 && p->alive) { S.warpOn = 1; S.warpTarget = (float)mlrint(lerpf(155, 90, clampf((mhypot(p->x - Boss.x, p->y - Boss.y) - 180) / 620, 0, 1))); }
    else S.warpOn = 0;
    /* sweeping lasers live on the beat grid: two beats of warning, then they fire on the beat */
    double gb = gameBeat();
    for (int i = 0; i < Boss.nlas; i++) {
      Laser *L = &Boss.las[i]; float e = (float)(gb - L->b0); L->wf = clampf(e / L->warnB, 0, 1); L->ff = clampf((e - L->warnB) / L->durB, 0, 1);
      if (L->state == 0 && e >= L->warnB) { L->state = 1; sfx_laser(L->durB * spb()); cam_shake(pr ? 0.18f : 0.3f); }
      if (L->state == 1) {
        L->rot += L->spin * mdt;
        if (p->alive) for (int a = 0; a < L->nang; a++) {
          float ang = L->angles[a] + L->rot, x1 = Boss.x + mcos(ang) * 2600, y1 = Boss.y + msin(ang) * 2600;
          if (segDist2(Boss.x, Boss.y, x1, y1, p->x, p->y) < (L->w + p->hitR) * (L->w + p->hitR)) player_hurt();
          if (chance(0.4f * partsBudget)) { float r = rndr(Boss.r, 1400); emit(Boss.x + mcos(ang) * r, Boss.y + msin(ang) * r, LIFT, rndr(-200, 200), rndr(-200, 200), rndr(50, 300), 0.4f, L->col.r, L->col.g, L->col.b, 1.4f, 0.92f, 0.03f, 17); }
        }
      }
    }
    { int j = 0; for (int i = 0; i < Boss.nlas; i++) if (!(Boss.las[i].state == 1 && Boss.las[i].ff >= 1)) Boss.las[j++] = Boss.las[i]; Boss.nlas = j; }
    /* the pointed laser: tracks you, locks a split second before the beat, then fires too wide to walk out of */
    double vb = Game.visBeat;
    for (int i = 0; i < Boss.naim; i++) {
      Aimed *A = &Boss.aim[i];
      if (A->state == 0) { A->ang = lerpAngle(A->ang, matan2(p->y - Boss.y, p->x - Boss.x), minf(1, dt * 7)); if (vb >= A->lockBeat) { A->state = 1; sfx_ui(UI_MOVE); } }
      if (A->state == 1 && vb >= A->fireBeat) { A->state = 2; A->t = 0; sfx_laser(A->dur + 0.15f); cam_shake(0.4f); cam_punch(0.02f); }
      if (A->state == 2) {
        A->t += dt;
        float x1 = Boss.x + mcos(A->ang) * 2600, y1 = Boss.y + msin(A->ang) * 2600;
        if (p->alive && segDist2(Boss.x, Boss.y, x1, y1, p->x, p->y) < (A->w + p->hitR) * (A->w + p->hitR)) player_hurt();
        if (chance(0.8f * partsBudget)) { float r = rndr(Boss.r, 1600); emit(Boss.x + mcos(A->ang) * r, Boss.y + msin(A->ang) * r, LIFT, rndr(-300, 300), rndr(-300, 300), rndr(100, 400), 0.5f, A->col.r, A->col.g, A->col.b, 1.6f, 0.92f, 0.03f, 17); }
      }
    }
    { int j = 0; for (int i = 0; i < Boss.naim; i++) if (!(Boss.aim[i].state == 2 && Boss.aim[i].t >= Boss.aim[i].dur)) Boss.aim[j++] = Boss.aim[i]; Boss.naim = j; }
  }
  Boss.x = clampf(Boss.x + Boss.vx * mdt, margin, ARENA_W - margin); Boss.y = clampf(Boss.y + Boss.vy * mdt, margin, ARENA_H - margin);
}
static int boss_wellInfo(Well *w) {
  if (!Boss.on || !isEcl(Boss.kind) || Boss.dead || Boss.spawn > 0) return 0;
  w->boss = 1; w->x = Boss.x; w->y = Boss.y; w->r = Boss.r; w->pr = 1100; w->pull = 1.6f; w->ppull = 150 + Boss.phase * 40.f; w->bpull = 1.2f; return 1;
}

/* ---- musical attack scheduling ---- */
static void radial(float x, float y, int n, float off, float spd, int hued, float hue) {
  for (int i = 0; i < n; i++) { float a = off + i * TAU / n; enemyShot(x + mcos(a) * 30, y + msin(a) * 30, a, spd, 7, hued ? rainbow(hue + (float)i / n, 1.25f) : COL_ebullet); }
}
static void gapRing(int n, float spd, float gapAt, float gapW, float hue) {
  for (int i = 0; i < n; i++) { float a = i * TAU / n; if (mfabs(wrapAngle(a - gapAt)) < gapW) continue; enemyShot(Boss.x + mcos(a) * Boss.r, Boss.y + msin(a) * Boss.r, a, spd, 5, rainbow(hue + (float)i / n, 1.25f)); }
}
static void aimLaser(double fireBeat) {
  if (Boss.naim >= 6) return;
  Player *p = &Game.p; Aimed *A = &Boss.aim[Boss.naim++];
  A->fireBeat = fireBeat; A->lockBeat = fireBeat - 0.3; A->ang = matan2(p->y - Boss.y, p->x - Boss.x); A->state = 0; A->t = 0; A->w = 54; A->dur = 0.32f;
  A->col = Boss.kind == BK_PRIME ? c3(1.6f, 1.5f, 1.7f) : c3(1.7f, 0.3f, 0.6f);
  sfx_charge(spb() * 2.6f, Boss.x);
}
static void sweepLasers(i64 beat) {
  int pr = Boss.kind == BK_PRIME;
  if ((pr && Boss.nlas >= 4) || Boss.nlas >= 8) return;
  static const int NB[4] = { 0, 3, 4, 5 };
  int n = NB[Boss.phase]; float off = rnd(TAU);
  Laser *L = &Boss.las[Boss.nlas++]; mzero(L, sizeof *L);
  for (int i = 0; i < n; i++) L->angles[i] = off + i * TAU / n;
  L->nang = n; L->spin = (chance(0.5f) ? 1 : -1) * (Boss.phase == 1 ? 0.3f : 0.35f); L->state = 0; L->b0 = (double)beat; L->warnB = 2; L->durB = pr ? 1.2f : 1.6f; L->w = pr ? 12.f : 15.f;
  L->col = pr ? rainbow(frand(), 1.3f) : Boss.kind == BK_OMEGA ? c3(1.3f, 0.3f, 0.7f) : c3(1.1f, 0.35f, 1.25f);
  sfx_charge(spb() * 2, Boss.x);
}
/** Called on every beat. */
static void boss_beat(i64 beat) {
  if (!attackLive()) return;
  Player *p = &Game.p;
  if (Boss.kind == BK_PRISM) {
    if (Boss.phase == 1 && beat % 2 == 0) radial(Boss.x, Boss.y, 12, beat * 0.13f, 210, 1, beat * 0.07f);
    if (Boss.phase == 3) {
      if (beat % 2 == 0) radial(Boss.x, Boss.y, 16, (beat % 4) * PI / 16, 230, 1, beat * 0.05f);
      if (beat % 8 == 6) { Boss.warn = Boss.warnMax = spb(); sfx_charge(Boss.warn * 0.95f, Boss.x); }
      if (beat % 8 == 7) { float dx = p->x - Boss.x, dy = p->y - Boss.y, m = mhypot(dx, dy); if (m == 0) m = 1; Boss.cx = dx / m; Boss.cy = dy / m; Boss.charge = 0.55f; cam_shake(0.3f); }
    }
    if (beat % 8 == 0) { Boss.tx = rndr(450, ARENA_W - 450); Boss.ty = rndr(320, ARENA_H - 320); }
  } else if (Boss.kind == BK_CAROUSEL) {
    float spd = 320, t = mhypot(p->x - Boss.x, p->y - Boss.y) / spd;
    float lx = clampf(p->x + p->vx * t, 0, ARENA_W), ly = clampf(p->y + p->vy * t, 0, ARENA_H), a = matan2(ly - Boss.y, lx - Boss.x);
    for (int i = -1; i <= 1; i++) enemyShot(Boss.x + mcos(a) * Boss.r, Boss.y + msin(a) * Boss.r, a + i * 0.13f, spd, 8, c3(1.4f, 1.1f, 0.4f));
  } else {
    int pr = Boss.kind == BK_PRIME, om = Boss.kind == BK_OMEGA;
    static const int EV[3][3] = { { 2, 2, 1 }, { 4, 4, 2 }, { 8, 8, 4 } };
    int every = EV[pr ? 0 : om ? 1 : 2][Boss.phase - 1];
    if (beat % every == 0) sweepLasers(beat);
    if (!pr) {
      if (Boss.phase == 1 && beat % 2 == 0) radial(Boss.x, Boss.y, 10, beat * 0.21f, 200, 0, 0);
      if (Boss.phase == 2 && beat % 2 == 0) radial(Boss.x, Boss.y, 12, beat * 0.17f, 210, 0, 0);
    } else {
      if (beat % 2 == 0) gapRing(28, 150, matan2(p->y - Boss.y, p->x - Boss.x) + PI * ((beat % 4) / 2.f), 0.32f, Boss.hue);
      fx_ring2(Boss.x, Boss.y, Boss.r, Boss.r * 3.6f, spb() * 0.9f, rainbow(Boss.hue + beat * 0.13f, 1.5f), 2.2f, 64, 1, 1);
    }
    if (Boss.phase == 3 && (om || pr)) {
      int k = pr ? 4 : 8;
      if (beat % k == k - 2) { Boss.warn = Boss.warnMax = spb(); sfx_charge(Boss.warn * 0.95f, Boss.x); }
      if (beat % k == k - 1) { float dx = p->x - Boss.x, dy = p->y - Boss.y, m = mhypot(dx, dy); if (m == 0) m = 1; Boss.cx = dx / m; Boss.cy = dy / m; Boss.charge = 0.5f; cam_shake(0.35f); sfx_dash(-1); }
    }
  }
}
/** Every step of the current meter (16ths in 4/4, triplet eighths in 12/8). */
static void boss_step(i64 s, int steps) {
  if (!attackLive() || Boss.kind != BK_CAROUSEL || steps != 3) return;
  int sub = (int)(s % 3), inBar = (int)(s % 12); Car *car = &Boss.segs[inBar]; Player *p = &Game.p;
  car->glow = 1;
  if (sub > 0 && Boss.phase == 1) { float sweep = msin(inBar / 12.f * TAU) * 1.1f, aim = matan2(p->y - Boss.y, p->x - Boss.x); enemyShot(Boss.x, Boss.y, aim + sweep, 170 + sub * 25.f, 6, COL_ebullet); }
  if (Boss.phase >= 2 && car->alive) {
    float a = matan2(car->y - Boss.y, car->x - Boss.x);
    enemyShot(car->x, car->y, a, 190, 6, COL_ebullet);
    if (Boss.phase == 3) { enemyShot(car->x, car->y, a + 0.4f, 190, 6, COL_ebullet); enemyShot(car->x, car->y, a - 0.4f, 190, 6, COL_ebullet); }
  }
  for (int i = 0; i < 2; i++) {
    Sprayer *sp = &Boss.spr[i]; if (!sp->on) continue;
    sp->fa += sp->dir * 0.38f;
    for (int k = 0; k < 2; k++) { float a = sp->fa + k * PI; enemyShot(sp->x + mcos(a) * 10, sp->y + msin(a) * 10, a, 165, 6, sp->dir > 0 ? c3(1.5f, 1.2f, 0.5f) : c3(0.6f, 1.2f, 1.6f)); }
  }
}
static void boss_eighth(i64 i) {
  if (!attackLive()) return;
  if (Boss.kind == BK_PRISM && Boss.phase == 2 && (i >> 2) % 2 == 0) { float a = i * 0.29f, h = i * 0.06f; enemyShot(Boss.x + mcos(a) * Boss.r, Boss.y + msin(a) * Boss.r, a, 230, 7, rainbow(h, 1.25f)); enemyShot(Boss.x - mcos(a) * Boss.r, Boss.y - msin(a) * Boss.r, a + PI, 230, 7, rainbow(h + 0.5f, 1.25f)); }
  if ((Boss.kind == BK_ECLIPSE || Boss.kind == BK_OMEGA) && Boss.phase == 3 && i % 2 == 0 && (i >> 3) % 2 == 0) for (int k = 0; k < 3; k++) { float a = i * 0.22f + k * TAU / 3; enemyShot(Boss.x + mcos(a) * Boss.r, Boss.y + msin(a) * Boss.r, a, 190, 7, COL_ebullet); }
}
/** Every tick (24 per beat) — the fine-grained spiral streams. */
static void boss_tick(i64 t) {
  if (!attackLive() || (Boss.kind != BK_OMEGA && Boss.kind != BK_PRIME)) return;
  int pr = Boss.kind == BK_PRIME; i64 bar = t / 96; float flip = bar % 2 ? -1.f : 1.f;
#define SHOOT(A_, SPD_, COL_, R_) enemyShot(Boss.x + mcos(A_) * Boss.r * 0.9f, Boss.y + msin(A_) * Boss.r * 0.9f, A_, SPD_, R_, COL_)
  if (t % 4 == 0 && (Boss.phase == 2 || (pr && Boss.phase == 3))) {
    if (pr) { float f = Boss.phase == 3 ? flip : 1; Boss.sa += 0.19f * f; Boss.sb -= 0.19f * f; for (int k = 0; k < 2; k++) { SHOOT(Boss.sa + k * PI, 150, rainbow(Boss.hue + k / 2.f, 1.25f), 5); SHOOT(Boss.sb + k * PI, 150, rainbow(Boss.hue + 0.5f + k / 2.f, 1.25f), 5); } }
    else { Boss.sa += 0.23f * flip; for (int k = 0; k < 2; k++) SHOOT(Boss.sa + k * PI, 175, c3(1.6f, 0.35f, 0.7f), 6); }
  }
  if (t % 6 == 0 && (pr ? Boss.phase == 1 : Boss.phase == 3)) {
    int arms = pr ? 4 : 2; float step = pr ? 0.12f : 0.17f;
    Boss.sa += step * (Boss.phase == 3 ? flip : 1);
    for (int k = 0; k < arms; k++) SHOOT(Boss.sa + k * TAU / arms, pr ? 165 : 185, pr ? rainbow(Boss.hue + (float)k / arms, 1.25f) : c3(1.6f, 0.35f, 0.7f), pr ? 5 : 6);
  }
#undef SHOOT
}
static void boss_bar(int bar) {
  if (!Boss.on || Boss.dead || Boss.spawn > 0) return;
  float ms = Boss.mods.spawn;
  if (Boss.kind == BK_PRISM && Boss.phase == 2 && Game.nen < 18) for (int i = 0; i < 3; i++) { float a = rnd(TAU); spawnEnemy(ET_DART, Boss.x + mcos(a) * 220, Boss.y + msin(a) * 220, -1); }
  if (Boss.kind == BK_CAROUSEL) {
    if (Boss.phase == 3) { radial(Boss.x, Boss.y, 12, bar * 0.2f, 240, 0, 0); Boss.dir *= -1; }
    int alive[12], n = 0; for (int i = 0; i < 12; i++) if (Boss.segs[i].alive) alive[n++] = i;
    if (Boss.phase >= 2 && bar % 2 == 0 && n && Game.nen < 30) { Car *c = &Boss.segs[alive[rndi(0, n - 1)]]; for (int i = 0; i < 5; i++) spawnEnemy(ET_GNAT, c->x + rndr(-30, 30), c->y + rndr(-30, 30), spb()); }
  }
  if (isEcl(Boss.kind)) {
    int pr = Boss.kind == BK_PRIME, nv = 0; for (int i = 0; i < Game.nen; i++) if (Game.en[i].type == ET_VORTEX) nv++;
    if (Boss.phase >= 2 && bar % 4 == 1 && nv < mlrint(2 * ms)) { float a = rnd(TAU); spawnEnemy(ET_VORTEX, Boss.x + mcos(a) * 450, Boss.y + msin(a) * 300, -1); }
    if (Boss.kind != BK_ECLIPSE && Boss.phase >= 2 && Boss.shiftT <= 0 && (pr || bar % 2 == 0)) aimLaser(bar * 4.0 + 3);
    if (ms > 1 && bar % 4 == 2 && Game.nen < 24 * ms) {
      int n = mlrint(2 * ms); static const int TY[3] = { ET_DART, ET_LUNGER, ET_FLICKER }; int type = TY[rndi(0, 2)];
      for (int i = 0; i < n; i++) { float a = rnd(TAU); spawnEnemy(type, Boss.x + mcos(a) * 320, Boss.y + msin(a) * 320, -1); }
    }
  }
}
static void moonHit(float x, float y, int k) { (void)k; hit_add(x, y, Boss.kind == BK_PRIME ? 17 : 14, NULL, 1, -1, HF_SOLID | HF_HARM); }
static void boss_hits(void) {
  if (!Boss.on || Boss.dead || Boss.spawn > 0) return;
  hit_add(Boss.x, Boss.y, Boss.r, NULL, 1, 0, HF_VULN | HF_SOLID | HF_HARM);
  if (Boss.kind == BK_PRISM) { for (int i = 0; i < 6; i++) if (Boss.nodes[i].alive) { float x, y; nodePos(i, &x, &y); hit_add(x, y, 20, NULL, 1, i + 1, HF_VULN | HF_SOLID | HF_HARM); } }
  else if (Boss.kind == BK_CAROUSEL) { for (int i = 0; i < 12; i++) if (Boss.segs[i].alive) hit_add(Boss.segs[i].x, Boss.segs[i].y, Boss.segs[i].r, NULL, 1, i + 1, HF_VULN | HF_SOLID | HF_HARM); }
  else moons(moonHit);
}
static void boss_drawDark(Batch *bd) {
  if (!Boss.on) return;
  if (isEcl(Boss.kind)) { float s = Boss.spawn > 0 ? 1 - Boss.spawn / Boss.spawnMax : 1; bdoth(bd, Boss.x, Boss.y, Boss.r * 1.15f * s, c3(0.9f, 0.9f, 0.9f), Boss.r * 0.8f * s); }
  else bdoth(bd, Boss.x, Boss.y, Boss.r * 0.9f, c3(0.35f, 0.35f, 0.35f), Boss.r * 0.3f);
}
static void drawLaserBeam(Batch *bt, Batch *fb, float ang, C3 c, float w, float warnF, int warn) {
  float x1 = Boss.x + mcos(ang) * 2600, y1 = Boss.y + msin(ang) * 2600;
  if (warn) { float fl = (int)mfloor(Boss.t * 24) % 2 ? 1 : 0.5f, f = warnF * fl; fb->z = 1; bline(fb, Boss.x, Boss.y, x1, y1, 0.8f + warnF * 1.5f, cmul(c, f)); fb->z = LIFT; return; }
  blineh(bt, Boss.x, Boss.y, x1, y1, w, c, w * 0.6f); bline(bt, Boss.x, Boss.y, x1, y1, w * 0.3f, c3(1.8f, 1.7f, 1.9f));
}
static Batch *moonB; static C3 moonC; static float moonSc;
static void moonDraw(float x, float y, int k) {
  (void)k; int pr = Boss.kind == BK_PRIME; C3 mc = pr ? rainbow(Boss.hue + x * 0.001f, 1.4f) : cmul(moonC, 0.85f);
  float mx = Boss.x + (x - Boss.x) * moonSc, my = Boss.y + (y - Boss.y) * moonSc;
  bcircle(moonB, mx, my, (pr ? 16 : 13) * moonSc, 16, 2, mc, 0); bdot(moonB, mx, my, 3, cmul(mc, 1.2f));
}
static void boss_draw(Batch *bt, Batch *fb) {
  if (!Boss.on) return;
  float k = Boss.dead ? ((int)mfloor(Boss.deathT * 20) % 2 ? 0.4f : 1.8f) : 1.5f, sk = Boss.spawn > 0 ? 1 - Boss.spawn / Boss.spawnMax : 1;
  if (Boss.spawn > 0) k *= sk * (0.6f + 0.4f * msin(Boss.t * 30));
  C3 c = cmul(BCOLS[Boss.kind], k), cf = Boss.flash > 0 ? c3(lerpf(c.r, 2.2f, 0.45f), lerpf(c.g, 2.2f, 0.45f), lerpf(c.b, 2.2f, 0.45f)) : c;
  float pulse = 1 + Game.beatPulse * 0.06f, sc = easeOutBack(clampf(sk, 0, 1));
  if (Boss.spawn > 0) { fb->z = 1; bcircle(fb, Boss.x, Boss.y, Boss.r * 2 + (1 - sk) * 400, 64, 2.5f, c, Boss.t); fb->z = LIFT; }
  Player *p = &Game.p;
  if (Boss.kind == BK_PRISM) {
    float R0 = Boss.r * 1.15f * pulse * sc, a0 = Boss.t * 0.6f, ox = 8 * sc, oy = -10 * sc;
    POLY(bt, SH_tri, Boss.x + ox, Boss.y + oy, a0, R0, 1.3f, cmul(c, 0.5f));
    POLY(bt, SH_tri, Boss.x, Boss.y, a0, R0, 2.6f, cf);
    for (int i = 0; i < 3; i++) { float a = a0 + i * TAU / 3, vx = Boss.x + mcos(a) * R0, vy = Boss.y + msin(a) * R0; bline(bt, vx, vy, vx + ox, vy + oy, 1.3f, cmul(c, 0.7f)); }
    bdot(bt, Boss.x, Boss.y, 6 + Game.beatPulse * 6, c3(1.8f, 1.8f, 1.9f));
    fb->z = 1; bcircle(fb, Boss.x, Boss.y, 100 * Boss.breathe * sc, 64, 0.8f, c3(0.35f, 0.3f, 0.4f), 0); bcircle(fb, Boss.x, Boss.y, 178 * Boss.breathe * sc, 64, 0.8f, c3(0.35f, 0.3f, 0.4f), 0); fb->z = LIFT;
    for (int i = 0; i < 6; i++) {
      PNode *n = &Boss.nodes[i]; if (!n->alive) continue;
      float qx, qy; nodePos(i, &qx, &qy); float f = n->flash > 0 ? 1.5f : 1, px = Boss.x + (qx - Boss.x) * sc, py = Boss.y + (qy - Boss.y) * sc; C3 nc = cmul(rainbow(n->hue, 1.3f), k / 1.5f * f);
      bline(bt, Boss.x, Boss.y, px, py, 0.7f, cmul(nc, 0.35f));
      POLY(bt, SH_shard, px, py, matan2(py - Boss.y, px - Boss.x), 20 * sc, 2, nc);
      bdot(bt, px, py, 2.4f, cmul(nc, 1.3f));
    }
    if (Boss.warn > 0) { float a = Boss.warn / (Boss.warnMax > 0 ? Boss.warnMax : spb()); fb->z = 1; bline(fb, Boss.x, Boss.y, p->x, p->y, 1 + (1 - a) * 3, c3(1.5f * (1 - a), 1.2f * (1 - a), 1.6f * (1 - a))); fb->z = LIFT; }
  } else if (Boss.kind == BK_CAROUSEL) {
    fb->z = 1; bcircle(fb, Boss.x, Boss.y, Boss.R * sc, 72, 1.1f, cmul(c, 0.3f), Boss.rot); fb->z = LIFT;
    for (int i = 0; i < 12; i++) {
      Car *s = &Boss.segs[i]; if (!s->alive) continue;
      float px = Boss.x + (s->x - Boss.x) * sc, py = Boss.y + (s->y - Boss.y) * sc, f = (s->flash > 0 ? 1.45f : 1) * (1 + s->glow * 0.8f), a = Boss.rot + s->a;
      bline(bt, Boss.x + mcos(a) * Boss.r, Boss.y + msin(a) * Boss.r, px - mcos(a) * s->r, py - msin(a) * s->r, 0.8f, cmul(c, 0.35f));
      bngon(bt, px, py, s->r * sc * (1 + s->glow * 0.15f), 4, a + PI / 4, 2, cmul(c, f));
      if (s->glow > 0.1f) bdot(bt, px, py, 4 * s->glow, c3(1.8f * s->glow, 1.6f * s->glow, 1.0f * s->glow));
    }
    bcircle(bt, Boss.x, Boss.y, Boss.r * pulse * sc, 32, 2.8f, cf, 0);
    for (int i = 0; i < 2; i++) {
      Sprayer *sp = &Boss.spr[i]; if (!sp->on) continue; C3 sc2 = sp->dir > 0 ? c3(1.6f, 1.3f, 0.6f) : c3(0.6f, 1.3f, 1.7f);
      bcircle(bt, sp->x, sp->y, 10, 14, 1.6f, sc2, 0);
      for (int j = 0; j < 3; j++) { float a = sp->fa + j * TAU / 3; bline(bt, sp->x, sp->y, sp->x + mcos(a) * 17, sp->y + msin(a) * 17, 1.6f, sc2); }
    }
    float vb = (float)Game.visBeat; int sub = (int)mfloor((vb - mfloor(vb)) * 3);
    for (int i = 0; i < 3; i++) { float a = -PI / 2 + i * TAU / 3 - Boss.rot * 0.5f; int on = i == sub; bdot(bt, Boss.x + mcos(a) * Boss.r * 0.5f, Boss.y + msin(a) * Boss.r * 0.5f, on ? 5.f : 2.5f, cmul(c, on ? 1.6f : 0.6f)); }
  } else {
    int pr = Boss.kind == BK_PRIME, om = Boss.kind != BK_ECLIPSE;
    if (om) {
      int arms = pr ? 6 : 3; float r0 = Boss.r * 1.1f, r1 = Boss.r * (pr ? 4.4f : 3.6f);
      fb->z = 1;
      for (int a = 0; a < arms; a++) {
        C3 ac = pr ? rainbow(Boss.hue + (float)a / arms, 1.1f * k / 1.5f) : cmul(c, 0.7f); float px = 0, py = 0;
        for (int j = 0; j <= 18; j++) {
          float f = j / 18.f, rr = lerpf(r0, r1, f) * sc, ang = -Boss.spiral + a * TAU / arms + f * 3.2f, x = Boss.x + mcos(ang) * rr, y = Boss.y + msin(ang) * rr, fa = (1 - f) * (0.7f + 0.5f * Game.beatPulse);
          if (j) bline(fb, px, py, x, y, 1.8f - f, cmul(ac, fa));
          px = x; py = y;
        }
      }
      fb->z = LIFT;
    }
    for (int o = 0; o < 3; o++) {
      Orbit *O = &Boss.orbits[o]; float cc = mcos(O->tilt + Boss.spin * 0.15f), ss = msin(O->tilt + Boss.spin * 0.15f); C3 oc = pr ? rainbow(Boss.hue + O->tilt / TAU, 0.45f) : cmul(c, 0.22f);
      float px = 0, py = 0;
      for (int j = 0; j <= 40; j++) { float ph = j / 40.f * TAU, ex = mcos(ph) * O->A * sc, ey = msin(ph) * O->Bm * sc, x = Boss.x + ex * cc - ey * ss, y = Boss.y + ex * ss + ey * cc; if (j) bline(bt, px, py, x, y, 0.8f, oc); px = x; py = y; }
    }
    moonB = bt; moonC = c; moonSc = sc; moons(moonDraw);
    C3 limb = pr ? rainbow(Boss.hue * 2, 1.6f) : cf;
    bcircle(bt, Boss.x, Boss.y, Boss.r * pulse * sc, 64, 2.8f, limb, 0);
    int nStr = pr ? 60 : 36;
    for (int i = 0; i < nStr; i++) {
      float a = Boss.spin * 0.4f + i * TAU / nStr, len = (10 + 12 * (0.5f + 0.5f * msin(Boss.t * 3 + i * 1.7f)) + Game.beatPulse * (pr ? 16 : 8)) * sc, r0 = (Boss.r + 6) * sc, f = i % 3 ? 0.4f : 0.75f;
      C3 s3 = pr ? rainbow((float)i / nStr + Boss.hue, f * 1.3f) : cmul(c, f);
      bline(bt, Boss.x + mcos(a) * r0, Boss.y + msin(a) * r0, Boss.x + mcos(a) * (r0 + len), Boss.y + msin(a) * (r0 + len), 1.2f, s3);
    }
    float fa = Boss.spin * 0.5f, fx = Boss.x + mcos(fa) * Boss.r * sc, fy = Boss.y + msin(fa) * Boss.r * sc, gl = 1.4f + Game.beatPulse;
    bdoth(bt, fx, fy, 5, c3(2 * gl, 1.9f * gl, 1.8f * gl), 6);
    for (int i = 0; i < 4; i++) { float a = i * PI / 2 + PI / 4; bline(bt, fx, fy, fx + mcos(a) * 22, fy + msin(a) * 22, 0.9f, c3(1.3f, 1.25f, 1.3f)); }
    if (Boss.phase == 3) { fb->z = 1; float w = 0.5f + 0.5f * Game.beatPulse; bcircle(fb, Boss.x, Boss.y, 180, 64, 1.2f, c3(1.0f * w, 0.3f * w, 1.2f * w), Boss.t * 0.3f); bcircle(fb, Boss.x, Boss.y, 800, 96, 0.8f, c3(0.3f, 0.2f, 0.6f), -Boss.t * 0.1f); fb->z = LIFT; }
    for (int i = 0; i < Boss.nlas; i++) {
      Laser *L = &Boss.las[i];
      for (int a = 0; a < L->nang; a++) {
        float ang = L->angles[a] + L->rot;
        if (L->state == 0) drawLaserBeam(bt, fb, ang, L->col, 0, L->wf, 1);
        else drawLaserBeam(bt, fb, ang, L->col, L->w * (0.5f + 0.5f * minf(1, L->ff * L->durB * 6)) * (0.7f + 0.3f * (1 - L->ff)), 0, 0);
      }
    }
    for (int i = 0; i < Boss.naim; i++) {
      Aimed *A = &Boss.aim[i]; float ca = mcos(A->ang), sa = msin(A->ang), nx = -sa * A->w, ny = ca * A->w, x0 = Boss.x + ca * Boss.r, y0 = Boss.y + sa * Boss.r, x1 = Boss.x + ca * 2600, y1 = Boss.y + sa * 2600; C3 c2 = A->col;
      if (A->state == 2) { float w = A->w * (0.6f + 0.4f * minf(1, A->t * 14)); blineh(bt, x0, y0, x1, y1, w, c2, w * 0.7f); bline(bt, x0, y0, x1, y1, w * 0.35f, c3(2, 1.9f, 2)); continue; }
      int lock = A->state == 1; float fl = lock ? ((int)mfloor(Boss.t * 30) % 2 ? 1.6f : 0.8f) : ((int)mfloor(Boss.t * 12) % 2 ? 0.7f : 0.35f);
      fb->z = 1;
      bline(fb, x0, y0, x1, y1, lock ? 2.2f : 1, cmul(c2, fl * 0.6f));
      bline(fb, x0 + nx, y0 + ny, x1 + nx, y1 + ny, 0.8f, cmul(c2, fl * 0.35f));
      bline(fb, x0 - nx, y0 - ny, x1 - nx, y1 - ny, 0.8f, cmul(c2, fl * 0.35f));
      fb->z = LIFT;
    }
    if (Boss.warn > 0) { float a = Boss.warn / (Boss.warnMax > 0 ? Boss.warnMax : spb()); fb->z = 1; bline(fb, Boss.x, Boss.y, p->x, p->y, 1 + (1 - a) * 4, c3(1.6f * (1 - a), 0.4f * (1 - a), 0.8f * (1 - a))); fb->z = LIFT; }
  }
}

/* =========================================================================
   DIRECTOR — procedural sectors, formations spawned in rhythm
   ========================================================================= */
static const float COST[ET_N] = { 1, 1.4f, 2, 2, 2.6f, 0, 0.45f, 4, 6, 6, 4.5f };
static const int UNLOCK[ET_N] = { 1, 1, 4, 3, 4, 99, 2, 6, 7, 6, 16 };
static const float WEIGHT[ET_N] = { 3, 4, 2, 2, 1.5f, 0, 2.4f, 1.1f, 0.7f, 0.6f, 1.3f };
static const int CAP[ET_N] = { 0, 0, 0, 0, 0, 0, 0, 5, 3, 3, 4 };
typedef struct { const char *name; int key; float mul[ET_N]; int cap[ET_N]; float budget; } Theme;
static const Theme THEMES[9] = {
  { "GNAT CLOUD", ET_GNAT, { [ET_GNAT] = 4 }, { 0 }, 0 },
  { "VORTEX FIELD", ET_VORTEX, { [ET_VORTEX] = 4, [ET_MOTE] = 2 }, { [ET_VORTEX] = 4 }, 0 },
  { "PURSUIT", ET_FLICKER, { [ET_DART] = 2, [ET_FLICKER] = 2.5f, [ET_LUNGER] = 2 }, { 0 }, 0 },
  { "LIGHTHOUSES", ET_BEACON, { [ET_BEACON] = 4 }, { [ET_BEACON] = 7 }, 0 },
  { "BOLA STORM", ET_BOLA, { [ET_BOLA] = 4 }, { [ET_BOLA] = 5 }, 0 },
  { "LUNGE", ET_LUNGER, { [ET_LUNGER] = 4 }, { 0 }, 0 },
  { "SHATTER", ET_CLUSTER, { [ET_CLUSTER] = 4 }, { 0 }, 0 },
  { "NOISE FLOOR", ET_MOTE, { 0 }, { 0 }, 1.3f },
  { "BOMBARDMENT", ET_MORTAR, { [ET_MORTAR] = 3, [ET_BEACON] = 1.5f }, { [ET_MORTAR] = 6 }, 0 }
};
enum { DS_IDLE, DS_INTRO, DS_SPAWN, DS_CLEARED };
typedef struct { i64 tick; int type; float x, y; } QItem;
static struct { int state, wave, bars, bar, introLeft, boss, bossSpawned, scored; float budget; const Theme *theme; QItem q[160]; int nq; } Director;
static int directorCleared(void) { return Director.state == DS_CLEARED ? 2 : (Director.state == DS_SPAWN && !Director.boss && Director.bar >= Director.bars && !Director.nq && Game.nen == 0); }
static void director_setup(int n) {
  Director.wave = n; Director.nq = 0; Director.bar = 0; Director.boss = n % 5 == 0; Director.bossSpawned = 0;
  Director.bars = Director.boss ? 0 : 12 + (n < 10 ? n : 10);
  Director.budget = minf(3.2f + n * 1.15f, 17);
  Director.theme = NULL;
  if (!Director.boss && n >= 3 && chance(0.6f)) {
    int ok[9], m = 0; for (int i = 0; i < 9; i++) if (UNLOCK[THEMES[i].key] <= n) ok[m++] = i;
    if (m) { Director.theme = &THEMES[ok[rndi(0, m - 1)]]; if (Director.theme->budget > 0) Director.budget *= Director.theme->budget; }
  }
  Director.state = DS_INTRO; Director.introLeft = 1; Director.scored = 0;
}
static float director_progress(void) { return Director.boss ? 1 : clampf((float)Director.bar / (Director.bars > 1 ? Director.bars : 1), 0, 1); }
static void director_score(void) { Score s; mzero(&s, sizeof s); s.has = SC_CRASH | SC_LEVEL | SC_STYLE; s.level = 2; s.style = Director.boss ? ST_BOSS : ST_GAME; setScore(&s); }
static void director_onBeat(i64 beat) { if (Director.state == DS_INTRO && Director.introLeft == 1 && beat % 4 == 3) { director_score(); Director.scored = 1; } }
static void qpush(i64 tick, int type, float x, float y) { if (Director.nq < ARR(Director.q)) { QItem *q = &Director.q[Director.nq++]; q->tick = tick; q->type = type; q->x = x; q->y = y; } }
static void far_(float *x, float *y, float d) { Player *p = &Game.p; float dx = *x - p->x, dy = *y - p->y, m = mhypot(dx, dy); if (m == 0) m = 1; if (m < d) { *x = p->x + dx / m * d; *y = p->y + dy / m * d; } *x = clampf(*x, 40, ARENA_W - 40); *y = clampf(*y, 40, ARENA_H - 40); }
static void randFar(float d, float *x, float *y) { Player *p = &Game.p; for (int i = 0; i < 20; i++) { float a = rndr(60, ARENA_W - 60), b = rndr(60, ARENA_H - 60); if (dist2(a, b, p->x, p->y) > d * d) { *x = a; *y = b; return; } } *x = rndr(60, ARENA_W - 60); *y = rndr(60, ARENA_H - 60); far_(x, y, d); }
static void corners(float *c) {
  float C[4][2] = { { 70, 70 }, { ARENA_W - 70, 70 }, { 70, ARENA_H - 70 }, { ARENA_W - 70, ARENA_H - 70 } }; Player *p = &Game.p;
  for (int i = 1; i < 4; i++) for (int j = i; j > 0 && dist2(C[j][0], C[j][1], p->x, p->y) > dist2(C[j - 1][0], C[j - 1][1], p->x, p->y); j--) { float t0 = C[j][0], t1 = C[j][1]; C[j][0] = C[j - 1][0]; C[j][1] = C[j - 1][1]; C[j - 1][0] = t0; C[j - 1][1] = t1; }
  memcpy(c, C, sizeof C);
}
static int formation(int type, float budget, i64 at) {
  Player *p = &Game.p; float C = COST[type]; int maxN = (int)mfloor(budget / C); if (maxN < 1) maxN = 1;
  float pts[16][2]; int n = 0, stagger = 6;
#define EDGE(k) do { int side = rndi(0, 3); for (int i = 0; i < (k); i++) { float t = (i + 1.f) / ((k) + 1); float xx = side == 0 ? t * ARENA_W : side == 1 ? t * ARENA_W : side == 2 ? 50 : ARENA_W - 50, yy = side == 0 ? 50 : side == 1 ? ARENA_H - 50 : t * ARENA_H; far_(&xx, &yy, 280); pts[n][0] = xx; pts[n][1] = yy; n++; } } while (0)
#define RING(k, R) do { float a0 = rnd(TAU); for (int i = 0; i < (k); i++) { float a = a0 + i * TAU / (k), xx = p->x + mcos(a) * (R), yy = p->y + msin(a) * (R); far_(&xx, &yy, 260); pts[n][0] = xx; pts[n][1] = yy; n++; } } while (0)
  switch (type) {
    case ET_MOTE: { int k = rndi(3, 6); if (k > maxN) k = maxN; if (chance(0.4f) && k >= 4) { float c[8]; corners(c); for (int i = 0; i < 4; i++) { pts[n][0] = c[i * 2]; pts[n][1] = c[i * 2 + 1]; n++; } } else for (int i = 0; i < k; i++) { randFar(320, &pts[n][0], &pts[n][1]); n++; } break; }
    case ET_DART: { int k = rndi(4, 8); if (k > maxN) k = maxN; if (chance(0.55f)) RING(k, rndr(380, 470)); else EDGE(k); break; }
    case ET_GNAT: { int k = rndi(8, 13); if (k > maxN) k = maxN; float cx, cy; if (chance(0.5f)) { float c[8]; corners(c); int j = rndi(0, 1); cx = c[j * 2]; cy = c[j * 2 + 1]; } else randFar(500, &cx, &cy);
      for (int i = 0; i < k; i++) { float xx = cx + rndr(-50, 50), yy = cy + rndr(-50, 50); far_(&xx, &yy, 280); pts[n][0] = xx; pts[n][1] = yy; n++; } stagger = 2; break; }
    case ET_LUNGER: { int k = rndi(3, 4); if (k > maxN) k = maxN; if (chance(0.5f)) EDGE(k); else RING(k, 420); break; }
    case ET_FLICKER: { int k = rndi(2, 4); if (k > maxN) k = maxN; for (int i = 0; i < k; i++) { randFar(380, &pts[n][0], &pts[n][1]); n++; } break; }
    case ET_CLUSTER: { int k = rndi(2, 3); if (k > maxN) k = maxN; float c[8]; corners(c); for (int i = 0; i < k; i++) { pts[n][0] = c[i * 2]; pts[n][1] = c[i * 2 + 1]; n++; } break; }
    default: randFar(type == ET_VORTEX ? 360 : 450, &pts[n][0], &pts[n][1]); n++;
  }
#undef EDGE
#undef RING
  for (int i = 0; i < n; i++) qpush(at + i * stagger, type, pts[i][0], pts[i][1]);
  return n;
}
static void planBar(i64 tick) {
  int n = Director.wave, cap = 55 + n * 7 < 150 ? 55 + n * 7 : 150;
  float live = 0; for (int i = 0; i < Game.nen; i++) live += Game.en[i].type == ET_GNAT || Game.en[i].type == ET_SHARD ? 0.4f : 1;
  if (live > cap) return;
  float budget = Director.budget * (0.55f + 0.45f * minf(1, Director.bar / 4.f));
  int counts[ET_N]; mzero(counts, sizeof counts);
  for (int i = 0; i < Game.nen; i++) counts[Game.en[i].type]++;
  for (int i = 0; i < Director.nq; i++) counts[Director.q[i].type]++;
  const Theme *th = Director.theme;
  int guard = 0; i64 at = tick + (chance(0.5f) ? 0 : 48);
  while (budget > 0.4f && guard++ < 8) {
    int pool[ET_N]; float pw[ET_N], tot = 0; int np = 0;
    for (int t = 0; t < ET_N; t++) {
      if (WEIGHT[t] <= 0 || UNLOCK[t] > n) continue;
      int c = th && th->cap[t] ? th->cap[t] : CAP[t];
      if ((c && counts[t] >= c) || COST[t] > budget + 0.5f) continue;
      pool[np] = t; pw[np] = WEIGHT[t] * (th && th->mul[t] > 0 ? th->mul[t] : 1); tot += pw[np]; np++;
    }
    if (!np) break;
    float r = rnd(tot); int type = pool[0];
    for (int i = 0; i < np; i++) { r -= pw[i]; if (r <= 0) { type = pool[i]; break; } }
    int cnt = formation(type, budget, at);
    counts[type] += cnt; budget -= cnt * COST[type]; at += 24;
  }
  for (int i = 1; i < Director.nq; i++) for (int j = i; j > 0 && Director.q[j].tick < Director.q[j - 1].tick; j--) { QItem t = Director.q[j]; Director.q[j] = Director.q[j - 1]; Director.q[j - 1] = t; }
}
static void director_onBar(int bar, i64 tick) {
  (void)bar;
  if (Director.state == DS_INTRO) {
    if (--Director.introLeft > 0) return;
    if (!Director.scored) director_score();
    Director.state = DS_SPAWN; Director.bar = 0;
    if (Director.boss) {
      int kind = boss_kindFor(Director.wave);
      boss_spawn(kind, 1 + 0.55f * mfloor((Director.wave - 5) / 15.f) + 0.12f * mfloor((Director.wave - 5) / 5.f));
      Director.bossSpawned = 1;
      return;
    }
  }
  if (Director.state != DS_SPAWN || Director.boss || Director.bar >= Director.bars) return;
  planBar(tick);
  Director.bar++;
  int lvl = (Game.nen > 40 || (Director.bar > Director.bars * 0.5f && Director.wave >= 3) || Game.mult >= 150) ? 3 : 2;
  score_level(lvl);
}
static void director_onTick(i64 tick) {
  while (Director.nq && Director.q[0].tick <= tick) {
    QItem q = Director.q[0]; memmove(Director.q, Director.q + 1, sizeof(QItem) * (Director.nq - 1)); Director.nq--;
    spawnEnemy(q.type, q.x, q.y, spb() * (q.type == ET_GNAT ? 1 : 1.5f));
  }
}

/* =========================================================================
   AUGMENTS — some also join the band (♪ = adds a layer to the score)
   ========================================================================= */
static const struct { const char *name; C3 col; float w; } RARITY[3] = { { "COMMON", { 0.3f, 0.95f, 1.0f }, 10 }, { "RARE", { 1.0f, 0.31f, 0.85f }, 6 }, { "EPIC", { 1.0f, 0.83f, 0.36f }, 2.6f } };
enum { U_RAPID, U_SPREAD, U_DMG, U_PIERCE, U_BOUNCE, U_HOMING, U_REAR, U_ORBIT, U_CHAIN, U_BOOM, U_SHIELD, U_HULL, U_MAGNET, U_DASH, U_PERFECT, U_BOMB, U_MISSILE, U_NOVA, U_SPEED, U_ACCENT, U_N, U_REPAIR = 20 };
typedef struct { const char *name; int rar, max; const char *icon; } UpgDef;
static const UpgDef UPG[21] = {
  { "DOUBLE TIME", 1, 2, "M4 6l5 6-5 6M10 6l5 6-5 6M16 6l5 6-5 6" },
  { "FANFARE", 1, 4, "M12 21V4M12 21L5 6M12 21l7-15M12 21L2 12M12 21l10-9" },
  { "GAIN", 0, 5, "M12 2l2.6 6.6L21 9.5l-5 4.4 1.6 6.9L12 17.2 6.4 20.8 8 13.9 3 9.5l6.4-.9z" },
  { "LANCE", 0, 3, "M2 12h20M17 7l5 5-5 5M12 8a4 4 0 1 0 0 8a4 4 0 1 0 0-8" },
  { "REBOUND", 0, 3, "M3 21L12 5l9 16M2 3h20" },
  { "TRACKING", 1, 2, "M4 21C4 11 13 6 20 6M15 2l5 4-5 4" },
  { "ECHO", 0, 2, "M2 12h20M7 7l-5 5 5 5M17 7l5 5-5 5" },
  { "ORBITALS", 2, 3, "M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6M12 2a10 10 0 1 1 0 20a10 10 0 1 1 0-20M19 3l2 2-2 2-2-2z" },
  { "STATIC", 1, 3, "M13 2L4 14h7l-2 8 9-12h-7z" },
  { "FLASHPOINT", 1, 3, "M12 1v5M12 18v5M1 12h5M18 12h5M4 4l3.5 3.5M16.5 16.5L20 20M4 20l3.5-3.5M16.5 7.5L20 4M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6" },
  { "AEGIS", 1, 2, "M12 2l8.66 5v10L12 22l-8.66-5V7z" },
  { "PLATING", 0, 3, "M12 2l8.66 5v10L12 22l-8.66-5V7zM12 8v8M8 12h8" },
  { "PULL", 0, 2, "M6 3v9a6 6 0 0 0 12 0V3M6 7h4M14 7h4" },
  { "AFTERBURN", 1, 2, "M2 8h10M4 12h14M2 16h10M18 8l4 4-4 4" },
  { "IN THE POCKET", 0, 2, "M8 21L11 3h2l3 18zM12 15l6-10" },
  { "STOCKPILE", 0, 2, "M11 21a7 7 0 1 1 0-14a7 7 0 1 1 0 14M14 8l4-4M18 1v4M16 3h4" },
  { "BASS DROP", 2, 3, "M12 2c3 3 4 7 4 11l-4 4-4-4c0-4 1-8 4-11zM8 14l-3 4h3M16 14l3 4h-3M12 17v5" },
  { "SWELL", 2, 2, "M12 10a2 2 0 1 0 0 4a2 2 0 1 0 0-4M12 5a7 7 0 1 1 0 14a7 7 0 1 1 0-14M12 1a11 11 0 1 1 0 22" },
  { "NITRO", 0, 2, "M2 18L12 6l10 12M7 18l5-6 5 6" },
  { "STRIKE", 1, 2, "M9 18a3 3 0 1 1-6 0a3 3 0 1 1 6 0M9 18V4l10-2v12M19 14a3 3 0 1 1-6 0a3 3 0 1 1 6 0" },
  { "PATCH UP", 0, 99, "M12 2l8.66 5v10L12 22l-8.66-5V7zM12 8v8M8 12h8" }
};
static void upg_desc(int u, int l, char *o) {
  switch (u) {
    case U_RAPID: s_cpy(o, l == 1 ? "Fire on sextuplets instead of sixteenths. +50% fire rate." : "Fire on thirty-second notes. +33% fire rate."); break;
    case U_SPREAD: s_cpy(o, "+1 projectile, fanned into a spread."); break;
    case U_DMG: s_cpy(o, "+35% damage for everything you fire."); break;
    case U_PIERCE: s_cpy(o, "Shots pierce through +1 enemy."); break;
    case U_BOUNCE: s_cpy(o, "Shots bounce off the arena walls +1 time."); break;
    case U_HOMING: s_cpy(o, l == 1 ? "Shots curve toward nearby enemies." : "Shots steer much harder."); break;
    case U_REAR: s_cpy(o, l == 1 ? "Also fire a shot behind you." : "Rear fire becomes a 3-way spread."); break;
    case U_ORBIT: s_cpy(o, "+1 drone orbiting once per bar. Rams enemies and fires on every beat."); break;
    case U_CHAIN: s_printf(o, "Kills arc lightning to %d nearby enemies.", l + 1); break;
    case U_BOOM: s_cpy(o, "Kills explode, damaging nearby enemies. Chain reactions!"); break;
    case U_SHIELD: s_cpy(o, l == 1 ? "A shield absorbs one hit. Recharges every 8 bars." : "Shield recharges every 4 bars."); break;
    case U_HULL: s_cpy(o, "+1 hull bar and a full repair."); break;
    case U_MAGNET: s_cpy(o, "Spark pickup radius +60%."); break;
    case U_DASH: s_cpy(o, "Dashes cost 20% less energy. Dashing through enemies shreds them."); break;
    case U_PERFECT: s_cpy(o, "Perfect, Great and Good timing windows +40%. Dash shockwaves +50%."); break;
    case U_BOMB: s_cpy(o, "+1 max bombs and +1 bomb now."); break;
    case U_MISSILE: s_cpy(o, "Launch 2 homing missiles on every bar."); break;
    case U_NOVA: s_cpy(o, l == 1 ? "Release a damaging nova every 4 bars." : "Nova every 2 bars."); break;
    case U_SPEED: s_cpy(o, "+12% movement speed."); break;
    case U_ACCENT: s_cpy(o, l == 1 ? "Shots fired on the beat are huge and deal +150% damage." : "On-beat shots deal +300% damage."); break;
    default: s_cpy(o, "Repair 1 hull bar. If already whole, gain 25,000 x combo."); break;
  }
}
static const char *upg_music(int u, int l) {
  switch (u) {
    case U_RAPID: return l == 1 ? "Trap triplet hi-hats" : "Drum & bass 32nd breaks";
    case U_SPREAD: return "Richer ninth chords";
    case U_ORBIT: return "An arpeggio that circles the stereo field with your drones";
    case U_CHAIN: return "Metallic FM ticks on the offbeat sixteenths";
    case U_MISSILE: return "A brass stab and sub drop on the one";
    case U_NOVA: return "A riser that swells into every nova";
    case U_ACCENT: return "Vocal chops on the accents";
  }
  return NULL;
}
static void game_syncLayers(void) {
  int *u = Game.upg; S.L.arc = u[U_CHAIN]; S.L.orbit = u[U_ORBIT]; S.L.missile = u[U_MISSILE]; S.L.tempo = u[U_RAPID]; S.L.accent = u[U_ACCENT]; S.L.spread = u[U_SPREAD]; S.L.nova = u[U_NOVA];
}
static void applyUpgrade(int u) {
  int l = (u == U_REPAIR ? 0 : Game.upg[u]) + 1; Stats *s = &Game.st; Player *p = &Game.p;
  if (u != U_REPAIR) Game.upg[u] = l;
  switch (u) {
    case U_RAPID: { static const int F[3] = { 4, 6, 8 }; s->fireSub = F[l]; break; }
    case U_SPREAD: s->spread = l; break;
    case U_DMG: s->dmg = s->dmgBase * (1 + 0.35f * l); break;
    case U_PIERCE: s->pierce = l; break;
    case U_BOUNCE: s->bounce = l; break;
    case U_HOMING: s->homing = l; break;
    case U_REAR: s->rear = l; break;
    case U_ORBIT: s->orbit = l; break;
    case U_CHAIN: s->chain = l; break;
    case U_BOOM: s->boom = l; break;
    case U_SHIELD: s->shield = l; p->shield = 1; break;
    case U_HULL: s->maxHp = s->hpBase + l; p->hp = s->maxHp; break;
    case U_MAGNET: s->magnet = 1.f + l; break;
    case U_DASH: s->dashDmg = l; break;
    case U_PERFECT: s->perfect = l; break;
    case U_BOMB: s->bombsMax = s->bombBase + l; p->bombs = p->bombs + 1 > s->bombsMax ? s->bombsMax : p->bombs + 1; break;
    case U_MISSILE: s->missile = l; break;
    case U_NOVA: s->nova = l; break;
    case U_SPEED: s->speed = s->speedBase * (1 + 0.12f * l); break;
    case U_ACCENT: s->accent = l; break;
    default: if (p->hp < s->maxHp) p->hp++; else addScore(25000LL * Game.mult); break;
  }
  game_syncLayers();
}
static int randomOpenUpgrade(void) { int pool[U_N], n = 0; for (int i = 0; i < U_N; i++) if (Game.upg[i] < UPG[i].max) pool[n++] = i; return n ? pool[rndi(0, n - 1)] : U_REPAIR; }
static void rollUpgrades(int afterBoss, int *out) {
  int pool[U_N], n = 0, k = 0; for (int i = 0; i < U_N; i++) if (Game.upg[i] < UPG[i].max) pool[n++] = i;
  static const float AB[3] = { 0.6f, 1.5f, 3.5f };
  while (k < 3 && n) {
    float ws[U_N], tot = 0; for (int i = 0; i < n; i++) { ws[i] = RARITY[UPG[pool[i]].rar].w * (afterBoss ? AB[UPG[pool[i]].rar] : 1); tot += ws[i]; }
    float r = rnd(tot); int i = 0; for (; i < n - 1; i++) { r -= ws[i]; if (r <= 0) break; }
    out[k++] = pool[i]; memmove(&pool[i], &pool[i + 1], sizeof(int) * (n - i - 1)); n--;
  }
  while (k < 3) out[k++] = U_REPAIR;
}

/* =========================================================================
   BONUS — permanent upgrades bought with banked score between runs.
   LEVEL SELECT — start a run at a guardian, unlocked permanently with XP.
   ========================================================================= */
typedef struct { const char *name; int max; i64 base; const char *icon; } MetaDef;
static const MetaDef META[6] = {
  { "EXTRA HULL", 3, 2000000, "M2 8h17v8H2zM19 10.5h3v3h-3M5.5 10.5v3M9 10.5v3M12.5 10.5v3" },
  { "SPARE SHELLS", 3, 2500000, "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6" },
  { "HOT ROUNDS", 5, 500000, "M12 2c3 4 5.5 7 5.5 11a5.5 5.5 0 0 1-11 0c0-2.2 1-4.2 2.4-5.4 0 2 1 3.2 2.3 3.4C11 8 11 5 12 2z" },
  { "TUNED THRUSTERS", 5, 500000, "M2 6l6 6-6 6M9 6l6 6-6 6M16 6l6 6-6 6" },
  { "COMBO GUARD", 3, 2000000, "M12 2l8 3.5v6.2c0 5-3.4 8.6-8 10.3-4.6-1.7-8-5.3-8-10.3V5.5zM8 12.5l3 3 5-6" },
  { "SECOND OPINION", 5, 750000, "M20 12a8 8 0 1 1-2.4-5.7M20 3.5v5h-5" }
};
static const char *REFUND_ICON = "M9 14L4 9l5-5M4 9h11a5 5 0 0 1 0 10h-3";
static void meta_desc(int i, int l, char *o) {
  switch (i) {
    case 0: s_printf(o, "Start every run with %d extra hull bar%s.", l, l > 1 ? "s" : ""); break;
    case 1: s_printf(o, "Start with %d extra bomb%s, and room to carry %s.", l, l > 1 ? "s" : "", l > 1 ? "them" : "it"); break;
    case 2: s_printf(o, "+%d%% damage on everything you fire.", l * 6); break;
    case 3: s_printf(o, "+%d%% movement speed.", l * 4); break;
    case 4: s_printf(o, "Keep %d%% of your combo when hit, instead of 50%%.", 50 + l * 10); break;
    default: s_printf(o, "%d augment rerolls per sector, instead of 2.", 2 + l); break;
  }
}
static i64 metaCost(int i, int l) { static const int M3[3] = { 1, 5, 25 }, M5[5] = { 1, 2, 4, 8, 16 }; return META[i].base * (META[i].max == 3 ? M3[l] : M5[l]); }
static i64 meta_bank(void) { return Save.bank > 0 ? Save.bank : 0; }
static void meta_deposit(i64 n) { Save.bank = meta_bank() + n; save_write(); }
static i64 meta_spent(void) { i64 t = 0; for (int d = 0; d < 6; d++) for (int i = 0; i < Save.meta[d]; i++) t += metaCost(d, i); return t; }
static int meta_buy(int id) { int l = Save.meta[id]; if (l >= META[id].max || meta_bank() < metaCost(id, l)) return 0; Save.bank = meta_bank() - metaCost(id, l); Save.meta[id] = (u8)(l + 1); save_write(); return 1; }
static i64 meta_refund(void) { i64 n = meta_spent(); if (!n) return 0; Save.bank = meta_bank() + n; mzero(Save.meta, sizeof Save.meta); save_write(); return n; }
static const struct { int wave; i64 cost; } LEVELS[5] = { { 1, 0 }, { 5, 5000000 }, { 10, 15000000 }, { 15, 30000000 }, { 20, 60000000 } };
#define RUSH_COST 40000000
static int level_bit(int w) { return w == 0 ? 31 : w / 5; }   /* 0 = boss rush */
static int level_unlocked(int w) { return w == 1 || (Save.levels >> level_bit(w) & 1); }
static int level_buy(int w) {
  i64 cost = 0; if (w == 0) cost = RUSH_COST; else for (int i = 0; i < 5; i++) if (LEVELS[i].wave == w) cost = LEVELS[i].cost;
  if (level_unlocked(w) || meta_bank() < cost) return 0;
  Save.bank = meta_bank() - cost; Save.levels |= 1u << level_bit(w); save_write(); return 1;
}
/* =========================================================================
   PALETTES
   ========================================================================= */
#define P3(a, b, c) { a, b, c }
static const Pal PALETTES[5] = {
  { "SUNSET BLVD", P3(0.95f, 0.15f, 0.75f), P3(0.1f, 0.7f, 1.0f), P3(1.0f, 0.85f, 1.0f), P3(1.4f, 0.35f, 1.2f), P3(0.02f, 0.0f, 0.07f), P3(0.28f, 0.03f, 0.26f), P3(0.8f, 0.18f, 0.45f), P3(1.0f, 0.6f, 0.16f), P3(0.78f, 0.06f, 0.3f), P3(1.0f, 0.2f, 0.8f), P3(0.03f, 0.0f, 0.06f), P3(0.25f, 0.05f, 0.35f) },
  { "LAVENDER MALL", P3(0.55f, 0.35f, 1.0f), P3(1.0f, 0.45f, 0.75f), P3(0.8f, 1.0f, 1.0f), P3(0.9f, 0.6f, 1.4f), P3(0.02f, 0.01f, 0.07f), P3(0.2f, 0.1f, 0.32f), P3(0.6f, 0.3f, 0.8f), P3(1.0f, 0.75f, 0.9f), P3(0.6f, 0.3f, 1.0f), P3(0.7f, 0.4f, 1.0f), P3(0.03f, 0.01f, 0.07f), P3(0.15f, 0.1f, 0.35f) },
  { "TEAL LAGOON", P3(0.1f, 0.8f, 0.85f), P3(0.6f, 0.3f, 1.0f), P3(1, 1, 0.8f), P3(0.3f, 1.3f, 1.2f), P3(0.0f, 0.02f, 0.06f), P3(0.05f, 0.18f, 0.26f), P3(0.15f, 0.7f, 0.7f), P3(1, 0.95f, 0.6f), P3(0.2f, 0.8f, 0.9f), P3(0.2f, 0.9f, 1.0f), P3(0.0f, 0.03f, 0.05f), P3(0.05f, 0.2f, 0.3f) },
  { "PEACH ARCADE", P3(1.0f, 0.45f, 0.3f), P3(1.0f, 0.2f, 0.6f), P3(1, 1, 0.7f), P3(1.4f, 0.6f, 0.4f), P3(0.04f, 0.0f, 0.06f), P3(0.36f, 0.12f, 0.16f), P3(0.8f, 0.38f, 0.28f), P3(1.0f, 0.95f, 0.45f), P3(1.0f, 0.35f, 0.3f), P3(1.0f, 0.4f, 0.4f), P3(0.05f, 0.01f, 0.04f), P3(0.3f, 0.08f, 0.2f) },
  { "ACID MINT", P3(0.3f, 1.0f, 0.6f), P3(0.2f, 0.5f, 1.0f), P3(1, 1, 1), P3(0.5f, 1.4f, 0.8f), P3(0.0f, 0.02f, 0.05f), P3(0.05f, 0.2f, 0.18f), P3(0.3f, 0.75f, 0.55f), P3(0.9f, 1.0f, 0.6f), P3(0.2f, 0.9f, 0.6f), P3(0.3f, 1.0f, 0.7f), P3(0.0f, 0.04f, 0.04f), P3(0.05f, 0.25f, 0.2f) }
};
static const Pal BOSS_PAL = { "GUARDIAN", P3(1.0f, 0.1f, 0.35f), P3(0.45f, 0.1f, 1.0f), P3(1.0f, 0.5f, 0.75f), P3(1.4f, 0.25f, 0.5f), P3(0.03f, 0.0f, 0.03f), P3(0.26f, 0.0f, 0.08f), P3(0.8f, 0.08f, 0.24f), P3(1.0f, 0.5f, 0.3f), P3(0.8f, 0.0f, 0.3f), P3(1.0f, 0.15f, 0.4f), P3(0.05f, 0.0f, 0.02f), P3(0.3f, 0.0f, 0.1f) };
static const Pal *palTarget = &PALETTES[0];
static Pal palCur;
static const C3 CHROME[2] = { { 0.5f, 1.0f, 1.35f }, { 1.35f, 0.22f, 0.85f } };

/* =========================================================================
   GAME FLOW
   ========================================================================= */
static void ui_showUpgrades(const int *list, int wave, int afterBoss, int reroll);
static void ui_showGameOver(const Record *rec, int rank, i64 banked, int won);
static void ui_show(int id);
static int ui_current(void);
enum { SC_NONE, SC_BOOT, SC_TITLESCR, SC_LEVELS, SC_BONUS, SC_HOW, SC_SETTINGS, SC_RECORDS, SC_UPGRADE, SC_PAUSE, SC_OVER };
static void startWave(int n);
static void game_startDemo(void);
static void game_newRun(int start, int rush) {
  if (rush) start = 5;
  Game.bossRush = rush; Game.won = 0; Game.bt = 0; Game.btOn = 0; Game.hasWarn = 0;
  Game.state = GS_PLAY; Game.paused = 0; Game.demo = 0; Game.god = 0; snd_setSfxScale(1);
  mzero(&Game.p, sizeof Game.p);
  Player *p = &Game.p; p->x = ARENA_W / 2; p->y = ARENA_H / 2; p->r = 13; p->hitR = 3.5f; p->energy = E_MAX; p->ang = p->aim = -PI / 2; p->inv = 2; p->alive = 1; p->ddx = 1;
  Game.st = baseStats(Save.meta); mzero(Game.upg, sizeof Game.upg); Game.banked = 0;
  p->hp = Game.st.maxHp; p->bombs = 2 + Save.meta[1];
  Game.nen = Game.nbu = Game.neb = Game.nsh = Game.npk = Game.nmi = Game.nbo = Game.nbm = 0; Game.bombWave.on = 0;
  Boss.on = 0; Game.nwells = 0; Game.hasBanner = 0;
  Game.score = 0; Game.shownScore = 0; Game.mult = 1; Game.wave = 0; Game.kills = 0; Game.maxMult = 1; Game.perfects = 0; Game.chain = 0; Game.chainT = 0; Game.playTime = 0;
  Game.time = 0; Game.timeScale = 1; Game.slowT = 0; Game.hitstopT = 0; Game.flash = 0; Game.aberr = 0; Game.hasLastTick = 0; Game.overT = 0;
  Game.hiScore = Save.nrec ? Save.rec[0].score : 0;
  fx_clear(); parts_clear(); grid_reset();
  Cam.zoom = 1; S.warpOn = 0;
  snd_setMuffle(0, 0.25f);
  Game.bossBeaten = 0; Game.afterBoss = 0;
  Game.startedAt = start;
  for (int i = 1; i < start; i++) applyUpgrade(randomOpenUpgrade());
  p->hp = Game.st.maxHp; { int b = 2 + Save.meta[1]; if (b > Game.st.bombsMax) b = Game.st.bombsMax; if (b > p->bombs) p->bombs = b; }
  game_syncLayers();
  startWave(start);
  Game.tutorial = !Save.played;
}
static void startWave(int n) {
  Game.wave = n;
  Game.diff.speed = 1 + minf(0.45f, (n - 1) * 0.028f); Game.diff.hp = 1 + maxf(0, n - 5.f) * 0.09f;
  director_setup(n);
  int boss = Director.boss;
  palTarget = boss ? &BOSS_PAL : &PALETTES[(n - 1) % 5];
  snd_newKey(boss ? K_PRISM + boss_kindFor(n) : K_GAME);
  { Score s; mzero(&s, sizeof s); s.has = SC_STYLE | SC_LEVEL; s.style = ST_GAME; s.level = 1; setScore(&s); }
  sfx_waveStart();
  if (boss) {
    int kind = boss_kindFor(n); Warning *w = &Game.warn; mzero(w, sizeof *w);
    w->t = 0; w->out = -1; w->kind = kind; s_cpy(w->name, BNAMES[kind]); s_cpy(w->sig, BSIG[kind]); w->idx = mlrint(n / 5.f); w->col = BCOLS[kind]; w->rush = Game.bossRush; Game.hasWarn = 1;
    snd_setMuffle(0.28f, 0.2f); Game.hasBanner = 0;
  } else {
    char t[24]; s_printf(t, "SECTOR %2", n);
    setBanner(t, Director.theme ? Director.theme->name : n == 1 ? "SYNC TO THE BEAT" : palTarget->name, 2.6f, c3(1.2f, 1.4f, 1.6f));
  }
}
static void waveCleared(void) {
  i64 bonus = 2500LL * Game.wave * (i64)maxf(1, mfloor(msqrt((float)Game.mult)));
  addScore(bonus);
  char sub[32]; sub[0] = '+'; s_fmt(sub + 1, bonus); setBanner("SECTOR CLEAR", sub, 2.2f, c3(0.6f, 1.6f, 1.1f));
  sfx_fanfare();
  fx_wave(Game.p.x, Game.p.y, 900, 1.2f, 1.2f);
  grid_explode(200, Game.p.x, Game.p.y, 700, -100);
  if (Game.wave % 4 == 0) Game.p.bombs = Game.p.bombs + 1 > Game.st.bombsMax ? Game.st.bombsMax : Game.p.bombs + 1;
  for (int i = 0; i < Game.npk; i++) if (Game.pk[i].type == PK_GEOM) Game.pk[i].t = minf(Game.pk[i].t, Game.pk[i].life - 1.2f);
  Director.state = DS_CLEARED; Game.clearT = 0;
  for (int i = 0; i < Game.neb; i++) burst(Game.eb[i].x, Game.eb[i].y, 4, Game.eb[i].col, 40, 160, 0.2f, 0.4f, NULL);
  Game.neb = 0; Game.nsh = 0;
  if (!Game.demo) { Save.played = 1; save_write(); } Game.tutorial = 0;
}
static void game_onBossDefeated(void) {
  Game.bossBeaten++;
  for (int i = 0; i < Game.nen; i++) { Enemy *e = &Game.en[i]; if (!e->dead) { if (e->spawn > 0) e->dead = 1; else killEnemy(e, CA_SHOCK); } }
  Game.afterBoss = 1;
  int kind = boss_kindFor(Game.wave);
  waveCleared();
  if (Game.wave >= BOSS_LAST) Game.won = 1;
  if (Game.won) { char s[40]; s_printf(s, "ALL %d SECTORS CLEARED", BOSS_LAST); setBanner("PHASE COMPLETE", s, 5, c3(1.7f, 1.3f, 0.5f)); }
  else setBanner("GUARDIAN DOWN", BNAMES[kind], 3, c3(1.6f, 1.4f, 0.6f));
}
static int upList[3];
static void game_openUpgrades(void) {
  Game.paused = 1; Game.state = GS_UPGRADE; Game.btOn = 0; Game.bt = 0;
  score_style(ST_CALM); snd_setMuffle(0.35f, 0.25f);
  Game.draftAfterBoss = Game.afterBoss; Game.rerolls = 2 + Save.meta[5];
  rollUpgrades(Game.afterBoss, upList);
  ui_showUpgrades(upList, Game.wave, Game.afterBoss, 0);
  Game.afterBoss = 0;
}
static void game_pickUpgrade(int u) {
  applyUpgrade(u);
  sfx_ui(UI_OK); snd_setMuffle(0, 0.25f);
  fx_ring(Game.p.x, Game.p.y, 10, 160, 0.6f, c3(1.2f, 1.2f, 1.6f), 2, 48);
  Game.state = GS_PLAY; Game.paused = 0; Game.hasLastTick = 0;
  if (Game.bossRush) {
    for (int i = 0; i < 4; i++) applyUpgrade(randomOpenUpgrade());
    fx_text(Game.p.x, Game.p.y - 60, "+4 AUGMENTS", 16, c3(0.6f, 1.4f, 1.6f), 1.6f);
    startWave(Game.wave + 5);
  } else startWave(Game.wave + 1);
}
static void game_onPlayerDeath(void) {
  if (Game.btOn) { Game.btOn = 0; sfx_bulletTime(0); }
  Game.overT = 0; S.warpOn = 0;
  Score s; mzero(&s, sizeof s); s.has = SC_STYLE | SC_BPM; s.style = ST_OVER; s.bpm = 84; setScore(&s);
}
static i64 game_bankRun(void) { if (Game.banked || !Game.score) return 0; Game.banked = 1; meta_deposit(Game.score); return Game.score; }
static void game_over(int won) {
  Game.state = GS_OVER;
  i64 banked = game_bankRun();
  Record rec; mzero(&rec, sizeof rec);
  rec.won = (u8)won; rec.score = Game.score; rec.wave = Game.wave; rec.start = Game.bossRush ? 0 : Game.startedAt; rec.rush = (u8)Game.bossRush; rec.kills = Game.kills; rec.mult = Game.maxMult; rec.time = (int)(Game.playTime + 0.5f); rec.date = plat_date();
  int rank = Save.nrec; while (rank > 0 && Save.rec[rank - 1].score < rec.score) rank--;
  if (rank < 10) { int n = Save.nrec < 10 ? Save.nrec : 9; memmove(&Save.rec[rank + 1], &Save.rec[rank], sizeof(Record) * (n - rank)); Save.rec[rank] = rec; if (Save.nrec < 10) Save.nrec++; }
  save_write();
  ui_showGameOver(&rec, rank < 10 ? rank : -1, banked, won);
}
static void game_onTick(i64 t) {
  if (t % 96 == 0) { int bar = (int)(t / 96); director_onBar(bar, t); player_bar(bar); boss_bar(bar); }
  if (t % 24 == 0 && Game.hasWarn && Game.warn.out < 0 && ++Game.warn.k <= 8) sfx_klaxon(tickTime(t), (t / 24) % 2 == 1);
  if (t % 24 == 0) {
    i64 beat = t / 24;
    for (int i = 0; i < Game.nen; i++) if (!Game.en[i].dead) enemy_beat(&Game.en[i], beat);
    player_beat(); director_onBeat(beat); boss_beat(beat);
  }
  int steps = stepsAt(t), st = 24 / steps;
  if (t % st == 0) boss_step(t / st, steps);
  if (t % 12 == 0) boss_eighth(t / 12);
  if (Boss.on) boss_tick(t);
  if (t % (24 / Game.st.fireSub) == 0) player_fire(t);
  director_onTick(t);
}
static void game_processTicks(void) {
  i64 tick = (i64)mfloor((float)(gameBeat() * TPB));
  if (!Game.hasLastTick || tick < Game.lastTick || tick - Game.lastTick > 48) { Game.lastTick = tick; Game.hasLastTick = 1; return; }
  while (Game.lastTick < tick) game_onTick(++Game.lastTick);
}
static int nearestEnemy(float x, float y, float maxD, float *tx, float *ty, u32 *id) {
  float bd = maxD * maxD; int found = 0;
  for (int i = 0; i < Game.nen; i++) { Enemy *e = &Game.en[i]; if (e->dead || e->spawn > 0) continue; float d = dist2(x, y, e->x, e->y); if (d < bd) { bd = d; *tx = e->x; *ty = e->y; if (id) *id = e->id; found = 1; } }
  if (boss_targetable() && dist2(x, y, Boss.x, Boss.y) < bd) { *tx = Boss.x; *ty = Boss.y; if (id) *id = BOSS_ID; found = 1; }
  return found;
}
typedef struct { Enemy *e; float dt; } SepCtx;
static int sep_cb(Hit *h, void *ctx) {
  SepCtx *c = ctx; Enemy *e = c->e;
  if (h->boss || h->e == e || h->part != 0 || ETS[h->e->type].isWell) return 1;
  float dx = e->x - h->x, dy = e->y - h->y, d2 = dx * dx + dy * dy, R = (e->r + h->r) * 0.8f;
  if (d2 < R * R && d2 > 0.01f) { float d = msqrt(d2), push = (R - d) * 0.5f * minf(1, c->dt * 20); e->x += dx / d * push; e->y += dy / d * push; }
  return 1;
}
typedef struct { int hurt; } PlCtx;
static int plhit_cb(Hit *h, void *ctx) { Player *p = &Game.p; if ((h->f & HF_HARM) && !hit_dead(h) && dist2(p->x, p->y, h->x, h->y) < (h->r * 0.8f + p->hitR) * (h->r * 0.8f + p->hitR)) { ((PlCtx *)ctx)->hurt = 1; return 0; } return 1; }
static void rebuildHits(void) {
  nhits = 0;
  for (int i = 0; i < Game.nen; i++) { Enemy *e = &Game.en[i]; if (e->dead || e->spawn > 0) continue; hit_add(e->x, e->y, e->r, e, 0, 0, HF_VULN | HF_SOLID | HF_HARM); enemy_hits(e); }
  boss_hits();
  hash_build();
}
/** One step of the arena: player, enemies, guardians, collisions, projectiles, pickups, FX. Shared by play and the title attract mode. */
static void game_simulate(float dt, float rdt, float pdt) {
  Player *p = &Game.p;
  player_update(pdt, rdt);
  Game.nswarm = 0; for (int i = 0; i < Game.nen; i++) if (Game.en[i].type == ET_GNAT && Game.en[i].spawn <= 0) Game.swarm[Game.nswarm++] = i;
  for (int i = 0; i < Game.nen; i++) {
    Enemy *e = &Game.en[i];
    if (e->dead) continue;
    e->t += dt; e->flash = maxf(0, e->flash - dt);
    if (e->spawn > 0) {
      e->spawn -= dt;
      if (e->spawn <= 0) { fx_ring(e->x, e->y, e->r * 2.5f, e->r, 0.2f, e->col, 1.4f, 24); grid_explode(12, e->x, e->y, 90, -30); }
      continue;
    }
    enemy_update(e, dt);
    if (e->dead) continue;
    e->x += e->vx * dt; e->y += e->vy * dt;
    float r = e->r, bn = ETS[e->type].bounce ? 1 : 0.3f;
    if (e->x < r) { e->x = r; e->vx = mfabs(e->vx) * bn; } else if (e->x > ARENA_W - r) { e->x = ARENA_W - r; e->vx = -mfabs(e->vx) * bn; }
    if (e->y < r) { e->y = r; e->vy = mfabs(e->vy) * bn; } else if (e->y > ARENA_H - r) { e->y = ARENA_H - r; e->vy = -mfabs(e->vy) * bn; }
  }
  boss_update(dt);
  Game.nwells = 0;
  for (int i = 0; i < Game.nen; i++) { Enemy *e = &Game.en[i]; if (ETS[e->type].isWell && !e->dead && e->spawn <= 0 && Game.nwells < 15) { Well *w = &Game.wells[Game.nwells++]; w->x = e->x; w->y = e->y; w->r = e->r; w->pr = e->pr; w->pull = 1; w->ppull = 300; w->bpull = 2.4f; w->boss = 0; } }
  { Well w; if (boss_wellInfo(&w)) Game.wells[Game.nwells++] = w; }
  snd_setHum(Game.state == GS_PLAY ? minf(1, Game.nwells * 0.4f) : 0);
  rebuildHits();
  for (int i = 0; i < nhits; i++) {
    Hit *h = &hits[i]; if (h->boss || h->part != 0 || ETS[h->e->type].isWell) continue;
    Enemy *e = h->e; SepCtx c = { e, dt };
    hash_query(e->x - e->r * 2, e->y - e->r * 2, e->x + e->r * 2, e->y + e->r * 2, sep_cb, &c);
  }
  updateBullets(dt, pdt); updateShells();
  if (p->alive && p->inv <= 0 && p->dashT <= 0) { PlCtx c = { 0 }; hash_query(p->x - 80, p->y - 80, p->x + 80, p->y + 80, plhit_cb, &c); if (c.hurt) player_hurt(); }
  updatePickups(pdt);
  { int j = 0; for (int i = 0; i < Game.nen; i++) if (!Game.en[i].dead) { if (j != i) Game.en[j] = Game.en[i]; j++; } Game.nen = j; }
  rebuildHits();
  fx_update(dt); grid_update(dt);
  Game.chainT -= dt;
  Game.flash = maxf(0, Game.flash - rdt * 2.2f);
  Game.aberr = maxf(0, Game.aberr - rdt * 2.5f);
  Game.multPulse = maxf(0, Game.multPulse - rdt * 4); Game.scorePulse = maxf(0, Game.scorePulse - rdt * 5);
  if (Game.hasBanner && (Game.banner.t += rdt) > Game.banner.dur) Game.hasBanner = 0;
  if (Game.hasWarn) {
    Warning *w = &Game.warn; w->t += rdt;
    if (w->out < 0 && ((Boss.on && Boss.spawn <= 0) || w->t > 12 || !p->alive)) w->out = 0;
    if (w->out >= 0 && (w->out += rdt) > 0.7f) { Game.hasWarn = 0; snd_setMuffle(Game.btOn ? 0.5f : 0, 0.15f); }
  }
  float tgt = Game.hasWarn && Game.warn.out < 0 ? 0.25f + 0.45f * Game.beatPulse : p->alive && p->hp == 1 ? 0.5f + 0.5f * msin(Game.time * 4) : 0;
  Game.danger = damp(Game.danger, tgt, Game.hasWarn ? 10 : 4, rdt);
}
static void game_update(float rdt) {
  Player *p = &Game.p;
  if (Game.slowT > 0) Game.slowT -= rdt;
  Game.timeScale = damp(Game.timeScale, Game.slowT > 0 ? Game.slowTo : 1, Game.slowT > 0 ? 18 : 5, rdt);
  int want = p->alive && In.slow && (Game.btOn ? p->energy > 0 : p->energy >= BT_MIN);
  if (want != Game.btOn) { Game.btOn = want; sfx_bulletTime(want); }
  if (want) p->energy = maxf(0, p->energy - BT_DRAIN * rdt);
  Game.bt = damp(Game.bt, want ? 1 : 0, want ? 14 : 7, rdt);
  float dt = rdt * Game.timeScale * lerpf(1, 0.3f, Game.bt), pdt = rdt * Game.timeScale * lerpf(1, 0.8f, Game.bt);
  if (Game.hitstopT > 0) { Game.hitstopT -= rdt; dt *= 0.08f; pdt *= 0.08f; }
  Game.time += dt; Game.simDt += dt;
  if (p->alive) Game.playTime += rdt;
  Game.noBombMsg = maxf(0, Game.noBombMsg - rdt);
  Game.visBeat = visBeat();
  { float f = (float)(Game.visBeat - mfloor((float)Game.visBeat)); Game.beatPulse = (1 - f) * (1 - f) * (1 - f); }
  field_update(Game.visBeat);
  if (p->alive) game_processTicks(); else Game.hasLastTick = 0;
  game_simulate(dt, rdt, pdt);
  float look = p->alive ? 70 : 0;
  Game.tiltGoal = p->alive ? 0 : 1.28f;
  Cam.tilt = damp(Cam.tilt, Game.tiltGoal, p->alive ? 2.6f : 0.7f, rdt);
  float lgx = p->alive ? -(p->x - ARENA_W / 2) / (ARENA_W / 2) * 0.09f - p->vx * 0.00012f : 0, lgy = p->alive ? -(p->y - ARENA_H / 2) / (ARENA_H / 2) * 0.09f - p->vy * 0.00012f : 0;
  Cam.lx = damp(Cam.lx, lgx, 2.5f, rdt); Cam.ly = damp(Cam.ly, lgy, 2.5f, rdt);
  cam_update(rdt, p->x + mcos(p->aim) * look, p->y + msin(p->aim) * look, 5.5f, 1);
  Cam.zoom = damp(Cam.zoom, Boss.on && !Boss.dead ? 0.86f : 1, 1.5f, rdt);
  if (directorCleared() == 1) waveCleared();
  if (Director.state == DS_CLEARED && Game.state == GS_PLAY) { Game.clearT += rdt; if (Game.won) { if (Game.clearT > 4.5f) game_over(1); } else if (Game.clearT > 2.0f && p->alive) game_openUpgrades(); }
  if (!p->alive) { Game.overT += rdt; if (Game.overT > 2.8f && Game.state == GS_PLAY) game_over(0); }
}

/* ---------------- title / attract: a computer pilot plays a real (invulnerable) round ---------------- */
static void game_startDemo(void) {
  Game.demo = 1; Game.god = 1; Game.paused = 0; Game.hasWarn = 0;
  mzero(&Game.p, sizeof Game.p);
  Player *p = &Game.p; p->x = ARENA_W / 2; p->y = ARENA_H / 2; p->r = 13; p->hitR = 3.5f; p->energy = E_MAX; p->ang = p->aim = -PI / 2; p->alive = 1; p->hp = 99; p->bombs = 3; p->ddx = 1;
  u8 m0[8] = { 0 }; Game.st = baseStats(m0); Game.st.spread = 1; Game.st.orbit = 1; Game.st.fireSub = 6; Game.st.bombsMax = 3; Game.st.maxHp = 99;
  mzero(Game.upg, sizeof Game.upg); game_syncLayers();
  Game.nen = Game.nbu = Game.neb = Game.nsh = Game.npk = Game.nmi = Game.nbo = Game.nbm = 0; Game.bombWave.on = 0;
  Boss.on = 0; Game.nwells = 0; Game.hasBanner = 0;
  Game.score = 0; Game.mult = 1; Game.wave = 6; Game.hiScore = (i64)1 << 62; Game.diff.speed = 0.85f; Game.diff.hp = 1;
  Game.slowT = 0; Game.timeScale = 1; Game.hitstopT = 0;
  Director.state = DS_IDLE; Director.nq = 0; Director.boss = 0;
  Game.demoBomb = 9; Game.demoBar = -1; Game.hasLastTick = 0;
  snd_setSfxScale(0.32f);
}
static void game_demoSpawn(void) {
  Player *p = &Game.p; if (Game.nen > 18) return;
  static const int TYPES[12] = { ET_MOTE, ET_MOTE, ET_DART, ET_DART, ET_GNAT, ET_LUNGER, ET_CLUSTER, ET_FLICKER, ET_BEACON, ET_BOLA, ET_VORTEX, ET_MORTAR };
  int type = TYPES[rndi(0, 11)];
  int k = type == ET_GNAT ? 9 : (type == ET_BOLA || type == ET_VORTEX || type == ET_MORTAR || type == ET_BEACON) ? 1 : rndi(3, 5);
  float a0 = rnd(TAU), R = rndr(380, 560);
  for (int i = 0; i < k; i++) { float a = a0 + (type == ET_GNAT ? rndr(-0.3f, 0.3f) : i * TAU / k); spawnEnemy(type, p->x + mcos(a) * R, p->y + msin(a) * R, -1); }
}
static void game_demoInput(void) {
  Player *p = &Game.p; float mx = (ARENA_W / 2 - p->x) * 0.0016f, my = (ARENA_H * 0.45f - p->y) * 0.0016f; int nearN = 0;
  for (int i = 0; i < Game.nen; i++) { Enemy *e = &Game.en[i]; float dx = p->x - e->x, dy = p->y - e->y, d2 = dx * dx + dy * dy; if (d2 < 280 * 280) { float d = msqrt(d2); if (d == 0) d = 1; mx += dx / d * (280 - d) / 90; my += dy / d * (280 - d) / 90; nearN++; } }
  for (int i = 0; i < Game.neb; i++) { EBullet *b = &Game.eb[i]; float dx = p->x - b->x, dy = p->y - b->y, d2 = dx * dx + dy * dy; if (d2 < 130 * 130) { float d = msqrt(d2); if (d == 0) d = 1; mx += dx / d * 2; my += dy / d * 2; } }
  mx += mcos(Game.titleT * 0.7f) * 0.25f; my += msin(Game.titleT * 0.5f) * 0.25f;
  float m = mhypot(mx, my); if (m > 1) { mx /= m; my /= m; }
  float tx, ty; int t = nearestEnemy(p->x, p->y, 900, &tx, &ty, NULL);
  In.mode = 1; In.moveX = mx; In.moveY = my; In.fire = t; In.aimStick = t;
  if (t) { float a = matan2(ty - p->y, tx - p->x); In.aimX = mcos(a); In.aimY = msin(a); }
  double vb = visBeat(); float off = (float)(vb - (double)(i64)(vb + 0.5));
  In.dash = p->dashCd <= 0 && mfabs(off) * spb() < 0.04f && nearN >= 3 && chance(0.35f);
  In.bomb = Game.demoBomb <= 0 && (nearN >= 7 || Game.nen >= 16);
  if (In.bomb) { Game.demoBomb = rndr(9, 14); p->bombs = 3; }
  dashAge = 0;
}
static float titleMx = -1e9f, titleMy;
static void game_updateTitle(float rdt) {
  if (!Game.demo) game_startDemo();
  Game.titleT += rdt; Game.simDt += rdt; Game.time += rdt; Game.demoBomb -= rdt;
  Game.visBeat = visBeat();
  { float f = (float)(Game.visBeat - mfloor((float)Game.visBeat)); Game.beatPulse = (1 - f) * (1 - f) * (1 - f); }
  field_update(Game.visBeat);
  i64 bar = (i64)mfloor((float)(gameBeat() / 4));
  if (bar != Game.demoBar) { if (Game.demoBar >= 0) game_demoSpawn(); Game.demoBar = bar; }
  InputState keep = In; In.slow = 0;
  game_demoInput();
  game_processTicks();
  game_simulate(rdt, rdt, rdt);
  In = keep;
  float wx, wy; cam_toWorld(In.mouseX, In.mouseY, 0, &wx, &wy);
  if (titleMx > -1e8f && In.mouseSeen && wy < ARENA_H + 400) {
    float dx = wx - titleMx, dy = wy - titleMy;
    if (dx * dx + dy * dy > 1 && dx * dx + dy * dy < 40000) { grid_directed(dx * 0.03f, dy * 0.03f, 0, wx, wy, 90); if (chance(0.5f)) emit(wx, wy, 4, dx * 8 + rndr(-40, 40), dy * 8 + rndr(-40, 40), rndr(40, 200), 0.6f, 1.2f, 0.5f, 1.4f, 1.2f, 0.95f, 0.03f, 25); }
  }
  titleMx = wx; titleMy = wy;
  Player *p = &Game.p;
  Cam.trauma = minf(Cam.trauma, 0.25f); Game.flash = minf(Game.flash, 0.15f);
  Cam.zoom = damp(Cam.zoom, 1, 2, rdt);
  Cam.tilt = damp(Cam.tilt, 1.4f, 1.2f, rdt); Cam.lx = damp(Cam.lx, 0, 2, rdt); Cam.ly = damp(Cam.ly, 0, 2, rdt);
  cam_update(rdt, lerpf(ARENA_W / 2, p->x, 0.35f) + mcos(Game.titleT * 0.11f) * 100, lerpf(ARENA_H * 0.42f, p->y, 0.3f), 2, 0);
  Game.danger = 0;
}

/* =========================================================================
   HUD — arcade cabinet overlay drawn with the same glowing vector strokes,
   on the 640×480 output.
   ========================================================================= */
static float hudTopLim, hudBotLim;
static float hud_fit(const char *str, float size, float maxW, float sp) { float w = font_width(str, 1, sp); return w > 0 ? minf(size, maxW / w) : size; }
static FOpt fo(int align, float spacing, float w, int italic, const C3 *vgrad) { FOpt o; mzero(&o, sizeof o); o.align = align; o.spacing = spacing; o.w = w; o.italic = italic; o.vgrad = vgrad; return o; }
static void hud_logo(Batch *b, float cx, float y, float size) {
  const char *str = "PHASE"; float sp = 1.3f, t = (float)plat_time(), vb = (float)Game.visBeat, travel = mfmod(mfmod(vb, 4) + 4, 4) / 4 * 7 - 1;
  int glitch = (int)mfloor(t * 9) % 23 == 0; float jit[10];
  for (int i = 0; i < 5; i++) { jit[i * 2] = glitch && (i * 7 + (int)mfloor(t * 9)) % 3 == 0 ? rndr(-size * 0.12f, size * 0.12f) : 0; jit[i * 2 + 1] = -size * 0.09f * maxf(0, 1 - mfabs(i - travel)); }
  float k = 1 + Game.beatPulse * 0.45f, split = (2.5f + Game.beatPulse * 3.5f) * size / 100;
  FOpt o = fo(AL_CENTER, sp, 1.25f, 1, NULL); o.jit = jit;
  font_draw(b, str, cx - split, y + split * 0.5f, size, c3(0.4f, 0, 0.22f), &o);
  font_draw(b, str, cx + split, y - split * 0.3f, size, c3(0, 0.18f, 0.4f), &o);
  C3 vg[2] = { cmul(CHROME[0], k), cmul(CHROME[1], k) }; o.w = 1.3f; o.vgrad = vg;
  font_draw(b, str, cx, y, size, c3(1, 1, 1), &o);
  int cur = ((int)mfloor(vb) % 4 + 4) % 4; float yy = y + size * 1.45f;
  for (int i = 0; i < 4; i++) { int on = i == cur; float rr = on ? 3 + Game.beatPulse * 3 : 2; bdot(b, cx + (i - 1.5f) * 18, yy, rr, on ? c3(1.6f, 0.6f, 1.5f) : c3(0.4f, 0.15f, 0.45f)); }
}
static void hud_bar(Batch *b, float x, float y, float bw, float bh, float f, C3 c, float S) {
  float o = 0.75f, lw = 1.3f * S * 0.5f; C3 co = cmul(c, o);
  bline(b, x, y, x + bw, y, lw, co); bline(b, x + bw, y, x + bw, y + bh, lw, co); bline(b, x + bw, y + bh, x, y + bh, lw, co); bline(b, x, y + bh, x, y, lw, co);
  if (f > 0) for (float yy = y + 2.5f * S; yy <= y + bh - 2.5f * S; yy += 1.6f * S) bline(b, x + 3 * S, yy, x + 3 * S + (bw - 6 * S) * f, yy, 0.45f * S, cmul(c, 1.35f));
}
static void hud_warning(Batch *b, float W, float H, float xL);
static void hud_game(Batch *b, float W, float H, float rdt) {
  Player *p = &Game.p; Stats *st = &Game.st; char buf[64];
  float S = clampf(minf(W / 1000, H / 640), 0.62f, 1.45f), m = (float)mlrint(clampf(22 * S, 12, 30));
  float xL = m, xR = W - m, yT = m, yB = H - m, colW = W * 0.42f;
  /* world-anchored floating text */
  for (int i = 0; i < nTexts; i++) {
    TextFx *t = &fxTexts[i]; float sx, sy; if (!cam_toUI(t->x, t->y, LIFT, &sx, &sy)) continue;
    float k = t->t / t->dur, a = k < 0.7f ? 1 : 1 - (k - 0.7f) / 0.3f, sz = t->size * (k < 0.12f ? easeOutBack(k / 0.12f) : 1) * 0.95f * 0.8f;
    FOpt o = fo(AL_CENTER, 1, 1, 1, NULL); font_draw(b, t->str, sx, sy - sz / 2, sz, cmul(t->col, a), &o);
  }
  /* top-left: hull bars, bomb shells, energy */
  int nBars = st->maxHp + (st->shield ? 1 : 0); float gap = 6 * S, bw = minf(40 * S, (colW - gap * (nBars - 1)) / nBars), bh = maxf(8, minf(15 * S, bw * 0.4f));
  int low = p->hp == 1; float pu = low ? 0.7f + 0.5f * msin(Game.time * 9) : 1;
  for (int i = 0; i < st->maxHp; i++) hud_bar(b, xL + i * (bw + gap), yT, bw, bh, i < p->hp ? 1 : 0, i < p->hp ? (low ? c3(1.6f * pu, 0.35f * pu, 0.6f * pu) : c3(0.45f, 1.25f, 1.5f)) : c3(0.45f, 0.2f, 0.5f), S);
  if (st->shield) hud_bar(b, xL + st->maxHp * (bw + gap), yT, bw, bh, p->shield > 0 ? 1 : p->shieldBars / (st->shield >= 2 ? 4.f : 8.f), cmul(COL_shield, p->shield > 0 ? 1.3f : 0.7f), S);
  float leftW = nBars * (bw + gap) - gap, br = minf(8.5f * S, (colW / st->bombsMax) / 2.9f), by = yT + bh + 10 * S + br;
  for (int i = 0; i < st->bombsMax; i++) {
    float x = xL + br + i * br * 2.9f; int have = i < p->bombs;
    bcircle(b, x, by, br, 20, (have ? 1.5f : 0.9f) * S * 0.5f, have ? cmul(COL_bomb, 1.3f) : c3(0.3f, 0.18f, 0.12f), 0);
    if (have) bdot(b, x, by, 2.8f * S, cmul(COL_bomb, 1.7f));
  }
  float ey = by + br + 10 * S, eh = maxf(5, 7 * S), ew = maxf(leftW, minf(150 * S, colW)) - 14 * S, ex = xL + 14 * S;
  float cost = DASH_COST * mpow(0.8f, (float)st->dashDmg), ef = p->energy / E_MAX; int ready = p->energy >= cost;
  C3 ec = Game.btOn ? c3(1.6f, 0.4f, 1.5f) : ready ? c3(0.45f, 1.25f, 1.6f) : c3(1.4f, 0.3f, 0.4f); float eo = ready && !Game.btOn ? 1 + Game.beatPulse * 0.4f : 1;
  static const float BOLT[12] = { 0.2f, -1, -0.5f, 0.15f, 0.1f, 0.15f, -0.2f, 1, 0.5f, -0.15f, -0.1f, -0.15f };
  bpoly(b, BOLT, 6, xL + 5 * S, ey + eh / 2, 0, 7 * S, 0.6f * S, ec, 1);
  bline(b, ex, ey, ex + ew, ey, 0.4f, c3(0.3f, 0.2f, 0.45f)); bline(b, ex, ey + eh, ex + ew, ey + eh, 0.4f, c3(0.3f, 0.2f, 0.45f));
  for (float yy = ey + 1.5f; yy <= ey + eh - 1.5f; yy += 1.5f) bline(b, ex, yy, ex + ew * ef, yy, 0.45f, cmul(ec, eo));
  for (float q = DASH_COST; q < E_MAX; q += DASH_COST) bline(b, ex + ew * q / E_MAX, ey - 2, ex + ew * q / E_MAX, ey + eh + 2, 0.4f, c3(0.9f, 0.8f, 1.1f));
  float leftBottom = ey + eh;
  /* top-right: score rolls up toward the real value */
  float diff = (float)(Game.score - (i64)Game.shownScore);
  if (mfabs(diff) < 1) Game.shownScore = (float)Game.score; else Game.shownScore += diff * minf(1, rdt * 12) + signf(diff) * minf(mfabs(diff), rdt * 400);
  int rolling = mfabs(diff) >= 1; float sc = 1 + Game.scorePulse * 0.06f;
  s_fmt(buf, (i64)Game.shownScore); float ss = hud_fit(buf, 40 * S, colW, 1) * sc;
  C3 rollg[2] = { c3(1.4f, 1.4f, 1.5f), CHROME[1] };
  FOpt o = fo(AL_RIGHT, 1, 1.1f, 1, rolling ? rollg : CHROME); font_draw(b, buf, xR, yT, ss, c3(1, 1, 1), &o);
  float rightW = font_width(buf, ss, 1);
  char hb[40]; s_cpy(hb, "HI "); s_fmt(hb + 3, Game.score > Game.hiScore ? Game.score : Game.hiScore); float hs = hud_fit(hb, 13 * S, colW, 1);
  o = fo(AL_RIGHT, 1, 1, 0, NULL); font_draw(b, hb, xR, yT + ss * 1.25f, hs, c3(0.8f, 0.6f, 1.05f), &o);
  float rightBottom = yT + ss * 1.25f + hs;
  /* top-centre: guardian bar or sector progress */
  float leftExt = xL + maxf(leftW, ew + 14 * S), rightExt = xR - rightW, centreFree = 2 * (minf(W / 2 - leftExt, rightExt - W / 2) - 16 * S);
  int own = centreFree < 180 * S; float cy0 = own ? maxf(leftBottom, rightBottom) + 12 * S : yT, cw = own ? minf(480 * S, xR - xL) : minf(480 * S, centreFree);
  int steps = snd_steps(), bpm = mlrint(snd_bpm()), warp = snd_isWarp(); float centreBottom = cy0;
  if (Boss.on) {
    float x0 = W / 2 - cw / 2, f = Boss.dead ? 0 : clampf(Boss.hp / Boss.maxHp, 0, 1), intro = Boss.spawn > 0 ? 1 - Boss.spawn / Boss.spawnMax : 1; C3 c = BCOLS[Boss.kind];
    float ns = hud_fit(BNAMES[Boss.kind], 16 * S, cw, 1.3f), y = cy0 + ns + 12 * S;
    o = fo(AL_CENTER, 1.3f, 1, 1, NULL); font_draw(b, BNAMES[Boss.kind], W / 2, cy0, ns, cmul(c, 1.4f), &o);
    bline(b, x0, y, x0 + cw, y, 0.3f, cmul(c, 0.3f));
    bline(b, x0, y, x0 + cw * f * intro, y, 1.5f, cmul(c, 1.5f));
    for (int q = 0; q < 4; q++) bline(b, x0 + cw * q / 3, y - 6, x0 + cw * q / 3, y + 6, 0.4f, c3(0.7f, 0.55f, 0.8f));
    centreBottom = y + 8;
    if (warp) { float k = 1 + Game.beatPulse * 0.5f, ws = 18 * S * k; s_printf(buf, "\x0E %d BPM", bpm); o = fo(AL_CENTER, 1, 1, 1, NULL); font_draw(b, buf, W / 2, y + 12 * S, ws, c3(1.5f, 0.5f + (bpm - 90) / 65.f, 1.6f), &o); centreBottom = y + 12 * S + ws; }
  } else if (Director.state == DS_SPAWN) {
    float w2 = minf(200 * S, cw), x0 = W / 2 - w2 / 2, y = cy0 + 6, f = director_progress();
    bline(b, x0, y, x0 + w2, y, 0.3f, c3(0.3f, 0.15f, 0.4f));
    bline(b, x0, y, x0 + w2 * f, y, 0.75f, c3(1.4f, 0.5f, 1.4f));
    if (f >= 1 && Game.nen) { s_printf(buf, "REMAINING %d", Game.nen); o = fo(AL_CENTER, 1, 1, 0, NULL); font_draw(b, buf, W / 2, y + 9, 9 * S, c3(0.8f, 0.7f, 1.1f), &o); }
    centreBottom = y + 22 * S;
  }
  /* bottom-left: combo */
  s_printf(buf, "x%d", Game.mult); float cs = hud_fit(buf, 48 * S, colW * 0.9f, 1);
  float mp = 1 + Game.multPulse * 0.35f + Game.beatPulse * 0.1f, glow = 1 + Game.beatPulse * 0.5f + Game.multPulse * 0.6f, cy = yB - cs;
  o = fo(AL_LEFT, 1.25f, 1, 0, NULL); font_draw(b, "COMBO", xL, cy - 20 * S, 13 * S, c3(1.4f, 0.4f, 1.1f), &o);
  o = fo(AL_LEFT, 1, 1.15f, 1, NULL); font_draw(b, buf, xL, cy - (mp - 1) * cs * 0.5f, cs * mp, c3(1.7f * glow, 1.3f * glow, 0.35f * glow), &o);
  /* bottom-right: sector + tempo */
  char sec[32]; if (Game.wave <= BOSS_LAST) s_printf(sec, "SECTOR %2 / %d", Game.wave, BOSS_LAST); else s_printf(sec, "SECTOR %2", Game.wave);
  float secS = hud_fit(sec, 24 * S, colW, 1), ry = yB - secS;
  o = fo(AL_RIGHT, 1, 1, 1, NULL); font_draw(b, sec, xR, ry, secS, c3(1.0f, 1.15f, 1.5f), &o);
  s_printf(buf, "%d BPM  %s", bpm, steps == 3 ? "12/8" : "4/4"); o = fo(AL_RIGHT, 1, 1, 0, NULL); font_draw(b, buf, xR, ry - 18 * S, 12 * S, warp ? c3(1.6f, 0.5f, 1.4f) : c3(0.75f, 0.65f, 1.05f), &o);
  /* beat indicator: 4 diamonds in 4/4, 4 × 3 pips in 12/8 */
  float vb = (float)Game.visBeat; int cur = ((int)mfloor(vb) % 4 + 4) % 4; float yb = yB - 6;
  for (int i = 0; i < 4; i++) {
    int on = i == cur; float x = W / 2 + (i - 1.5f) * (steps == 3 ? 34 : 22), r = on ? 3.5f + Game.beatPulse * 3 : 2.2f, k = on ? 1 + Game.beatPulse : 0.3f;
    POLY(b, SH_diamond, x, yb, -PI / 2, r * 1.4f, 0.65f, c3(1.4f * k, 0.5f * k, 1.4f * k));
    if (steps == 3) for (int j = 1; j < 3; j++) { int sub = on && (vb - mfloor(vb)) * 3 >= j; bdot(b, x + j * 8, yb, sub ? 2 : 1.2f, sub ? c3(0.6f, 1.2f, 1.5f) : c3(0.2f, 0.3f, 0.45f)); }
  }
  /* banner */
  if (Game.hasBanner) {
    Banner *bn = &Game.banner; float k = bn->t < 0.3f ? bn->t / 0.3f : bn->t > bn->dur - 0.5f ? (bn->dur - bn->t) / 0.5f : 1;
    float sp = 1 + (1 - easeOutCubic(clampf(bn->t / 0.6f, 0, 1))) * 0.8f, size = hud_fit(bn->text, clampf(W * 0.045f, 22, 52), W - 2 * xL, sp);
    float y = maxf(H * 0.26f, centreBottom + 16 * S);
    C3 vg[2] = { cmul(c3(1.7f, 1.7f, 1.8f), k), cmul(bn->col, k) };
    o = fo(AL_CENTER, sp, 1.15f, 1, vg); font_draw(b, bn->text, W / 2, y, size, c3(1, 1, 1), &o);
    if (bn->sub[0]) { o = fo(AL_CENTER, 1.15f, 1, 0, NULL); font_draw(b, bn->sub, W / 2, y + size * 1.45f, hud_fit(bn->sub, clampf(size * 0.3f, 9, 15), W - 2 * xL, 1.15f), c3(0.85f * k, 0.75f * k, 1.1f * k), &o); }
    float lw = minf(W / 2 - xL, font_width(bn->text, size, sp) / 2 + 30) * easeOutCubic(clampf(bn->t / 0.5f, 0, 1));
    bline(b, W / 2 - lw, y - size * 0.45f, W / 2 + lw, y - size * 0.45f, 0.45f, c3(1.5f * 0.5f * k, 0.4f * 0.5f * k, 1.3f * 0.5f * k));
    bline(b, W / 2 - lw, y + size * 1.15f, W / 2 + lw, y + size * 1.15f, 0.45f, c3(0.4f * 0.5f * k, 1.2f * 0.5f * k, 1.5f * 0.5f * k));
  }
  hudTopLim = maxf(leftBottom, rightBottom) + 8; hudBotLim = minf(cy - 20 * S, ry - 18 * S) - 8;
  hud_warning(b, W, H, xL);
  if (Game.tutorial && Game.wave == 1 && Game.state == GS_PLAY) {
    const char *msg = In.mode == 1 ? "L-STICK MOVE  \xB7  R-STICK FIRE  \xB7  BUTTON 1 DASH ON THE BEAT  \xB7  BUTTON 3 SLOW" : "WASD MOVE  \xB7  CLICK DASH ON THE BEAT  \xB7  RIGHT-CLICK SLOW  \xB7  Q BOMB";
    float a = 0.55f + 0.25f * Game.beatPulse; o = fo(AL_CENTER, 1, 1, 0, NULL);
    font_draw(b, msg, W / 2, maxf(centreBottom, maxf(leftBottom, rightBottom)) + 12 * S, hud_fit(msg, 11 * S, W - 2 * xL, 1), c3(0.9f * a, 0.75f * a, 1.3f * a), &o);
  }
  /* off-screen threats */
  if (p->alive) {
    int n = 0;
    for (int i = 0; i <= Game.nen; i++) {
      float x, y; C3 c; int big;
      if (i < Game.nen) { if (n++ > 40) continue; Enemy *e = &Game.en[i]; x = e->x; y = e->y; c = e->col; big = e->r > 20; }
      else { if (!Boss.on || Boss.dead) break; x = Boss.x; y = Boss.y; c = BCOLS[Boss.kind]; big = 1; }
      float sx, sy; if (!cam_toUI(x, y, LIFT, &sx, &sy)) continue;
      if (sx > -10 && sx < W + 10 && sy > -10 && sy < H + 10) continue;
      float dx = sx - W / 2, dy = sy - H / 2, kk = minf((W / 2 - 16) / maxf(mfabs(dx), 1e-3f), (H / 2 - 16) / maxf(mfabs(dy), 1e-3f));
      POLY(b, SH_tri, W / 2 + dx * kk, H / 2 + dy * kk, matan2(dy, dx), big ? 11 : 6, 0.6f, cmul(c, 1.1f));
    }
  }
  if (In.mode == 0 && Game.state == GS_PLAY && !Game.paused && p->alive) {
    float x = In.mouseX / US, y = In.mouseY / US, rot = mfmod(vb, 1) * PI / 2, k = 1.1f + Game.beatPulse * 0.5f;
    bcircle(b, x, y, 9, 16, 0.45f, c3(1.1f * k, 0.45f * k, 1.2f * k), 0);
    for (int i = 0; i < 4; i++) { float a = rot + i * PI / 2; bline(b, x + mcos(a) * 12, y + msin(a) * 12, x + mcos(a) * 17, y + msin(a) * 17, 0.55f, c3(0.6f * k, 1.1f * k, 1.3f * k)); }
    bdot(b, x, y, 1.0f, c3(1.3f, 1.2f, 1.3f));
  }
}
/** Guardian warning screen: hazard bands, tickers, chevrons and a reticle behind a flashing WARNING. */
static void hud_warning(Batch *b, float W, float H, float xL) {
  if (!Game.hasWarn) return;
  Warning *w = &Game.warn; float t = w->t, kin = easeOutCubic(clampf(t / 0.5f, 0, 1)), kout = w->out >= 0 ? 1 - clampf(w->out / 0.7f, 0, 1) : 1, a = kin * kout;
  if (a <= 0.001f) return;
  float vb = (float)Game.visBeat, bp = Game.beatPulse; int half = (vb - mfloor(vb)) < 0.5f, prime = w->kind == BK_PRIME;
  C3 red = c3(1.7f, 0.2f, 0.3f), bc = cmul(w->col, 1.4f);
  float top = hudTopLim, bot = hudBotLim > 0 ? hudBotLim : H, avail = maxf(60, bot - top);
  float hb = clampf(H * 0.055f, 16, 46), ts = clampf(hb * 0.4f, 8, 15), size0 = hud_fit("WARNING", clampf(W * 0.11f, 38, 130), W * 0.84f, 1.15f);
  float ns = hud_fit(w->name, clampf(minf(W * 0.034f, size0 * 0.32f), 12, 32), W * 0.8f, 1.3f), subS = clampf(ns * 0.45f, 8, 14);
  float need = size0 + 8 + ns * 1.5f + subS + 2 * (14 + hb + 6 + ts), kfit = minf(1, avail / need);
  hb *= kfit; ts *= kfit; size0 *= kfit; ns *= kfit; subS *= kfit;
  float textH = size0 + 8 + ns * 1.5f + subS, blockTop = top + (avail - textH) / 2, cy = blockTop + size0 * 0.62f, ny = blockTop + size0 + 8;
  float bandTop = blockTop - 14 * kfit - hb, bandBot = blockTop + textH + 14 * kfit;
  float fe = (0.25f + bp * 0.9f) * a; C3 rf = cmul(red, fe);
  bline(b, 2, 2, W - 2, 2, 0.75f, rf); bline(b, 2, H - 2, W - 2, H - 2, 0.75f, rf); bline(b, 2, 2, 2, H - 2, 0.75f, rf); bline(b, W - 2, 2, W - 2, H - 2, 0.75f, rf);
  float R = minf(W * 0.3f, (bandBot - bandTop) * 0.5f), ra = 0.35f * a;
  for (int i = 0; i < 3; i++) { float a0 = t * 0.9f + i * TAU / 3; barc(b, W / 2, cy, R, a0, a0 + 1.3f, 18, 0.7f, cmul(bc, ra)); }
  for (int i = 0; i < 24; i++) { float aa = -t * 0.5f + i * TAU / 24, r0 = R * 0.84f, r1 = R * (i % 6 ? 0.88f : 0.93f); bline(b, W / 2 + mcos(aa) * r0, cy + msin(aa) * r0, W / 2 + mcos(aa) * r1, cy + msin(aa) * r1, 0.45f, cmul(bc, ra)); }
  char tick[160]; s_printf(tick, " GUARDIAN APPROACHING  \xB7  %s  \xB7  %s  \xB7  GUARDIAN %d / 5  \xB7 ", w->name, w->sig, w->idx); float tw = font_width(tick, ts, 1);
  for (int bi = 0; bi < 2; bi++) {
    float dir = bi ? -1.f : 1.f, y0 = bi ? bandBot : bandTop, slide = (1 - kin) * W * dir, per = hb * 1.4f, off = mfmod(mfmod(t * 150 * dir, per) + per, per), k = a;
    bline(b, slide, y0, W + slide, y0, 0.8f, cmul(red, k)); bline(b, slide, y0 + hb, W + slide, y0 + hb, 0.8f, cmul(red, k));
    for (float x = -per * 2 + off + slide; x < W + per + slide; x += per) {
      C3 scl = prime ? cmul(hsl(fractf(x / W + t * 0.2f), 0.95f, 0.6f), 1.5f) : (bi ? red : c3(1.7f, 0.9f, 0.2f));
      bline(b, x, y0 + hb - 3, x + hb * 0.7f, y0 + 3, per * 0.085f, cmul(scl, k * 0.8f));
    }
    float ty = bi ? y0 + hb + 6 : y0 - ts - 6, toff = mfmod(mfmod(t * 80 * -dir, tw) + tw, tw);
    for (float x = -tw + toff; x < W; x += tw) font_draw(b, tick, x, ty, ts, cmul(bc, k * 0.8f), NULL);
  }
  float size = size0 * (1 + bp * 0.06f), wy = cy - size * 0.62f, fl = (half ? 1 : 0.55f) * a, sp = 2 + bp * 7;
  FOpt o = fo(AL_CENTER, 1.15f, 1.3f, 1, NULL);
  font_draw(b, "WARNING", W / 2 - sp, wy + sp * 0.4f, size, c3(0.6f * fl, 0, 0.1f * fl), &o);
  font_draw(b, "WARNING", W / 2 + sp, wy - sp * 0.3f, size, c3(0, 0.3f * fl, 0.5f * fl), &o);
  C3 vg[2] = { cmul(c3(1.8f, 1.7f, 1.8f), fl), cmul(red, fl) }; o.w = 1.35f; o.vgrad = vg;
  font_draw(b, "WARNING", W / 2, wy, size, c3(1, 1, 1), &o);
  o = fo(AL_CENTER, 1.3f, 1, 1, NULL); font_draw(b, w->name, W / 2, ny, ns, cmul(bc, a), &o);
  char sub[96]; s_printf(sub, "%sGUARDIAN %d OF 5 \xB7 %s", w->rush ? "BOSS RUSH \xB7 " : "", w->idx, w->sig);
  o = fo(AL_CENTER, 1.1f, 1, 0, NULL); font_draw(b, sub, W / 2, ny + ns * 1.5f, hud_fit(sub, subS, W * 0.86f, 1.1f), c3(0.9f * a, 0.75f * a, 1.1f * a), &o);
  float room = (W - font_width("WARNING", size, 1.15f)) / 2 - 16;
  if (room > 50) for (int i = 0; i < 5; i++) {
    float f = mfmod(t * 1.2f + i / 5.f, 1), x = lerpf(xL, room, f), ca = msin(f * PI) * a, cs = clampf(size0 * 0.22f, 8, 22);
    POLY(b, SH_chevron, x, cy, 0, cs, 0.8f, cmul(red, ca)); POLY(b, SH_chevron, W - x, cy, PI, cs, 0.8f, cmul(red, ca));
  }
}

/* =========================================================================
   RENDER — one frame of the arena into the scene buffer, then composite.
   ========================================================================= */
static float frameBloom = 1, menuDim = 0;
static void game_drawWorld(Batch *b, Batch *fb) {
  for (int i = 0; i < Game.npk; i++) {
    Pickup *g = &Game.pk[i]; float fade = g->life - g->t < 1.5f ? ((int)mfloor(g->t * 12) % 2 ? 0.25f : 1) : 1, s = minf(1, g->t * 6);
    if (g->type == PK_GEOM) { POLY(b, SH_diamond, g->x, g->y, g->ang, 8 * s, 1.3f, cmul(COL_geom, 1.3f * fade)); }
    else if (g->type == PK_HEAL) { C3 c = cmul(COL_heal, (1.5f + Game.beatPulse) * fade); bngon(b, g->x, g->y, 13, 6, g->ang * 0.3f, 1.6f, c); bline(b, g->x - 6, g->y, g->x + 6, g->y, 1.8f, c); bline(b, g->x, g->y - 6, g->x, g->y + 6, 1.8f, c); }
    else { C3 c = cmul(COL_bomb, (1.5f + Game.beatPulse) * fade); bcircle(b, g->x, g->y, 11, 16, 1.6f, c, 0); bdot(b, g->x, g->y, 3.5f, c); }
  }
  for (int i = 0; i < Game.nen; i++) {
    Enemy *e = &Game.en[i]; int fl = e->flash > 0; float k = 1.55f, sc = 1;
    if (e->spawn > 0) {
      float s = 1 - e->spawn / e->spawnMax; k *= s * ((int)mfloor(e->t * 24) % 2 ? 0.5f : 1); sc = 0.4f + 0.6f * easeOutBack(s);
      fb->z = 1; bcircle(fb, e->x, e->y, e->r * (1 + (1 - s) * 3.5f), 24, 1.2f, cmul(e->col, s), e->t * 3); fb->z = LIFT;
    }
    C3 c = c3(lerpf(e->col.r, 1.5f, fl) * k, lerpf(e->col.g, 1.5f, fl) * k, lerpf(e->col.b, 1.5f, fl) * k);
    enemy_draw(e, b, c, sc);
  }
  boss_draw(b, fb);
  drawShells(b, fb);
  fb->z = LIFT; drawProjectiles(fb);
  player_draw(b, fb);
}
/* The world pass (grid, dark batch, reflection, particles, floor, world) in row bands, one per thread:
   each band draws everything that touches its rows, in the same order. */
static struct { float gpulse, refl, bass, sparkle, now; int n; } RJ;
static volatile long rjNext;
static void raster_band(int i) {
  RBand rb; rb.y0 = RH * i / RJ.n; rb.y1 = RH * (i + 1) / RJ.n; rb.sub = 0;
  draw_grid_lines(&rb, RJ.gpulse, 1, palCur.north, palCur.south, palCur.hot);
  draw_batch_b(&rb, &Bdark, Cam.vp, 1, 1);
  if (RJ.refl > 0.005f) draw_batch_b(&rb, &Bworld, Cam.refl, RJ.refl, 0);
  parts_draw_b(&rb, Cam.vp, RJ.bass, RJ.sparkle, RJ.now);
  draw_batch_b(&rb, &Bfloor, Cam.vp, 1, 0);
  draw_batch_b(&rb, &Bworld, Cam.vp, 1, 0);
}
/* Bands are finer than threads and claimed in turn, so a band full of boss geometry does not hold the
   others back (work is uneven down the screen). Bands touch disjoint rows, so the result is the same. */
static void raster_job(int w) { (void)w; for (;;) { int i = (int)__sync_fetch_and_add(&rjNext, 1); if (i >= RJ.n) break; raster_band(i); } }
static void game_render(float rdt) {
  PROF(C_PREP);
  float k = 1 - mexp(-1.5f * rdt);
  const C3 *src = &palTarget->north; C3 *dst = &palCur.north;
  for (int i = 0; i < PALN; i++) dst[i] = clerp(dst[i], src[i], k);
  palCur.name = palTarget->name;
  cam_build();
  S.lisX = Cam.x; S.lisW = Cam.viewW / Cam.zoom;
  snd_levels();
  float bass = S.lv.kick, sparkle = clampf(S.lv.high * 4, 0, 1), now = (float)plat_time();
  int inGame = (Game.state != GS_TITLE && Game.state != GS_BOOT) || Game.demo;
  Bdark.n = Bworld.n = Bfloor.n = 0;
  PROFE(C_PREP);
  PROF(C_BG); draw_background(&palCur, now, Game.beatPulse * 0.12f + bass * 0.5f, Game.danger); PROFE(C_BG);
  PROF(C_PREP);
  grid_pack();
  RJ.gpulse = Game.beatPulse * (inGame ? 0.22f : 0.3f) + bass * 0.45f + sparkle * 0.15f;
  C3 bc = palCur.border; float bk = 1.2f + Game.beatPulse * 0.8f + bass * 0.6f;
  Bfloor.z = 0;
  float B4[4][4] = { { 0, 0, ARENA_W, 0 }, { ARENA_W, 0, ARENA_W, ARENA_H }, { ARENA_W, ARENA_H, 0, ARENA_H }, { 0, ARENA_H, 0, 0 } };
  for (int i = 0; i < 4; i++) blineh(&Bfloor, B4[i][0], B4[i][1], B4[i][2], B4[i][3], 2.4f, cmul(bc, bk), 5);
  Bfloor.z = LIFT; Bworld.z = LIFT; Bdark.z = LIFT;
  if (inGame) {
    for (int i = 0; i < Game.nen; i++) { Enemy *e = &Game.en[i]; if (e->type == ET_VORTEX && e->spawn <= 0) bdoth(&Bdark, e->x, e->y, e->r * 0.9f, c3(0.6f, 0.6f, 0.6f), e->r * 0.5f); }
    boss_drawDark(&Bdark);
    game_drawWorld(&Bworld, &Bfloor);
  }
  fx_drawWorld(&Bworld);
  RJ.refl = 0.16f * clampf((Cam.tilt - 0.12f) / 0.5f, 0, 1);
  WellU wu[8]; int nw = Game.nwells < 8 ? Game.nwells : 8;
  for (int i = 0; i < nw; i++) { wu[i].x = Game.wells[i].x; wu[i].y = Game.wells[i].y; wu[i].pr = Game.wells[i].pr; wu[i].pull = Game.wells[i].pull; wu[i].r = Game.wells[i].r; }
  { float pdt = minf(Game.simDt, 0.05f); Field.B = 46; parts_update(pdt, now, bass, wu, inGame ? nw : 0); }
  Game.simDt = 0;
  RJ.bass = bass; RJ.sparkle = sparkle; RJ.now = now;
  draw_grid_project(Cam.vp);
  PROFE(C_PREP); PROF(C_RASTER);
  { int n = nWork; if (n > NWMAX) n = NWMAX; if (n > RH / 48) n = RH / 48; if (n < 1) n = 1;
    int nb = n > 1 ? n * 4 : 1; if (nb > RH / 24) nb = RH / 24; if (nb < n) nb = n;
    RJ.n = nb; rjNext = 0; if (n > 1) plat_par(n, raster_job); else raster_band(0); }
  PROFE(C_RASTER);
  /* composite */
  float flash = Set.flash ? Game.flash : Game.flash * 0.5f;
  float gain = menuDim > 0 ? lerpf(1, 0.42f, menuDim) : 1; C3 tint = cmul(c3(0.03f, 0.005f, 0.05f), menuDim);
  lut_build(gain, mfloor(flash * 40) / 40, Game.flashCol, tint);
  PROF(C_BLOOM); bloom_build(Set.bloom / 7.f * 1.2f * frameBloom); PROFE(C_BLOOM);
  Wave wv[6]; int nwv = cam_waves(wv); waves_build(wv, nwv);
  float aberr = (Set.flash ? Game.aberr : Game.aberr * 0.3f) + 0.14f + 0.6f * Game.bt;
  float sat = (Game.state == GS_OVER || (!Game.p.alive && inGame && !Game.demo)) ? 0.45f : 1.08f - 0.35f * Game.bt;
  float vhs = Set.vhs ? (Game.state == GS_PLAY && !Game.paused ? 0.5f : 1) : 0;
  PROF(C_COMP); composite(aberr, sat > 1 ? 1 : sat, Set.crt, vhs, now); PROFE(C_COMP);
}
/* =========================================================================
   UI — retro-OS windows and a cabinet menu, drawn in-engine with the vector
   font. Unified keyboard / mouse / gamepad focus.
   ========================================================================= */
static struct { int up, down, left, right, ok, back, digit, reroll, tap, pause, any, click, moved; float wheel; } Nav;
enum { IK_BTN, IK_OPT, IK_CARD, IK_META, IK_LEVEL };
enum { A_PLAY = 1, A_LEVELS, A_BONUS, A_HOW, A_SETTINGS, A_RECORDS, A_BACK, A_REROLL, A_RESUME, A_QUIT, A_RETRY, A_TITLE };
typedef struct { float x, y, w, h; u8 kind; int act, data; float deny; } Item;
static struct { int cur; Item it[24]; int n, focus, prev[8], np; double shownAt; float taps[8]; int ntaps; char note[120]; Record orec; int orank; i64 obanked; int owon; float t; float px, py, pw, ph; } U;
#define LW 640.f   /* menus are laid out on a 640×480 canvas, centred in UI space */
#define LH 480.f
static const C3 UI_INK = { 1.3f, 1.22f, 1.35f }, UI_DIM = { 0.75f, 0.6f, 0.95f }, UI_PINK = { 1.4f, 0.4f, 1.15f }, UI_CYAN = { 0.4f, 1.3f, 1.4f }, UI_SUN = { 1.4f, 1.15f, 0.45f }, UI_RED = { 1.5f, 0.35f, 0.45f };
static const C3 D_PINK = { 1.0f, 0.31f, 0.85f }, D_VIOLET = { 0.54f, 0.36f, 1.0f }, D_CYAN = { 0.3f, 0.95f, 1.0f };
static void perf_apply(void);
/* first-start benchmark state (see BENCHMARK in the main section) */
static struct { int on; double t0, tm; int frames; float acc, fps, shown, prog; int q; } Bench;
static void ui_hide(void) { U.cur = SC_NONE; U.n = 0; S.metro = 0; }
static int ui_current(void) { return U.cur; }
static float TX(const char *s, float x, float y, float size, C3 col, int align, int italic, float sp) { FOpt o = fo(align, sp, 1, italic, NULL); return font_draw(&Bhud, s, x, y, size, col, &o); }
static int wrap(const char *s, float size, float maxW, char out[][128], int maxL) {
  int n = 0; char line[128] = ""; int ll = 0;
  while (*s && n < maxL) {
    char word[64]; int wl = 0; while (*s == ' ') s++; while (*s && *s != ' ' && wl < 63) word[wl++] = *s++; word[wl] = 0; if (!wl) break;
    char test[192]; s_cpy(s_cpy(s_cpy(test, line), ll ? " " : ""), word);
    if (ll && font_width(test, size, 1) > maxW) { s_cpy(out[n++], line); s_cpy(line, word); ll = wl; }
    else { s_cpy(line, test); ll = s_len(line); }
  }
  if (ll && n < maxL) s_cpy(out[n++], line);
  return n;
}
static Item *add_item(int kind, int act, int data, float x, float y, float w, float h) { if (U.n >= ARR(U.it)) return &U.it[0]; Item *i = &U.it[U.n++]; i->kind = (u8)kind; i->act = act; i->data = data; i->x = x; i->y = y; i->w = w; i->h = h; i->deny = 0; return i; }
static void ui_window(float x, float y, float w, float h, const char *title) {
  ov_rect(x, y, w, h, c3(0.07f, 0.02f, 0.14f), 2);
  ov_grad(x + 2, y + 2, w - 4, 18, D_PINK, D_VIOLET, D_CYAN, 1);
  bline(&Bhud, x, y, x + w, y, 0.6f, c3(1.3f, 0.95f, 1.25f)); bline(&Bhud, x, y, x, y + h, 0.6f, c3(1.3f, 0.95f, 1.25f));
  bline(&Bhud, x, y + h, x + w, y + h, 0.6f, c3(0.35f, 0.15f, 0.55f)); bline(&Bhud, x + w, y, x + w, y + h, 0.6f, c3(0.35f, 0.15f, 0.55f));
  TX(title, x + 8, y + 7, 8, c3(1.6f, 1.6f, 1.7f), AL_LEFT, 0, 1.1f);
  for (int i = 0; i < 2; i++) { float bx = x + w - 34 + i * 15; ov_rect(bx, y + 5, 12, 11, c3(0.91f, 0.84f, 1.0f), 0); }
}

/* ---------------- settings ---------------- */
enum { SR_RANGE, SR_OFFSET, SR_BOOL, SR_QUALITY, SR_FPS, SR_RES };
typedef struct { const char *label; int type; } SetRow;
#define NSET 14
static const SetRow SET_ROWS[NSET] = {
  { "MASTER VOLUME", SR_RANGE }, { "MUSIC", SR_RANGE }, { "EFFECTS", SR_RANGE }, { "AUDIO OFFSET", SR_OFFSET }, { "SCREEN SHAKE", SR_RANGE }, { "BLOOM", SR_RANGE },
  { "CRT SCANLINES", SR_BOOL }, { "VHS TRACKING", SR_BOOL }, { "FLASH EFFECTS", SR_BOOL }, { "QUALITY", SR_QUALITY }, { "RESOLUTION", SR_RES }, { "FRAME RATE", SR_FPS },
  { "FULLSCREEN", SR_BOOL }, { "SHOW FPS", SR_BOOL }
};
static int *set_int(int r) { switch (r) { case 0: return &Set.master; case 1: return &Set.music; case 2: return &Set.sfx; case 3: return &Set.offset; case 4: return &Set.shake; default: return &Set.bloom; } }
static u8 *set_u8(int r) { switch (r) { case 6: return &Set.crt; case 7: return &Set.vhs; case 8: return &Set.flash; case 9: return &Set.quality; case 10: return &Set.res; case 11: return &Set.rate; case 12: return &Set.fullscreen; default: return &Set.fps; } }
static void setting_changed(int r) {
  save_write();
  if (r <= 2) snd_applyVolumes();
  if (r == 9) perf_apply();
  if (r == 10 || r == 12) plat_video_apply();
  if (r == 11) perf_apply();
  sfx_ui(UI_MOVE);
}
static void setting_adjust(int r, int d) {
  int t = SET_ROWS[r].type;
  if (t == SR_RANGE) { int *v = set_int(r); *v = clampi(*v + d, 0, 10); }
  else if (t == SR_OFFSET) { int *v = set_int(r); *v = clampi(*v + d * 10, -100, 250); }
  else if (t == SR_BOOL) { u8 *v = set_u8(r); *v = !*v; }
  else if (t == SR_FPS) {   /* 30 → 60 → 120 → 240 → 1000 → unlimited */
    static const u8 ORD[NRATE] = { 1, 0, 2, 3, 4, 5 }; int k = 0; while (k < NRATE - 1 && ORD[k] != Set.rate) k++;
    Set.rate = ORD[(k + d + NRATE) % NRATE];
  }
  else if (t == SR_RES) Set.res = (u8)((Set.res + d + NRES) % NRES);
  else { u8 *v = set_u8(r); *v = (u8)((*v + d + 5) % 5); }
  setting_changed(r);
}
/** Tap calibration: each tap's distance from the raw (uncorrected) beat; the median becomes the offset. */
static void ui_tap(void) {
  double rb = rawVisBeat(); float err = (float)((rb - (double)(i64)(rb + (rb >= 0 ? 0.5 : -0.5))) * spb() * 1000);
  if (mfabs(err) > 320) return;
  if (U.ntaps >= 8) { memmove(U.taps, U.taps + 1, sizeof(float) * 7); U.ntaps = 7; }
  U.taps[U.ntaps++] = err;
  if (U.ntaps < 4) { s_printf(U.note, "Tap %d of 4...", U.ntaps); return; }
  float s[8]; memcpy(s, U.taps, sizeof s); for (int i = 1; i < U.ntaps; i++) for (int j = i; j > 0 && s[j] < s[j - 1]; j--) { float t = s[j]; s[j] = s[j - 1]; s[j - 1] = t; }
  int v = clampi(mlrint(s[U.ntaps >> 1] / 10) * 10, -100, 250);
  Set.offset = v; setting_changed(3);
  s_printf(U.note, "Offset set to %+ ms from %d taps. Keep tapping to refine.", v, U.ntaps);
}

/* ---------------- screen builds ---------------- */
static void build_title(void) { static const int A[6] = { A_PLAY, A_LEVELS, A_BONUS, A_HOW, A_SETTINGS, A_RECORDS }; for (int i = 0; i < 6; i++) add_item(IK_BTN, A[i], 0, 190, 236 + i * 27.f, 260, 24); }
static void build_window(float w, float h) { U.pw = w; U.ph = h; U.px = (LW - w) / 2; U.py = (LH - h) / 2; }
static void build_settings(void) { build_window(540, 424); for (int r = 0; r < NSET; r++) add_item(IK_OPT, 0, r, U.px + 14, U.py + 30 + r * 23.f, U.pw - 28, 21); add_item(IK_BTN, A_BACK, 0, LW / 2 - 60.f, U.py + U.ph - 32, 120, 22); s_cpy(U.note, "Line the pink pip up with the click you hear. Or tap T, or click the pip, on each click."); U.ntaps = 0; }
static void build_bonus(void) { build_window(560, 420); for (int i = 0; i < 7; i++) add_item(IK_META, 0, i, U.px + 14, U.py + 50 + i * 46.f, U.pw - 28, 42); add_item(IK_BTN, A_BACK, 0, LW / 2 - 60.f, U.py + U.ph - 32, 120, 22); }
static void build_levels(void) { build_window(560, 400); for (int i = 0; i < 6; i++) add_item(IK_LEVEL, 0, i, U.px + 14, U.py + 50 + i * 49.f, U.pw - 28, 44); add_item(IK_BTN, A_BACK, 0, LW / 2 - 60.f, U.py + U.ph - 32, 120, 22); }
static void build_how(void) { build_window(600, 456); add_item(IK_BTN, A_BACK, 0, LW / 2 - 60.f, U.py + U.ph - 30, 120, 22); }
static void build_records(void) { build_window(540, 380); add_item(IK_BTN, A_BACK, 0, LW / 2 - 60.f, U.py + U.ph - 32, 120, 22); }
static void build_upgrade(void) { for (int i = 0; i < 3; i++) add_item(IK_CARD, 0, i, 32 + i * 196.f, 118, 184, 250); add_item(IK_BTN, A_REROLL, 0, LW / 2 - 70.f, 384, 140, 22); }
static void build_pause(void) { static const int A[3] = { A_RESUME, A_SETTINGS, A_QUIT }; for (int i = 0; i < 3; i++) add_item(IK_BTN, A[i], 0, 200, 250 + i * 30.f, 240, 24); }
static void build_over(void) { add_item(IK_BTN, A_RETRY, 0, 180, 418, 130, 24); add_item(IK_BTN, A_TITLE, 0, 330, 418, 130, 24); }
static void ui_show(int id) {
  U.cur = id; U.n = 0; U.focus = 0; U.shownAt = plat_time(); U.t = 0;
  switch (id) {
    case SC_TITLESCR: build_title(); break; case SC_SETTINGS: build_settings(); break; case SC_BONUS: build_bonus(); break; case SC_LEVELS: build_levels(); break;
    case SC_HOW: build_how(); break; case SC_RECORDS: build_records(); break; case SC_UPGRADE: build_upgrade(); break; case SC_PAUSE: build_pause(); break; case SC_OVER: build_over(); break;
  }
  S.metro = id == SC_SETTINGS;
  mzero(&Nav, sizeof Nav);
}
static void ui_showUpgrades(const int *list, int wave, int afterBoss, int reroll) { (void)wave; (void)afterBoss; if (list != upList) memcpy(upList, list, sizeof upList); if (reroll) { U.shownAt = plat_time(); return; } ui_show(SC_UPGRADE); }
static void ui_showGameOver(const Record *rec, int rank, i64 banked, int won) { U.orec = *rec; U.orank = rank; U.obanked = banked; U.owon = won; ui_show(SC_OVER); }
static void ui_setFocus(int i, int sound) { if (!U.n) return; int n = ((i % U.n) + U.n) % U.n; if (n != U.focus && sound) sfx_ui(UI_MOVE); U.focus = n; }
static void ui_toTitle(void) {
  if (Game.state == GS_PLAY || Game.state == GS_UPGRADE) game_bankRun();
  ui_hide(); Game.state = GS_TITLE; Game.paused = 0; Game.hasLastTick = 0;
  Game.nen = Game.nbu = Game.neb = Game.nsh = Game.npk = 0; Boss.on = 0; Game.nwells = 0; mzero(Game.upg, sizeof Game.upg);
  game_syncLayers(); palTarget = &PALETTES[0]; fx_clear(); game_startDemo();
  S.warpOn = 0;
  Score s; mzero(&s, sizeof s); s.has = SC_STYLE | SC_LEVEL | SC_BPM | SC_STEPS | SC_ROOT | SC_MODE | SC_PROG | SC_RESET; s.style = ST_TITLE; s.level = 1; s.bpm = 100; s.steps = 4; s.root = 53; s.mode = MD_LYDIAN; s.prog[1] = 3; s.prog[2] = 5; s.prog[3] = 4; setScore(&s);
  snd_setMuffle(0, 0.3f); snd_setHum(0);
  U.np = 0; ui_show(SC_TITLESCR);
}
static void ui_resume(void) { ui_hide(); Game.paused = 0; Game.hasLastTick = 0; snd_setMuffle(0, 0.25f); sfx_ui(UI_BACK); }
static void ui_pause(void) {
  if (Game.state != GS_PLAY || Game.paused) return;
  Game.paused = 1; snd_setMuffle(0.62f, 0.25f); sfx_ui(UI_BACK);
  U.np = 0; ui_show(SC_PAUSE);
}
static void ui_back(void) {
  if (U.cur == SC_HOW || U.cur == SC_SETTINGS || U.cur == SC_RECORDS || U.cur == SC_BONUS || U.cur == SC_LEVELS) { sfx_ui(UI_BACK); ui_show(U.np ? U.prev[--U.np] : SC_TITLESCR); }
  else if (U.cur == SC_PAUSE) ui_resume();
}
static void ui_activate(Item *it, float mx, int byMouse) {
  if (!it) return;
  if (it->kind == IK_CARD) {
    if (plat_time() - U.shownAt < 0.45) return;
    int u = upList[it->data]; ui_hide(); game_pickUpgrade(u); return;
  }
  if (it->kind == IK_META) {
    int ok = it->data == 6 ? meta_refund() > 0 : meta_buy(it->data);
    if (!ok) { sfx_ui(UI_BACK); it->deny = 0.35f; } else sfx_ui(UI_OK);
    return;
  }
  if (it->kind == IK_LEVEL) {
    int key = it->data == 5 ? 0 : LEVELS[it->data].wave;
    if (level_unlocked(key)) { sfx_ui(UI_START); ui_hide(); if (key == 0) game_newRun(5, 1); else game_newRun(key, 0); return; }
    if (!level_buy(key)) { sfx_ui(UI_BACK); it->deny = 0.35f; return; }
    sfx_ui(UI_OK); return;
  }
  if (it->kind == IK_OPT) {
    int r = it->data;
    if (byMouse) {
      float vx = it->x + it->w - 210;
      if (SET_ROWS[r].type == SR_OFFSET && mx < vx + 20) { ui_tap(); return; }
      if (SET_ROWS[r].type == SR_RANGE && mx > vx + 40 && mx < vx + 170) { *set_int(r) = clampi(mlrint((mx - vx - 45) / 120 * 10), 0, 10); setting_changed(r); return; }
      setting_adjust(r, mx < it->x + it->w - 105 ? -1 : 1); return;
    }
    if (SET_ROWS[r].type != SR_OFFSET) setting_adjust(r, 1);
    return;
  }
  switch (it->act) {
    case A_PLAY: case A_RETRY: sfx_ui(UI_START); ui_hide(); game_newRun(1, 0); break;
    case A_HOW: case A_SETTINGS: case A_RECORDS: case A_BONUS: case A_LEVELS:
      sfx_ui(UI_OK); if (U.np < 8) U.prev[U.np++] = U.cur;
      ui_show(it->act == A_HOW ? SC_HOW : it->act == A_SETTINGS ? SC_SETTINGS : it->act == A_RECORDS ? SC_RECORDS : it->act == A_BONUS ? SC_BONUS : SC_LEVELS); break;
    case A_BACK: ui_back(); break;
    case A_REROLL:
      if (Game.state != GS_UPGRADE || Game.rerolls <= 0) { sfx_ui(UI_BACK); it->deny = 0.35f; break; }
      Game.rerolls--; sfx_ui(UI_MOVE); sfx_ui(UI_OK);
      rollUpgrades(Game.draftAfterBoss, upList); ui_showUpgrades(upList, Game.wave, Game.draftAfterBoss, 1); break;
    case A_RESUME: ui_resume(); break;
    case A_QUIT: case A_TITLE: sfx_ui(UI_BACK); ui_toTitle(); break;
  }
}
static void ui_update(float rdt, float mx, float my) {
  if (!U.cur) return;
  U.t += rdt;
  for (int i = 0; i < U.n; i++) U.it[i].deny = maxf(0, U.it[i].deny - rdt);
  if (U.cur == SC_SETTINGS && Nav.tap) ui_tap();
  if (U.cur == SC_PAUSE && (Nav.back || Nav.pause)) { ui_resume(); return; }
  if (U.cur == SC_BOOT) return;
  int grid = U.cur == SC_UPGRADE || U.cur == SC_OVER;
  if (Nav.up || (grid && Nav.left)) ui_setFocus(U.focus - 1, 1);
  if (Nav.down || (grid && Nav.right)) ui_setFocus(U.focus + 1, 1);
  if (!grid && (Nav.left || Nav.right)) { Item *it = &U.it[U.focus]; if (it->kind == IK_OPT) setting_adjust(it->data, Nav.left ? -1 : 1); }
  /* mouse: hover focuses, click activates */
  int hover = -1; for (int i = 0; i < U.n; i++) { Item *it = &U.it[i]; if (mx >= it->x && mx < it->x + it->w && my >= it->y && my < it->y + it->h) hover = i; }
  if (Nav.moved && hover >= 0) ui_setFocus(hover, 1);
  if (Nav.click && hover >= 0) { U.focus = hover; ui_activate(&U.it[hover], mx, 1); return; }
  if (Nav.click && U.cur == SC_SETTINGS && mx < U.px + 40 && my > U.py + 30 + 3 * 23 && my < U.py + 30 + 4 * 23) { ui_tap(); return; }
  if (Nav.ok) { ui_activate(&U.it[U.focus], 0, 0); return; }
  if (Nav.back) ui_back();
  if (U.cur == SC_UPGRADE && Nav.digit && Nav.digit <= 3) { U.focus = Nav.digit - 1; ui_activate(&U.it[U.focus], 0, 0); return; }
  if (U.cur == SC_UPGRADE && Nav.reroll) { for (int i = 0; i < U.n; i++) if (U.it[i].act == A_REROLL) ui_activate(&U.it[i], 0, 0); }
}

/* ---------------- drawing ---------------- */
static void draw_button(Item *it, const char *label, int focus, float size) {
  float cx = it->x + it->w / 2, cy = it->y + it->h / 2, sh = it->deny > 0 ? msin(it->deny * 60) * 4 * it->deny / 0.35f : 0;
  if (focus) {
    ov_grad(it->x + sh, it->y + 2, it->w, it->h - 4, c3(0, 0, 0), D_PINK, c3(0, 0, 0), 0.38f);
    ov_grad(it->x + sh, it->y + 2, it->w, it->h - 4, c3(0, 0, 0), D_VIOLET, D_CYAN, 0.15f);
    float lw = 26; bline(&Bhud, it->x - 4 - lw + sh, cy, it->x - 4 + sh, cy, 0.5f, UI_PINK); bline(&Bhud, it->x + it->w + 4 + sh, cy, it->x + it->w + 4 + lw + sh, cy, 0.5f, UI_CYAN);
  }
  C3 c = focus ? c3(1.7f, 1.6f, 1.75f) : UI_INK;
  TX(label, cx + sh, cy - size / 2, size, c, AL_CENTER, 1, 1.28f);
}
static void draw_chips(float y) {
  char line[256] = ""; float x0 = 40, x = x0, size = 7.5f;
  for (int i = 0; i < U_N; i++) {
    if (!Game.upg[i]) continue;
    char chip[48]; s_printf(chip, "%s%s %s", upg_music(i, 1) ? "\x0E " : "", UPG[i].name, roman(Game.upg[i]));
    float w = font_width(chip, size, 1) + 14;
    if (x + w > LW - x0) { x = x0; y += 16; }
    C3 c = upg_music(i, 1) ? UI_CYAN : UI_DIM;
    bline(&Bhud, x, y - 3, x + w - 4, y - 3, 0.4f, cmul(c, 0.5f)); bline(&Bhud, x, y + size + 3, x + w - 4, y + size + 3, 0.4f, cmul(c, 0.5f));
    bline(&Bhud, x, y - 3, x, y + size + 3, 0.4f, cmul(c, 0.5f)); bline(&Bhud, x + w - 4, y - 3, x + w - 4, y + size + 3, 0.4f, cmul(c, 0.5f));
    TX(chip, x + 5, y, size, c, AL_LEFT, 0, 1); x += w + 4;
  }
  (void)line;
}
static void draw_h2(const char *s, float y, float size) {
  C3 vg[2] = { c3(1.0f, 1.5f, 1.6f), c3(1.5f, 0.4f, 1.2f) }; FOpt o = fo(AL_CENTER, 1.06f, 1.2f, 0, vg);
  font_draw(&Bhud, s, LW / 2, y, size, c3(1, 1, 1), &o);
}
static void ui_draw(void) {
  char buf[160];
  if (!U.cur) return;
  int dim = U.cur != SC_BOOT && U.cur != SC_TITLESCR;
  if (dim) ov_dim(0.42f, c3(0.03f, 0.005f, 0.05f));
  Bhud.n = 0; ovX = UOX; ovY = UOY;
  if (U.cur == SC_BOOT || U.cur == SC_TITLESCR) {
    float ls = U.cur == SC_BOOT ? 88 : 72;
    hud_logo(&Bhud, LW / 2, U.cur == SC_BOOT ? 120 : 64, ls);
    if (U.cur == SC_BOOT) {
      if (Bench.on) {
        TX("TUNING FOR THIS MACHINE", LW / 2, 300, 12, c3(1.6f, 1.5f, 1.7f), AL_CENTER, 1, 1.3f);
        ov_rect(LW / 2 - 120, 322, 240, 4, c3(0.12f, 0.05f, 0.2f), 0); ov_grad(LW / 2 - 120, 322, 240 * Bench.prog, 4, D_PINK, D_VIOLET, D_CYAN, 1);
      } else {
        if ((int)mfloor(U.t * 1.8f) % 2 == 0) TX("PRESS START", LW / 2, 300, 18, c3(1.6f, 1.5f, 1.7f), AL_CENTER, 1, 1.4f);
        if (Bench.shown > 0) { static const char *Q[5] = { "AUTO", "HIGH", "MEDIUM", "LOW", "LOWEST" }; s_printf(buf, "QUALITY SET TO %s \xB7 %d FPS MEASURED", Q[Bench.q], mlrint(Bench.fps)); TX(buf, LW / 2, 360, 7, UI_CYAN, AL_CENTER, 0, 1.1f); }
      }
      if (!Bench.on) TX("ANY KEY \xB7 CLICK \xB7 BUTTON  \xB7  HEADPHONES RECOMMENDED", LW / 2, 334, 7, UI_DIM, AL_CENTER, 0, 1.1f);
    } else {
      TX("A RHYTHM SHOOTER", LW / 2, 200, 10, UI_CYAN, AL_CENTER, 0, 1.6f);
      static const char *L[6] = { "PLAY", "LEVEL SELECT", "BONUS", "HOW TO PLAY", "SETTINGS", "RECORDS" };
      for (int i = 0; i < U.n; i++) draw_button(&U.it[i], L[i], i == U.focus, 13);
      i64 bank = meta_bank();
      if (Save.nrec) s_printf(buf, "XP %D \xB7 BEST %D \xB7 SECTOR %d", bank, Save.rec[0].score, Save.rec[0].wave);
      else if (bank) s_printf(buf, "XP %D", bank); else s_cpy(buf, "KEYBOARD + MOUSE \xB7 GAMEPAD");
      TX(buf, 16 - UOX, UH - UOY - 20, 7, UI_DIM, AL_LEFT, 0, 1.1f);
      if ((int)mfloor(U.t * 1.8f) % 2 == 0) TX("CREDIT 01", LW + UOX - 16, UH - UOY - 20, 7, UI_SUN, AL_RIGHT, 0, 1.1f);
    }
  } else if (U.cur == SC_SETTINGS) {
    ui_window(U.px, U.py, U.pw, U.ph, "SETTINGS.EXE");
    for (int r = 0; r < NSET; r++) {
      Item *it = &U.it[r]; int f = U.focus == r; float vx = it->x + it->w - 210, cy = it->y + it->h / 2;
      if (f) { ov_grad(it->x, it->y, it->w, it->h, D_PINK, c3(0.2f, 0.3f, 0.5f), D_CYAN, 0.16f); bline(&Bhud, it->x, it->y, it->x + it->w, it->y, 0.4f, cmul(UI_CYAN, 0.6f)); bline(&Bhud, it->x, it->y + it->h, it->x + it->w, it->y + it->h, 0.4f, cmul(UI_CYAN, 0.6f)); }
      TX(SET_ROWS[r].label, it->x + 10, cy - 4.5f, 9, f ? c3(1.6f, 1.55f, 1.7f) : UI_INK, AL_LEFT, 0, 1.15f);
      TX("\x11", vx + 30, cy - 3.5f, 7, cmul(UI_INK, 0.6f), AL_CENTER, 0, 1); TX("\x10", vx + 196, cy - 3.5f, 7, cmul(UI_INK, 0.6f), AL_CENTER, 0, 1);
      int t = SET_ROWS[r].type;
      if (t == SR_RANGE) { int v = *set_int(r); for (int k = 0; k < 10; k++) { float bx = vx + 48 + k * 12; ov_rect(bx, cy - 5, 8, 10, k < v ? c3(0.3f + k * 0.07f, 0.8f - k * 0.05f, 1.0f) : c3(0.08f, 0.2f, 0.25f), 0); } }
      else {
        if (t == SR_OFFSET) { s_printf(buf, "%+ MS", *set_int(r)); float f2 = (float)(Game.visBeat - mfloor((float)Game.visBeat)); C3 pc = f2 < 0.12f ? c3(1.8f, 0.5f, 1.5f) : c3(0.5f, 0.15f, 0.45f); bcircle(&Bhud, vx + 8, cy, 5, 16, 0.6f, UI_PINK, 0); if (f2 < 0.12f) bdot(&Bhud, vx + 8, cy, 4, pc); }
        else if (t == SR_BOOL) s_cpy(buf, *set_u8(r) ? "ON" : "OFF");
        else if (t == SR_FPS) { if (rate_hz(Set.rate)) s_printf(buf, "%d FPS", rate_hz(Set.rate)); else s_cpy(buf, "UNLIMITED"); }
        else if (t == SR_RES) { int w, h; plat_res_info(Set.res, &w, &h); if (Set.res <= RES_DESKTOP && w) s_printf(buf, "%s %dX%d", RESOLUTIONS[Set.res].name, w, h); else s_cpy(buf, RESOLUTIONS[Set.res].name); }
        else { static const char *Q[5] = { "AUTO", "HIGH", "MEDIUM", "LOW", "LOWEST" }; s_cpy(buf, Q[Set.quality]); }
        TX(buf, vx + 113, cy - 4.5f, 9, UI_CYAN, AL_CENTER, 0, 1.1f);
      }
    }
    TX(U.note, LW / 2, U.py + U.ph - 54, 6.5f, UI_DIM, AL_CENTER, 0, 1);
    draw_button(&U.it[NSET], "BACK", U.focus == NSET, 11);
  } else if (U.cur == SC_BONUS || U.cur == SC_LEVELS) {
    int bonus = U.cur == SC_BONUS; i64 bank = meta_bank();
    ui_window(U.px, U.py, U.pw, U.ph, bonus ? "BONUS.EXE" : "LEVELS.EXE");
    s_printf(buf, "%D", bank); TX("XP", U.px + 20, U.py + 30, 9, UI_DIM, AL_LEFT, 0, 1.3f); TX(buf, U.px + U.pw - 20, U.py + 28, 12, UI_SUN, AL_RIGHT, 0, 1.1f);
    int n = bonus ? 7 : 6;
    for (int i = 0; i < n; i++) {
      Item *it = &U.it[i]; int f = U.focus == i; float sh = it->deny > 0 ? msin(it->deny * 60) * 4 * it->deny / 0.35f : 0, x = it->x + sh;
      const char *icon, *name; char desc[120], cost[40]; int maxed = 0, poor = 0, pips = -1, lvl = 0, maxl = 0;
      if (bonus) {
        if (i < 6) { lvl = Save.meta[i]; maxl = META[i].max; maxed = lvl >= maxl; i64 c = maxed ? 0 : metaCost(i, lvl); poor = !maxed && bank < c; icon = META[i].icon; name = META[i].name;
          char d[96]; meta_desc(i, maxed ? lvl : lvl + 1, d); s_printf(desc, "%s%s", maxed ? "" : lvl ? "Next: " : "", d); if (maxed) s_cpy(cost, "MAX"); else s_fmt(cost, c); pips = 1; }
        else { i64 sp = meta_spent(); maxed = !sp; icon = REFUND_ICON; name = "REFUND ALL"; s_cpy(desc, "Return every bonus to the bank and re-spend it."); if (sp) { cost[0] = '+'; s_fmt(cost + 1, sp); } else s_cpy(cost, "-"); }
      } else {
        int key = i == 5 ? 0 : LEVELS[i].wave; int open = level_unlocked(key); i64 c = i == 5 ? RUSH_COST : LEVELS[i].cost; poor = !open && bank < c;
        icon = open ? (i == 5 ? "M12 2l3 6 6 1-4.5 4.5L18 20l-6-3-6 3 1.5-6.5L3 9l6-1z" : "M7 4l13 8-13 8z") : "M6 11V8a6 6 0 0 1 12 0v3M5 11h14v10H5z";
        static char nm[48]; name = nm;
        if (i == 5) { s_cpy(nm, "BOSS RUSH"); s_cpy(desc, "All five guardians back to back. A draft after each, plus 4 random augments."); }
        else if (key == 1) { s_cpy(nm, "SECTOR 01 \xB7 FROM THE TOP"); s_printf(desc, "The full %d-sector campaign.", BOSS_LAST); }
        else { s_printf(nm, "SECTOR %2 \xB7 %s", key, BNAMES[boss_kindFor(key)]); s_printf(desc, "Open on the guardian, with %d random augments.", key - 1); }
        if (open) s_cpy(cost, "START"); else { s_fmt(cost, c); s_cpy(cost + s_len(cost), " XP"); }
        maxed = open;
      }
      if (f) ov_grad(x, it->y, it->w, it->h, D_PINK, c3(0.2f, 0.3f, 0.5f), D_CYAN, 0.16f);
      C3 ic = maxed && bonus ? UI_SUN : poor ? cmul(UI_DIM, 0.6f) : UI_PINK;
      svg_draw(&Bhud, icon, x + 8, it->y + 9, 24, 0.55f, ic);
      TX(name, x + 42, it->y + 9, 10, poor ? cmul(UI_INK, 0.6f) : f ? c3(1.6f, 1.55f, 1.7f) : UI_INK, AL_LEFT, 0, 1.1f);
      char wl[2][128]; int nl = wrap(desc, 7, it->w - 200, wl, 2); for (int k = 0; k < nl; k++) TX(wl[k], x + 42, it->y + 24 + k * 9.5f, 7, UI_DIM, AL_LEFT, 0, 1);
      TX(cost, x + it->w - 12, it->y + (pips > 0 ? 24 : 16), 9, maxed ? UI_SUN : poor ? cmul(UI_RED, 0.8f) : UI_CYAN, AL_RIGHT, 0, 1.1f);
      if (pips > 0) for (int k = 0; k < maxl; k++) { float px = x + it->w - 14 - (maxl - 1 - k) * 11; ov_rect(px - 4, it->y + 9, 8, 7, k < lvl ? D_PINK : c3(0.15f, 0.07f, 0.2f), 0); }
    }
    draw_button(&U.it[n], "BACK", U.focus == n, 11);
  } else if (U.cur == SC_HOW) {
    ui_window(U.px, U.py, U.pw, U.ph, "HOW_TO_PLAY.TXT");
    draw_h2("HOW TO PLAY", U.py + 28, 18);
    static const char *K[] = { "MOVE", "AIM + FIRE", "DASH", "BULLET TIME", "BOMB", "PAUSE", "", "THE BEAT", "ENERGY", "ON THE BEAT", "HITBOX", "COMBO", "THE ARENA", "XP", "AUGMENTS", "LATENCY" };
    static const char *D[] = { "WASD / Arrows \xB7 Left stick", "Mouse (always firing) \xB7 Right stick", "Left-click / Space \xB7 Button 1 / 6", "Hold right-click / Shift \xB7 Hold button 3", "Q / E / Middle-click \xB7 Button 2 / 5", "Esc / P \xB7 Start", "",
      "Guns fire on the grid, kills play notes, enemies strike in time. Guardians change the time signature, and the Eclipse bends the tempo itself. Sector 25 is the last.",
      "Dashes cost energy and bullet time drains it. It refills over time and with every kill. The bar sits under your bombs; one notch is one dash.",
      "Dash as the floor ring closes on your ship. PERFECT (40 ms) refunds more than it costs and fires a big shockwave; a Perfect kill adds +1 combo. GREAT (80 ms) and GOOD (125 ms) refund less with smaller shockwaves.",
      "Only the bright dot at the centre of your ship can be hit. Graze freely.",
      "Destroyed enemies drop sparks. Each raises your combo multiplier. Taking a hit halves it. Kills that land on the beat ring gold and hit harder in the mix.",
      "The floor is a shallow basin with a turning tide. Coasting ships drift and long shots curve.",
      "Every run's score becomes XP. Spend it on permanent bonuses, or unlock later start sectors and Boss Rush in Level Select.",
      "Clear a sector to choose one. Augments marked \x0E add a layer to the music.",
      "Wireless headphones and some sound cards lag. Set AUDIO OFFSET in Settings so perfect dashes feel right." };
    float y = U.py + 58, kx = U.px + 118, dx = U.px + 128, dw = U.pw - 145;
    for (int i = 0; i < ARR(K); i++) {
      if (!K[i][0]) { bline(&Bhud, U.px + 40, y + 2, U.px + U.pw - 40, y + 2, 0.4f, cmul(UI_PINK, 0.4f)); y += 8; continue; }
      char wl[5][128]; int nl = wrap(D[i], 6.5f, dw, wl, 5);
      TX(K[i], kx, y, 7, UI_CYAN, AL_RIGHT, 0, 1.15f);
      for (int k = 0; k < nl; k++) TX(wl[k], dx, y + k * 9, 6.5f, UI_INK, AL_LEFT, 0, 1);
      y += nl * 9 + 4;
    }
    draw_button(&U.it[0], "BACK", 1, 11);
  } else if (U.cur == SC_RECORDS) {
    ui_window(U.px, U.py, U.pw, U.ph, "RECORDS.DAT");
    draw_h2("HIGH SCORES", U.py + 30, 18);
    if (!Save.nrec) TX("No records yet. Go make some noise.", LW / 2, U.py + 150, 9, UI_DIM, AL_CENTER, 0, 1);
    else {
      float x[5] = { U.px + 30, U.px + 230, U.px + 330, U.px + 420, U.px + U.pw - 24 }; float y = U.py + 66;
      static const char *H[5] = { "#", "SCORE", "SECTOR", "MULT", "DATE" };
      for (int c = 0; c < 5; c++) TX(H[c], x[c], y, 7, UI_DIM, c ? AL_RIGHT : AL_LEFT, 0, 1.3f);
      for (int i = 0; i < Save.nrec; i++) {
        Record *r = &Save.rec[i]; float yy = y + 18 + i * 22; C3 c = i == U.orank && U.orank >= 0 && Game.state == GS_OVER ? UI_SUN : UI_INK;
        bline(&Bhud, U.px + 24, yy + 15, U.px + U.pw - 24, yy + 15, 0.3f, cmul(UI_PINK, 0.25f));
        s_int(buf, i + 1); TX(buf, x[0], yy, 9, UI_DIM, AL_LEFT, 0, 1);
        s_fmt(buf, r->score); TX(buf, x[1], yy, 9, c, AL_RIGHT, 0, 1.05f);
        s_int(buf, r->wave); TX(buf, x[2], yy, 9, c, AL_RIGHT, 0, 1);
        s_printf(buf, "x%d", r->mult); TX(buf, x[3], yy, 9, c, AL_RIGHT, 0, 1);
        s_printf(buf, "%d-%2-%2", (int)(r->date / 10000), (int)(r->date / 100 % 100), (int)(r->date % 100)); TX(buf, x[4], yy, 8, cmul(c, 0.8f), AL_RIGHT, 0, 1);
      }
    }
    draw_button(&U.it[0], "BACK", 1, 11);
  } else if (U.cur == SC_UPGRADE) {
    if (Game.draftAfterBoss) s_cpy(buf, "GUARDIAN DOWN"); else s_printf(buf, "SECTOR %2 CLEAR", Game.wave);
    draw_h2(buf, 40, 24);
    TX(Game.draftAfterBoss ? "Choose a guardian relic. Rare augments likely." : "Choose an augment. \x0E marks the ones that join the band.", LW / 2, 82, 8, UI_DIM, AL_CENTER, 0, 1.05f);
    float since = (float)(plat_time() - U.shownAt);
    for (int i = 0; i < 3; i++) {
      Item *it = &U.it[i]; int u = upList[i], lvl = u == U_REPAIR ? 0 : Game.upg[u], f = U.focus == i; C3 rc = RARITY[UPG[u].rar].col;
      float appear = clampf((since - 0.09f - i * 0.11f) / 0.25f, 0, 1); if (appear <= 0) continue;
      float y = it->y + (1 - easeOutCubic(appear)) * 30 - (f ? 6 : 0);
      ov_rect(it->x, y, it->w, it->h, c3(0.07f, 0.02f, 0.14f), 2);
      C3 bc = cmul(c3(rc.r * 1.3f, rc.g * 1.3f, rc.b * 1.3f), (f ? 1.3f : 0.8f) * appear);
      bline(&Bhud, it->x, y, it->x + it->w, y, f ? 0.8f : 0.5f, bc); bline(&Bhud, it->x, y + it->h, it->x + it->w, y + it->h, f ? 0.8f : 0.5f, bc);
      bline(&Bhud, it->x, y, it->x, y + it->h, f ? 0.8f : 0.5f, bc); bline(&Bhud, it->x + it->w, y, it->x + it->w, y + it->h, f ? 0.8f : 0.5f, bc);
      if (f) ov_grad(it->x + 1, y + 1, it->w - 2, 3, D_PINK, D_VIOLET, D_CYAN, 0.8f);
      s_int(buf, i + 1); TX(buf, it->x + 10, y + 10, 9, UI_DIM, AL_LEFT, 0, 1);
      TX(RARITY[UPG[u].rar].name, it->x + it->w - 10, y + 10, 7, cmul(rc, 1.3f), AL_RIGHT, 0, 1.3f);
      svg_draw(&Bhud, UPG[u].icon, it->x + it->w / 2 - 22, y + 30, 44, 0.8f, cmul(rc, 1.4f));
      if (lvl && u != U_REPAIR) s_printf(buf, "%s %s", UPG[u].name, roman(lvl + 1)); else s_cpy(buf, UPG[u].name);
      TX(buf, it->x + it->w / 2, y + 88, hud_fit(buf, 11, it->w - 16, 1.05f), f ? c3(1.7f, 1.65f, 1.8f) : UI_INK, AL_CENTER, 0, 1.05f);
      char d[160], wl[6][128]; upg_desc(u, lvl + 1, d); int nl = wrap(d, 7, it->w - 22, wl, 6);
      for (int k = 0; k < nl; k++) TX(wl[k], it->x + it->w / 2, y + 110 + k * 10, 7, UI_DIM, AL_CENTER, 0, 1);
      const char *mu = upg_music(u, lvl + 1);
      if (mu) { char m2[140], wl2[3][128]; s_printf(m2, "\x0E %s", mu); int n2 = wrap(m2, 6.5f, it->w - 22, wl2, 3); for (int k = 0; k < n2; k++) TX(wl2[k], it->x + it->w / 2, y + 116 + nl * 10 + k * 9, 6.5f, UI_CYAN, AL_CENTER, 0, 1); }
      if (UPG[u].max < 10) for (int k = 0; k < UPG[u].max; k++) { float px = it->x + it->w / 2 + (k - (UPG[u].max - 1) / 2.f) * 13; ov_rect(px - 4, y + it->h - 16, 8, 6, k < lvl ? D_PINK : k == lvl ? D_CYAN : c3(0.15f, 0.07f, 0.2f), 0); }
    }
    s_printf(buf, "REROLL \xB7 %d", Game.rerolls); draw_button(&U.it[3], buf, U.focus == 3, 11);
    TX("1 \xB7 2 \xB7 3 TO PICK, R TO REROLL", LW / 2, 420, 7, UI_DIM, AL_CENTER, 0, 1.1f);
  } else if (U.cur == SC_PAUSE) {
    draw_h2("PAUSED", 120, 30);
    draw_chips(185);
    static const char *L[3] = { "RESUME", "SETTINGS", "QUIT TO TITLE" };
    for (int i = 0; i < 3; i++) draw_button(&U.it[i], L[i], i == U.focus, 13);
  } else if (U.cur == SC_OVER) {
    draw_h2(U.owon ? "PHASE COMPLETE" : "SIGNAL LOST", 40, 30);
    s_fmt(buf, U.orec.score); { C3 vg[2] = { c3(1.4f, 1.4f, 1.5f), CHROME[1] }; FOpt o = fo(AL_CENTER, 1.05f, 1.2f, 1, vg); font_draw(&Bhud, buf, LW / 2, 92, 40, c3(1, 1, 1), &o); }
    if (U.orec.score) { if (U.orank == 0) TX("\x13 NEW RECORD \x13", LW / 2, 146, 11, UI_SUN, AL_CENTER, 0, 1.3f); else if (U.orank > 0) { s_printf(buf, "RANK #%d", U.orank + 1); TX(buf, LW / 2, 146, 11, UI_SUN, AL_CENTER, 0, 1.3f); } }
    if (U.obanked) { s_printf(buf, "+%D XP \xB7 %D XP TOTAL", U.obanked, meta_bank()); TX(buf, LW / 2, 166, 8, UI_CYAN, AL_CENTER, 0, 1.1f); }
    const char *SK[6] = { "SECTOR", "ENEMIES DESTROYED", "PEAK MULTIPLIER", "PERFECT DASHES", "GUARDIANS", "TIME" };
    for (int i = 0; i < 6; i++) {
      float y = 196 + i * 18; TX(SK[i], LW / 2 - 10, y, 8, UI_DIM, AL_RIGHT, 0, 1.15f);
      switch (i) { case 0: s_int(buf, U.orec.wave); break; case 1: s_fmt(buf, U.orec.kills); break; case 2: s_printf(buf, "x%d", U.orec.mult); break; case 3: s_int(buf, Game.perfects); break; case 4: s_int(buf, Game.bossBeaten); break; default: s_printf(buf, "%d:%2", U.orec.time / 60, U.orec.time % 60); }
      TX(buf, LW / 2 + 10, y, 9, UI_INK, AL_LEFT, 0, 1.05f);
    }
    draw_chips(312);
    draw_button(&U.it[0], "RETRY", U.focus == 0, 12); draw_button(&U.it[1], "TITLE", U.focus == 1, 12);
  }
  draw_overlay(&Bhud);
  ovX = ovY = 0;
}
/* =========================================================================
   INPUT — raw device state (filled by the platform) → game + menu intents.
   Keyboard and mouse edges carry their arrival time, so a dash is judged
   against the beat as it was heard when the key went down, not when the
   frame happened to run (at 30 fps that difference is up to 33 ms).
   ========================================================================= */
enum { K_W, K_A, K_S, K_D, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_SPACE, K_SHIFT, K_Q, K_E, K_P, K_ESC, K_ENTER, K_BACK, K_1, K_2, K_3, K_R, K_T, K_K, K_N, K_B, K_G, K_U, K_H, K_COUNT };
static struct {
  u8 down[K_COUNT], press[K_COUNT], rep[K_COUNT]; double ptime[K_COUNT];
  float mx, my, lmx, lmy; u8 mdown[3], medge[3]; double mtime[3]; int any;
  int padOk; float ax[6]; u32 btn, pbtn; int navDir; float navT;
} Raw;
static int debugKeys;
static void input_poll(float dt) {
  double now = plat_time();
#define K(c) Raw.down[c]
#define P(c) Raw.press[c]
  float mx = (float)((K(K_D) || K(K_RIGHT) ? 1 : 0) - (K(K_A) || K(K_LEFT) ? 1 : 0)), my = (float)((K(K_S) || K(K_DOWN) ? 1 : 0) - (K(K_W) || K(K_UP) ? 1 : 0));
  float m = mhypot(mx, my); if (m > 1) { mx /= m; my /= m; }
  int dash = P(K_SPACE) || Raw.medge[0], bomb = P(K_Q) || P(K_E) || Raw.medge[2], pause = P(K_ESC) || P(K_P), slow = K(K_SHIFT) || Raw.mdown[1];
  double dt0 = 0; if (P(K_SPACE)) dt0 = Raw.ptime[K_SPACE]; if (Raw.medge[0] && Raw.mtime[0] > dt0) dt0 = Raw.mtime[0];
  dashAge = dt0 > 0 ? clampf((float)(now - dt0), 0, 0.1f) : 0;
  mzero(&Nav, sizeof Nav);
  Nav.up = P(K_UP) || P(K_W) || Raw.rep[K_UP] || Raw.rep[K_W]; Nav.down = P(K_DOWN) || P(K_S) || Raw.rep[K_DOWN] || Raw.rep[K_S];
  Nav.left = P(K_LEFT) || P(K_A) || Raw.rep[K_LEFT] || Raw.rep[K_A]; Nav.right = P(K_RIGHT) || P(K_D) || Raw.rep[K_RIGHT] || Raw.rep[K_D];
  Nav.ok = P(K_ENTER) || P(K_SPACE); Nav.back = P(K_ESC) || P(K_BACK);
  Nav.digit = P(K_1) ? 1 : P(K_2) ? 2 : P(K_3) ? 3 : 0; Nav.reroll = P(K_R); Nav.tap = P(K_T);
  Nav.click = Raw.medge[0]; Nav.moved = mfabs(Raw.mx - Raw.lmx) + mfabs(Raw.my - Raw.lmy) > 0.5f; Raw.lmx = Raw.mx; Raw.lmy = Raw.my;
  int fire = 1; In.aimStick = 0;
  if (Nav.moved || Raw.medge[0] || Raw.medge[1]) In.mode = 0;
  for (int k = 0; k < K_COUNT; k++) if (Raw.press[k]) In.mode = 0;
  if (Raw.padOk) {
    float lx = Raw.ax[0], ly = Raw.ax[1], rx = Raw.ax[2], ry = Raw.ax[3];
    float lm = mhypot(lx, ly), rm = mhypot(rx, ry);
    if (lm < 0.18f) lx = ly = lm = 0; else { float k = minf(1, (lm - 0.18f) / 0.82f) / lm; lx *= k; ly *= k; lm = minf(1, (lm - 0.18f) / 0.82f); }
    if (rm < 0.25f) rx = ry = rm = 0; else { float k = minf(1, (rm - 0.25f) / 0.75f) / rm; rx *= k; ry *= k; rm = minf(1, (rm - 0.25f) / 0.75f); }
    u32 b = Raw.btn, e = b & ~Raw.pbtn;
    if (lm > 0 || rm > 0 || b) In.mode = 1;
    if (In.mode == 1) {
      mx = lx; my = ly;
      if (rm > 0) { float r = mhypot(rx, ry); In.aimX = rx / r; In.aimY = ry / r; In.aimStick = 1; }
      fire = rm > 0.15f;
    }
    if (e) Raw.any = 1;
    if (e & (1 | 32)) { dash = 1; dashAge = 0; }
    if (e & (2 | 16)) bomb = 1;
    if ((b & 4) || Raw.ax[4] > 0.5f) slow = 1;
    if (e & (128 | 64)) pause = 1;
    if (e & 1) Nav.ok = 1;
    if (e & (2 | 64)) Nav.back = 1;
    int dir = ly < -0.55f ? 1 : ly > 0.55f ? 2 : lx < -0.55f ? 3 : lx > 0.55f ? 4 : 0;
    if (dir) {
      if (Raw.navDir != dir) { Raw.navDir = dir; Raw.navT = 0.38f; }
      else if ((Raw.navT -= dt) <= 0) Raw.navT = 0.11f; else dir = 0;
      if (dir == 1) Nav.up = 1; if (dir == 2) Nav.down = 1; if (dir == 3) Nav.left = 1; if (dir == 4) Nav.right = 1;
    } else Raw.navDir = 0;
    Raw.pbtn = b;
  }
  In.moveX = mx; In.moveY = my; In.slow = slow; In.fire = fire; In.dash = dash; In.bomb = bomb; In.pause = pause;
  In.mouseX = Raw.mx; In.mouseY = Raw.my; if (Nav.moved) In.mouseSeen = 1;
  Nav.pause = pause; Nav.any = Raw.any;
  if (debugKeys && Game.state == GS_PLAY) {
    if (P(K_K)) for (int i = 0; i < Game.nen; i++) killEnemy(&Game.en[i], CA_DEBUG);
    if (P(K_N)) { Director.bar = Director.bars; Director.nq = 0; for (int i = 0; i < Game.nen; i++) killEnemy(&Game.en[i], CA_DEBUG); }
    if (P(K_B)) { Game.wave = (Game.wave + 5) / 5 * 5 - 1; Director.bar = Director.bars; Director.nq = 0; for (int i = 0; i < Game.nen; i++) killEnemy(&Game.en[i], CA_DEBUG); }
    if (P(K_G)) { Game.god = !Game.god; fx_text(Game.p.x, Game.p.y - 50, Game.god ? "GOD ON" : "GOD OFF", 14, c3(1, 1, 1), 1); }
    if (P(K_U)) applyUpgrade(randomOpenUpgrade());
    if (P(K_H)) Game.p.hp = Game.st.maxHp;
  }
  mzero(Raw.press, sizeof Raw.press); mzero(Raw.rep, sizeof Raw.rep); mzero(Raw.medge, sizeof Raw.medge); Raw.any = 0;
#undef K
#undef P
}

/* =========================================================================
   PERFORMANCE — quality tiers (internal resolution + particle budget), with
   an automatic mode that steps down when frames run long and back up when
   there is room.
   ========================================================================= */
/* component benchmark state (see COMPONENT BENCHMARK below) */
typedef struct { u8 scene, simd, threads; int frames; double secs, audio, t[NCOMP]; } CBRun;
static struct { int on, i, n, nwMax, measuring, prevSimd; double t0, tm; CBRun r[24]; char text[8192]; } CBm;
static struct { float ema; float t, good; int level; } Perf = { 8, 0, 0, 1 };
/* tiers: scene = output >> (xs, ys). 1 full, 2 half height (line-doubled), 3 half, 4 quarter. */
static const struct { u8 xs, ys, lowBloom; short parts; float budget, gstep; const char *name; } TIER[5] = {
  { 0, 0, 0, 4096, 1.0f, 1.f / 60, "" }, { 0, 0, 0, 4096, 1.0f, 1.f / 60, "FULL" }, { 0, 1, 0, 3072, 0.8f, 1.f / 60, "1/2 HEIGHT" },
  { 1, 1, 1, 2048, 0.5f, 1.f / 30, "1/2" }, { 2, 2, 1, 2048, 0.5f, 1.f / 30, "1/4" } };
/** Highest tier this output size allows: the scene stays at least ~240 lines tall. */
static int tier_max(void) { return OH >= 960 ? 4 : OH >= 360 ? 3 : 1; }
static void set_quality_level(int lv) {
  Perf.level = clampi(lv, 1, tier_max());
  render_settier(TIER[Perf.level].xs, TIER[Perf.level].ys, TIER[Perf.level].lowBloom);
  parts_setCap(TIER[Perf.level].parts); partsBudget = TIER[Perf.level].budget; Grid.step = TIER[Perf.level].gstep;
  Perf.ema = 8; Perf.good = 0; Perf.t = 0;
}
static void perf_apply(void) { set_quality_level(Set.quality ? Set.quality : Perf.level); }
static void perf_update(float work) {
  if (work > 0.25f || Bench.on || CBm.on) return;
  Perf.ema = lerpf(Perf.ema, work * 1000, 0.05f);
  if (Set.quality || (Perf.t += work) < 0.6f) return;
  Perf.t = 0;
  int hz = rate_hz(Set.rate); if (!hz || hz > 240) hz = 240;   /* AUTO trades quality for frame rate up to 240 fps, not beyond */
  float budget = 1000.f / hz;
  if (Perf.ema > budget * 0.92f && Perf.level < tier_max()) set_quality_level(Perf.level + 1);
  else if (Perf.ema < budget * 0.45f && Perf.level > 1) { if (++Perf.good >= 10) set_quality_level(Perf.level - 1); }
  else Perf.good = 0;
}
/** After the output size or format changed (platform layer has set outbuf/fmt32). */
static void video_changed(int ow, int oh) {
  render_setsize(ow, oh);
  set_quality_level(Set.quality ? Set.quality : Perf.level);
  cam_fit();
}

/* =========================================================================
   BENCHMARK — once, on the first start: the attract scene with extra bursts,
   shockwaves and grid hits, at MEDIUM quality and the real output size, with
   no frame cap. After a 0.6 s warm-up (mode switch, caches) it counts frames
   for 2.4 s. Under 30 fps → LOW, over 120 fps → HIGH, otherwise MEDIUM. The
   choice and the measurement are saved, so it never runs again.
   ========================================================================= */
static void bench_start(double now) { mzero(&Bench, sizeof Bench); Bench.on = 1; Bench.t0 = now; Bench.tm = -1; set_quality_level(2); }
static void bench_load(float rdt) {
  Bench.acc += rdt;
  while (Bench.acc > 0.12f) {
    Bench.acc -= 0.12f;
    float x = rndr(220, ARENA_W - 220), y = rndr(220, ARENA_H - 220); C3 c = hsl(frand(), 0.9f, 0.62f);
    burst(x, y, 70, c, 90, 460, 0.5f, 1.3f, NULL); fx_wave(x, y, 280, 0.6f, 1); fx_ring(x, y, 12, 220, 0.5f, c, 2.2f, 48); GEXP(1.2f, x, y, 220);
  }
}
/** Once per frame, with the wall time after the frame was shown. */
static void bench_frame(double now) {
  if (!Bench.on) return;
  double el = now - Bench.t0; Bench.prog = (float)clampf((float)(el / 3.0), 0, 1);
  if (el < 0.6) return;
  if (Bench.tm < 0) { Bench.tm = now; Bench.frames = 0; return; }
  Bench.frames++;
  double m = now - Bench.tm;
  if ((m >= 2.4 && Bench.frames >= 8) || el > 8) {
    float fps = m > 0 ? (float)(Bench.frames / m) : 0;
    int q = fps < 30 ? 3 : fps > 120 ? 1 : 2;
    Set.quality = (u8)q; Save.benchDone = 1; Save.benchQ = (u8)q; Save.benchFps = (u16)clampi(mlrint(fps), 0, 65535);
    Bench.on = 0; Bench.fps = fps; Bench.q = q; Bench.shown = 6;
    save_write(); perf_apply();
  }
}

/* =========================================================================
   CPU — CPUID feature bits (every i686 has CPUID), XGETBV for the OS's AVX
   state; the platform layer adds what the OS allows. SIMD paths run only on
   NT-family Windows (2000 and later); Windows 95/98 stay on x87/integer.
   ========================================================================= */
static void cpu_detect(void) {
#if defined(__i386__) || defined(__x86_64__)
  unsigned a, b, c, d, mx = __get_cpuid_max(0, NULL);
  if (!mx) return;
  __cpuid(0, a, b, c, d); memcpy(Cpu.vendor, &b, 4); memcpy(Cpu.vendor + 4, &d, 4); memcpy(Cpu.vendor + 8, &c, 4); Cpu.vendor[12] = 0;
  __cpuid(1, a, b, c, d);
  Cpu.mmx = (d >> 23) & 1; Cpu.sse = (d >> 25) & 1; Cpu.sse2 = (d >> 26) & 1; Cpu.ssse3 = (c >> 9) & 1; Cpu.sse41 = (c >> 19) & 1;
  int osxsave = (c >> 27) & 1; Cpu.avx = (c >> 28) & 1;
  if (mx >= 7) { __cpuid_count(7, 0, a, b, c, d); Cpu.avx2 = (b >> 5) & 1; }
  if (osxsave && Cpu.avx) { unsigned lo, hi; __asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(lo), "=d"(hi) : "c"(0)); (void)hi; Cpu.osAvx = (lo & 6) == 6; }
  if (__get_cpuid_max(0x80000000u, NULL) >= 0x80000004u) {
    u32 r[12]; for (int i = 0; i < 3; i++) __cpuid(0x80000002u + i, r[i * 4], r[i * 4 + 1], r[i * 4 + 2], r[i * 4 + 3]);
    memcpy(Cpu.brand, r, 48); Cpu.brand[48] = 0; char *p = Cpu.brand; while (*p == ' ') p++; if (p != Cpu.brand) s_cpy(Cpu.brand, p);
  }
#endif
}
static void simd_set(int level);   /* the renderer's kernel table */
/** Gathers are fast on some CPUs and slow on others (much slower on Intel parts with the 2023 gather
    mitigation), so the AVX2 composite is timed against the scalar-LUT one on a synthetic row, once. */
static float gatherRatio;
static void simd_tune(void) {
#if defined(__i386__) || defined(__x86_64__)
  if (simdMax < 2) return;
  static u32 src[4096], dst[4096]; static BRow b[2048 + 8]; u32 x = 12345;
  for (int i = 0; i < 4096; i++) { x = x * 1664525u + 1013904223u; src[i] = x & 0xFFDFFBFFu; }   /* random fields, guard bits clear */
  for (int i = 0; i < 2048 + 8; i++) { b[i].r = i & 511; b[i].gb = ((u32)(i & 511) << 11) | (u32)(i & 255); }
  double best[2] = { 1e9, 1e9 };
  for (int trial = 0; trial < 5; trial++) for (int k = 0; k < 2; k++) {
    double t0 = plat_clock();
    for (int r = 0; r < 24; r++) (k ? comp32_avx2 : comp32_sse2)(dst, src, b, LUT32[0][0], 1024, 0);
    double t = plat_clock() - t0; if (t < best[k]) best[k] = t;
  }
  gatherRatio = best[0] > 0 ? (float)(best[1] / best[0]) : 9;
  gatherFast = gatherRatio < 0.85f;
#endif
}
static void simd_choose(int cap) {
  simdMax = Cpu.osSse && Cpu.sse2 ? (Cpu.avx2 && Cpu.osAvx ? 2 : 1) : 0;
  if (cap >= 0 && cap < simdMax) simdMax = cap;
  simd_set(simdMax); simd_tune(); simd_set(simdMax);
}
/* float → text, for reports */
static char *s_fix(char *d, double v, int dec) {
  if (v < 0) { *d++ = '-'; v = -v; }
  int sc = 1; for (int i = 0; i < dec; i++) sc *= 10;
  long long q = (long long)(v * sc + 0.5); char t[24]; s_int(t, (int)(q / sc)); d = s_cpy(d, t);
  if (dec) { *d++ = '.'; long long f = q % sc; for (int i = dec - 1; i >= 0; i--) { d[i] = (char)('0' + f % 10); f /= 10; } d += dec; *d = 0; }
  return d;
}
static char *s_pad(char *line, char *end, int col) { while (end - line < col) *end++ = ' '; *end = 0; return end; }

/* =========================================================================
   COMPONENT BENCHMARK (PHASE95.EXE -bench) — the title attract mode, sector
   3 and the sector 25 guardian (auto-pilot, invulnerable), each at every
   instruction set this machine and OS allow, on one thread and on all of
   them, with no frame cap. Each run warms up (5 s for the guardian, past its
   intro banner), then counts 2.5 s of frames. The report gives frames per
   second and every component's milliseconds per frame.
   ========================================================================= */
static const char *const CB_SCENE[3] = { "TITLE", "SECTOR 3", "BOSS 25" };
static void (*frame_hook)(void);
static void bench_bot(void) {   /* the attract-mode AI flies; dashes land on beats */
  if (Game.state != GS_PLAY || Game.paused || !Game.p.alive) return;
  InputState keep = In; game_demoInput(); In.slow = keep.slow; In.pause = 0;
  if (Game.p.hp == 1 && Game.p.bombs > 0 && Game.nen > 30 && chance(0.01f)) In.bomb = 1;
}
static void cb_setup(void) {
  CBRun *r = &CBm.r[CBm.i];
  simd_set(r->simd); nWork = r->threads; rng_s = 0x2545F491u; frame_hook = NULL;
  ui_toTitle();
  if (r->scene) { ui_hide(); game_newRun(r->scene == 1 ? 3 : 25, 0); Game.god = 1; frame_hook = bench_bot; }
  CBm.measuring = 0; CBm.t0 = plat_clock();
}
static void cb_start(void) {
  mzero(&CBm, sizeof CBm); CBm.nwMax = nWork; CBm.prevSimd = simdLevel;
  for (int sc = 0; sc < 3; sc++) for (int lv = 0; lv <= simdMax; lv++) for (int t = 0; t < 2; t++) {
    int th = t ? CBm.nwMax : 1; if (t && th == 1) continue;
    CBRun *r = &CBm.r[CBm.n++]; r->scene = (u8)sc; r->simd = (u8)lv; r->threads = (u8)th;
  }
  CBm.on = 1; compOn = 1; cb_setup();
}
static void plat_bench_done(const char *text);
static void cb_finish(void) {
  char *o = CBm.text, line[256], *e; const char *Q[5] = { "AUTO", "HIGH", "MEDIUM", "LOW", "LOWEST" };
  o = s_cpy(o, "PHASE 95 COMPONENT BENCHMARK\r\n\r\n");
  s_printf(line, "BUILD     %s\r\n", PH_BUILD); o = s_cpy(o, line);
  s_printf(line, "CPU       %s  %s\r\n", Cpu.vendor, Cpu.brand); o = s_cpy(o, line);
  s_printf(line, "FEATURES  %s%s%s%s%s%s%s\r\n", Cpu.mmx ? "MMX " : "", Cpu.sse ? "SSE " : "", Cpu.sse2 ? "SSE2 " : "", Cpu.ssse3 ? "SSSE3 " : "", Cpu.sse41 ? "SSE4.1 " : "", Cpu.avx ? "AVX " : "", Cpu.avx2 ? "AVX2" : ""); o = s_cpy(o, line);
  s_printf(line, "OS        %s %d.%d  SSE state %s  AVX state %s  ->  kernels up to %s\r\n", Cpu.nt ? "WINDOWS NT" : "WINDOWS 9X", Cpu.osMajor, Cpu.osMinor, Cpu.osSse ? "saved" : "not saved", Cpu.osAvx ? "saved" : "not saved", SIMD_NAME[simdMax]); o = s_cpy(o, line);
  s_printf(line, "THREADS   %d logical CPUs, %d used (up to %d), frames shown from their own thread: %s\r\n", Cpu.logical, CBm.nwMax, NWMAX, presentThreaded ? "yes" : "no"); o = s_cpy(o, line);
  if (simdMax >= 2) { e = s_cpy(line, "GATHER    AVX2 composite at "); e = s_fix(e, gatherRatio, 2); e = s_cpy(e, gatherFast ? "x the scalar-LUT time: used\r\n" : "x the scalar-LUT time: not used\r\n"); o = s_cpy(o, line); }
  s_printf(line, "OUTPUT    %dX%d %d-bit, scene %dX%d (quality %s)\r\n", OW, OH, fmt32 ? 32 : 16, RW, RH, Q[Set.quality <= 4 ? Set.quality : 0]); o = s_cpy(o, line);
  if (Save.benchDone) { s_printf(line, "FIRST-START BENCHMARK  %d FPS -> %s\r\n", Save.benchFps, Q[Save.benchQ <= 4 ? Save.benchQ : 0]); o = s_cpy(o, line); }
  o = s_cpy(o, "\r\nmilliseconds per frame; AUDIO = mixer thread load (% of one CPU)\r\n");
  static const int COL[] = { 0, 10, 16, 21, 30, 38, 46, 54, 62, 70, 78, 86, 94, 102, 110 };
  e = s_cpy(line, "SCENE"); e = s_pad(line, e, COL[1]); e = s_cpy(e, "SIMD"); e = s_pad(line, e, COL[2]); e = s_cpy(e, "THR"); e = s_pad(line, e, COL[3]); e = s_cpy(e, "FPS");
  for (int c = 0; c < NCOMP; c++) { e = s_pad(line, e, COL[4 + c]); e = s_cpy(e, COMP_NAME[c]); }
  e = s_pad(line, e, COL[4 + NCOMP]); e = s_cpy(e, "AUDIO"); e = s_cpy(e, "\r\n"); o = s_cpy(o, line);
  for (int i = 0; i < CBm.n; i++) {
    CBRun *r = &CBm.r[i];
    e = s_cpy(line, CB_SCENE[r->scene]); e = s_pad(line, e, COL[1]); e = s_cpy(e, SIMD_NAME[r->simd]); e = s_pad(line, e, COL[2]); s_int(e, r->threads); e += s_len(e);
    e = s_pad(line, e, COL[3]); e = s_fix(e, r->secs > 0 ? r->frames / r->secs : 0, 1);
    for (int c = 0; c < NCOMP; c++) { e = s_pad(line, e, COL[4 + c]); e = s_fix(e, r->t[c] * 1000, c == C_FRAME ? 2 : 3); }
    e = s_pad(line, e, COL[4 + NCOMP]); e = s_fix(e, r->audio * 100, 1); e = s_cpy(e, "%\r\n"); o = s_cpy(o, line);
  }
  CBm.on = 0; compOn = 0; nWork = CBm.nwMax; simd_set(CBm.prevSimd); frame_hook = NULL; ui_toTitle();
  plat_bench_done(CBm.text);
}
/** Once per frame, after it was shown. */
static void cb_frame(void) {
  if (!CBm.on) return;
  CBRun *r = &CBm.r[CBm.i]; double now = plat_clock();
  if (!CBm.measuring) {
    if (now - CBm.t0 >= (r->scene == 2 ? 5.0 : 1.0)) { CBm.measuring = 1; CBm.tm = now; r->frames = 0; mzero(compT, sizeof compT); compAudio = 0; }
    return;
  }
  r->frames++;
  if (now - CBm.tm >= 2.5) {
    r->secs = now - CBm.tm; for (int c = 0; c < NCOMP; c++) r->t[c] = compT[c] / r->frames; r->audio = compAudio / r->secs;
    if (++CBm.i >= CBm.n) cb_finish(); else cb_setup();
  }
}
/* the live readout (SHOW FPS): component times, smoothed */
static float compEma[NCOMP];
static void comp_tick(void) {
  compOn = Set.fps || CBm.on;
  if (CBm.on || !compOn) return;
  for (int c = 0; c < NCOMP; c++) { compEma[c] = lerpf(compEma[c], (float)(compT[c] * 1000), 0.05f); compT[c] = 0; }
}

/* =========================================================================
   MAIN LOOP
   ========================================================================= */
static float fpsShown = 60;
static void game_frame(float raw) {
  float rdt = clampf(raw, 0, 0.05f);
  PROF(C_SIM);
  snd_schedule();
  input_poll(rdt);
  if (frame_hook) frame_hook();
  if (!Bench.on) ui_update(rdt, Raw.mx / US - UOX, Raw.my / US - UOY); else bench_load(rdt);
  if (Bench.shown > 0) Bench.shown -= rdt;
  switch (Game.state) {
    case GS_BOOT: game_updateTitle(rdt); if (Nav.any && U.cur == SC_BOOT && !Bench.on) ui_toTitle(); break;
    case GS_TITLE: game_updateTitle(rdt); break;
    case GS_PLAY: if (!Game.paused) { if (In.pause && !U.cur) ui_pause(); else game_update(rdt); } break;
    case GS_UPGRADE: fx_update(rdt); grid_update(rdt); Game.simDt += rdt; Game.visBeat = visBeat(); { float f = (float)(Game.visBeat - mfloor((float)Game.visBeat)); Game.beatPulse = (1 - f) * (1 - f) * (1 - f); } cam_update(rdt, Cam.x, Cam.y, 1, 1); break;
    case GS_OVER: game_update(rdt); break;
  }
  PROFE(C_SIM);
  game_render(rdt);
  PROF(C_HUD);
  Bhud.n = 0;
  if (Game.state != GS_BOOT && Game.state != GS_TITLE) hud_game(&Bhud, UW, UH, rdt);
  draw_overlay(&Bhud);
  ui_draw();
  if (Set.fps || CBm.on) {
    char b[200], *e;
    fpsShown = lerpf(fpsShown, raw > 0 ? 1 / raw : 60, 0.05f);
    Bhud.n = 0; FOpt o = fo(AL_CENTER, 1, 1, 0, NULL);
    if (CBm.on) { CBRun *r = &CBm.r[CBm.i]; s_printf(b, "COMPONENT BENCHMARK %d/%d \xB7 %s \xB7 %s \xB7 %d THREAD%s", CBm.i + 1, CBm.n, CB_SCENE[r->scene], SIMD_NAME[r->simd], r->threads, r->threads > 1 ? "S" : ""); font_draw(&Bhud, b, UW / 2, 12, 8, c3(1.5f, 1.3f, 0.5f), &o); }
    s_printf(b, "%d FPS  %dX%d  SCENE %dX%d  %s X%d  %d PARTICLES", mlrint(fpsShown), OW, OH, RW, RH, SIMD_NAME[simdLevel], nWork, pLive);
    font_draw(&Bhud, b, UW / 2, UH - 22, 7, c3(0.9f, 0.8f, 1.3f), &o);
    e = b; *e = 0;
    for (int c = 0; c < NCOMP; c++) { e = s_cpy(e, COMP_NAME[c]); *e++ = ' '; e = s_fix(e, compEma[c], 2); e = s_cpy(e, c < NCOMP - 1 ? "  " : ""); }
    if (!CBm.on) font_draw(&Bhud, b, UW / 2, UH - 11, 6, c3(0.7f, 0.85f, 1.2f), &o);
    draw_overlay(&Bhud);
  }
  PROFE(C_HUD);
}
static Seg segPool[48000];
static void game_boot(void) {
  audio_tables(); curve_init(); exp_init(); sky_init(); font_init(); grid_init(); parts_init();
  nhits = 0; hash_build();
  int off = 0;
  Bdark.s = segPool + off; Bdark.cap = 512; off += 512;
  Bfloor.s = segPool + off; Bfloor.cap = 9000; off += 9000;
  Bworld.s = segPool + off; Bworld.cap = 26000; off += 26000;
  Bhud.s = segPool + off; Bhud.cap = 48000 - off;
  palCur = PALETTES[0];
  cam_fit();
  Game.state = GS_BOOT;
  game_startDemo();
  ui_show(SC_BOOT);
}

/* =========================================================================
   PLATFORM: WINDOWS (95 and later)
   ========================================================================= */
#if defined(PH_WIN32)
/* test build: -DPH_TEST_XP=1000000000 starts with that much XP, in its own save file */
#ifdef PH_TEST_XP
#define PH_SAVE_NAME "PHASE95T.SAV"
#define PH_TITLE "PHASE (TEST BUILD)"
#else
#define PH_SAVE_NAME "PHASE95.SAV"
#define PH_TITLE "PHASE"
#endif
static HWND hwnd; static HDC memdc; static HBITMAP dib; static int running = 1, active = 1, isFull, audioOk;
static void video_resize(int w, int h);
static double qpf, t0; static LARGE_INTEGER qpc0; static int useQpc;
static char savePath[MAX_PATH];
static double plat_time(void) {
  if (useQpc) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)(c.QuadPart - qpc0.QuadPart) / qpf; }
  return timeGetTime() / 1000.0 - t0;
}
static double plat_clock(void) { return plat_time(); }
static void plat_os_detect(void) {
  OSVERSIONINFOA v; mzero(&v, sizeof v); v.dwOSVersionInfoSize = sizeof v;
  if (GetVersionExA(&v)) { Cpu.nt = v.dwPlatformId == VER_PLATFORM_WIN32_NT; Cpu.osMajor = (int)v.dwMajorVersion; Cpu.osMinor = (int)v.dwMinorVersion; }
  typedef BOOL (WINAPI *IPFP)(DWORD);   /* not on Windows 95/98: looked up, not imported */
  IPFP ipf = (IPFP)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"), "IsProcessorFeaturePresent");
  Cpu.osSse = Cpu.nt && Cpu.osMajor >= 5 && (!ipf || ipf(10 /* PF_XMMI64_INSTRUCTIONS_AVAILABLE */));
  SYSTEM_INFO si; mzero(&si, sizeof si); GetSystemInfo(&si); Cpu.logical = (int)si.dwNumberOfProcessors;
}
static double msgTime(void) { DWORD m = (DWORD)GetMessageTime(), n = GetTickCount(); return plat_time() - (double)(DWORD)(n - m) / 1000.0; }
static float *mx_alloc(int n) { return (float *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n * sizeof(float) + 16); }
/* The save lives next to the EXE; if that folder is read-only (a CD, a protected folder) it falls back
   to %APPDATA%\PHASE95 (or WINDOWS\PHASE95 where there is no APPDATA, as on Windows 95). */
static char altDir[MAX_PATH], altPath[MAX_PATH];
static void save_paths(void) {
  GetModuleFileNameA(NULL, savePath, MAX_PATH); { int n = s_len(savePath); while (n > 0 && savePath[n - 1] != '\\' && savePath[n - 1] != '/') n--; s_cpy(savePath + n, PH_SAVE_NAME); }
  DWORD k = GetEnvironmentVariableA("APPDATA", altDir, MAX_PATH - 32); if (!k || k >= MAX_PATH - 32) k = GetWindowsDirectoryA(altDir, MAX_PATH - 32);
  if (!k || k >= MAX_PATH - 32) { altDir[0] = 0; return; }
  s_cpy(altDir + s_len(altDir), "\\PHASE95"); s_cpy(s_cpy(s_cpy(altPath, altDir), "\\"), PH_SAVE_NAME);
}
static int file_write(const char *path, const void *p, int n) { HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); if (f == INVALID_HANDLE_VALUE) return 0; DWORD w = 0; WriteFile(f, p, n, &w, NULL); CloseHandle(f); return (int)w == n; }
static int file_read(const char *path, void *p, int n) { HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); if (f == INVALID_HANDLE_VALUE) return 0; DWORD r = 0; ReadFile(f, p, n, &r, NULL); CloseHandle(f); return (int)r; }
static int plat_save(const void *p, int n) {
  if (file_write(savePath, p, n)) return 1;
  if (!altDir[0] || !s_cmp(savePath, altPath)) return 0;
  CreateDirectoryA(altDir, NULL);
  if (!file_write(altPath, p, n)) return 0;
  s_cpy(savePath, altPath); return 1;   /* keep using the fallback this session */
}
static int plat_load(void *p, int n) {
  int r = file_read(savePath, p, n); if (r) return r;
  if (altDir[0] && (r = file_read(altPath, p, n)) > 0) s_cpy(savePath, altPath);
  return r;
}
static u32 plat_date(void) { SYSTEMTIME s; GetLocalTime(&s); return s.wYear * 10000u + s.wMonth * 100u + s.wDay; }

/* ---- audio: waveOut, 4 × 512-frame buffers, refilled from a thread ---- */
#define NBUF 4
#define BUFN 512
static HWAVEOUT wo; static WAVEHDR wh[NBUF]; static short wbuf[NBUF][BUFN * 2]; static HANDLE wevent;
static DWORD WINAPI audio_thread(LPVOID arg) {
  (void)arg;
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  while (running) {
    WaitForSingleObject(wevent, 50);
    for (int i = 0; i < NBUF; i++) {
      if (!(wh[i].dwFlags & WHDR_DONE)) continue;
      double a0 = plat_time(); mx_render(wbuf[i], BUFN); if (compOn) compAudio += plat_time() - a0;
      wh[i].dwFlags &= ~WHDR_DONE;
      waveOutWrite(wo, &wh[i], sizeof(WAVEHDR));
      MMTIME mt; mt.wType = TIME_SAMPLES; waveOutGetPosition(wo, &mt, sizeof mt);
      SND_LOCK(); aud_rt = (double)Mx.pos / SR; aud_wall = plat_time(); if (mt.wType == TIME_SAMPLES) aud_lat = (double)(Mx.pos - (i64)mt.u.sample) / SR; SND_UNLOCK();
    }
  }
  return 0;
}
static int audio_open(void) {
  WAVEFORMATEX f; mzero(&f, sizeof f); f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 2; f.nSamplesPerSec = SR; f.wBitsPerSample = 16; f.nBlockAlign = 4; f.nAvgBytesPerSec = SR * 4;
  wevent = CreateEventA(NULL, FALSE, FALSE, NULL);
  if (waveOutOpen(&wo, WAVE_MAPPER, &f, (DWORD_PTR)wevent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) return 0;
  aud_blk = (float)BUFN / SR; aud_lat = (double)BUFN * NBUF / SR;
  for (int i = 0; i < NBUF; i++) {
    mzero(&wh[i], sizeof wh[i]); wh[i].lpData = (LPSTR)wbuf[i]; wh[i].dwBufferLength = BUFN * 4;
    waveOutPrepareHeader(wo, &wh[i], sizeof(WAVEHDR));
    mx_render(wbuf[i], BUFN); waveOutWrite(wo, &wh[i], sizeof(WAVEHDR));
  }
  aud_rt = (double)Mx.pos / SR; aud_wall = plat_time();
  DWORD id; CreateThread(NULL, 0, audio_thread, NULL, 0, &id);
  return 1;
}


/* ---- worker threads (multi-CPU machines; Windows 95/98 run single-CPU, so they stay at one) ---- */
static HANDLE wkEv[NWMAX];
static void wk_sleep(int i) { WaitForSingleObject(wkEv[i], INFINITE); }
static void wk_wake(int i) { SetEvent(wkEv[i]); }
static DWORD WINAPI worker(LPVOID p) { worker_loop((int)(INT_PTR)p); return 0; }
static void workers_start(void) {
  SYSTEM_INFO si; mzero(&si, sizeof si); GetSystemInfo(&si); int n = (int)si.dwNumberOfProcessors; if (n > NWMAX) n = NWMAX; nWork = 1; if (n < 2) return;
  wkEv[0] = CreateEventA(NULL, FALSE, FALSE, NULL);
  for (int i = 1; i < n; i++) {
    wkEv[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
    DWORD id; HANDLE t = CreateThread(NULL, 0, worker, (LPVOID)(INT_PTR)i, 0, &id); if (!t) break;
    CloseHandle(t); nWork = i + 1;
  }
}
static void *plat_alloc(size_t n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
static void plat_free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

/* ---- video: a DIB section the size of the client area (RGB565 on 16-bit displays, XRGB8888 above),
   blitted 1:1 with GDI. Fullscreen is a borderless window over the desktop (DESKTOP, or a listed
   resolution equal to it), or a display-mode switch to the listed resolution. ---- */
static int cliW = 640, cliH = 480, blitScaled, modeChanged, needResize;
/* On multi-core NT systems the frame is shown from a second thread out of one of two DIB sections, while
   the next frame is drawn into the other: the blit (tens of MB a second at HD and 4K) overlaps rendering
   instead of adding to it. A frame still goes to the screen as soon as it is finished. */
static int prOn, prCur, prShown; static HDC prDC[2]; static HBITMAP prBmp[2]; static void *prBits[2];
static HANDLE prGo, prIdle; static volatile int prBuf; static volatile double prLast;
static void present_from(HDC dc, HDC src);
static DWORD WINAPI present_thread(LPVOID p) {
  (void)p;
  for (;;) {
    WaitForSingleObject(prGo, INFINITE);
    double t = plat_clock(); HDC dc = GetDC(hwnd); present_from(dc, prDC[prBuf]); GdiFlush(); ReleaseDC(hwnd, dc);
    prLast = plat_clock() - t; SetEvent(prIdle);
  }
  return 0;
}
static void pr_wait(void) { if (prOn) WaitForSingleObject(prIdle, INFINITE); }
static void pr_start(void) {
  if (nWork < 2 || !Cpu.nt) return;
  prGo = CreateEventA(NULL, FALSE, FALSE, NULL); prIdle = CreateEventA(NULL, TRUE, TRUE, NULL);
  DWORD id; HANDLE t = prGo && prIdle ? CreateThread(NULL, 0, present_thread, NULL, 0, &id) : NULL;
  if (t) { SetThreadPriority(t, THREAD_PRIORITY_ABOVE_NORMAL); CloseHandle(t); prOn = 1; }
}
static int scr_bpp(void) { HDC d = GetDC(NULL); int b = GetDeviceCaps(d, BITSPIXEL) * GetDeviceCaps(d, PLANES); ReleaseDC(NULL, d); return b; }
static void video_resize(int w, int h) {
  if (w < 1 || h < 1) return;
  int f32 = scr_bpp() > 16;
  float sc = minf(1, minf((float)OWMAX / w, (float)OHMAX / h));   /* clients beyond 3840×2400 are stretched */
  int ow = ((int)(w * sc) + 3) & ~3, oh = ((int)(h * sc) + 3) & ~3;
  if (ow < 320) ow = 320; if (oh < 240) oh = 240; if (ow > OWMAX) ow = OWMAX; if (oh > OHMAX) oh = OHMAX;
  cliW = w; cliH = h; blitScaled = ow - w > 3 || oh - h > 3 || ow < w || oh < h;
  if (dib && ow == OW && oh == OH && f32 == fmt32) return;
  pr_wait();
  struct { BITMAPINFOHEADER h; DWORD m[3]; } bi; mzero(&bi, sizeof bi);
  bi.h.biSize = sizeof bi.h; bi.h.biWidth = ow; bi.h.biHeight = -oh; bi.h.biPlanes = 1;
  if (f32) { bi.h.biBitCount = 32; bi.h.biCompression = BI_RGB; } else { bi.h.biBitCount = 16; bi.h.biCompression = BI_BITFIELDS; bi.m[0] = 0xF800; bi.m[1] = 0x07E0; bi.m[2] = 0x001F; }
  HDC wdc = GetDC(hwnd); void *bits = NULL;
  HBITMAP nd = CreateDIBSection(wdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!memdc) memdc = CreateCompatibleDC(wdc);
  ReleaseDC(hwnd, wdc);
  if (!nd || !bits) {
    if (!dib) { MessageBoxA(hwnd, "Could not create the frame buffer.", "PHASE", MB_OK | MB_ICONERROR); ExitProcess(1); }
    blitScaled = 1; return;   /* keep the previous buffer and stretch it */
  }
  SelectObject(memdc, nd); if (dib) DeleteObject(dib);
  dib = nd; outbuf = bits; fmt32 = f32;
  if (prOn) {   /* the second buffer for the present thread; without it, fall back to presenting in place */
    HDC wd = GetDC(hwnd); void *b2 = NULL; HBITMAP n2 = CreateDIBSection(wd, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &b2, NULL, 0);
    if (!prDC[1]) prDC[1] = CreateCompatibleDC(wd);
    ReleaseDC(hwnd, wd);
    if (n2 && b2 && prDC[1]) { SelectObject(prDC[1], n2); if (prBmp[1]) DeleteObject(prBmp[1]); prBmp[1] = n2; prBits[1] = b2; }
    else { if (n2) DeleteObject(n2); if (prBmp[1]) { DeleteObject(prBmp[1]); prBmp[1] = NULL; } prBits[1] = NULL; }
    prDC[0] = memdc; prBmp[0] = dib; prBits[0] = bits; prCur = 0; prShown = 0; presentThreaded = prBits[1] != NULL;
  }
  video_changed(ow, oh);
}
static void present_from(HDC dc, HDC src) {
  if (!blitScaled) BitBlt(dc, 0, 0, cliW, cliH, src, 0, 0, SRCCOPY);
  else { SetStretchBltMode(dc, COLORONCOLOR); StretchBlt(dc, 0, 0, cliW, cliH, src, 0, 0, OW, OH, SRCCOPY); }
}
static void present(HDC dc) { pr_wait(); present_from(dc, prOn ? prDC[prShown] : memdc); }
/* show the finished frame: hand it to the present thread and draw the next one into the other buffer */
static void frame_show(void) {
  if (!prOn || !prBits[1]) { HDC dc = GetDC(hwnd); GdiFlush(); present_from(dc, memdc); ReleaseDC(hwnd, dc); prLast = 0; return; }
  pr_wait(); if (compOn) compT[C_PRESENT] += prLast;   /* the previous frame's blit, timed on its own thread */
  GdiFlush(); ResetEvent(prIdle); prBuf = prShown = prCur; SetEvent(prGo);
  prCur ^= 1; outbuf = prBits[prCur];
}
/* ---- display modes: "best" is the largest mode the driver lists (up to 3840×2400, at 60 Hz or more,
   16-bit or deeper), taken at 32-bit and its highest refresh rate ---- */
typedef struct { int w, h, bpp, hz; } VMode;
static VMode bestMode; static int bestKnown, forceWindowed;
static int mode_get(DWORD i, VMode *m) {
  DEVMODEA dm; mzero(&dm, sizeof dm); dm.dmSize = sizeof dm; if (!EnumDisplaySettingsA(NULL, i, &dm)) return 0;
  m->w = (int)dm.dmPelsWidth; m->h = (int)dm.dmPelsHeight; m->bpp = (int)dm.dmBitsPerPel; m->hz = (int)dm.dmDisplayFrequency; return 1;
}
static int mode_pref(const VMode *a, const VMode *b, int prefer16) {   /* at equal size: is a better than b? */
  int ka = prefer16 ? a->bpp == 16 : a->bpp >= 24, kb = prefer16 ? b->bpp == 16 : b->bpp >= 24;
  if (ka != kb) return ka > kb;
  return a->hz > b->hz;
}
static int find_mode(int w, int h, int prefer16, VMode *out) {
  VMode m; int found = 0;
  for (DWORD i = 0; mode_get(i, &m); i++) { if (m.w != w || m.h != h || m.bpp < 16) continue; if (!found || mode_pref(&m, out, prefer16)) { *out = m; found = 1; } }
  return found;
}
static void desk_size(int *w, int *h) {
  VMode m; if (mode_get(ENUM_REGISTRY_SETTINGS, &m) && m.w > 0) { *w = m.w; *h = m.h; return; }
  *w = GetSystemMetrics(SM_CXSCREEN); *h = GetSystemMetrics(SM_CYSCREEN);
}
static void best_find(void) {
  VMode m; int found = 0;
  for (DWORD i = 0; mode_get(i, &m); i++) {
    if (m.bpp < 16 || m.w > OWMAX || m.h > OHMAX || m.w < 640 || m.h < 480 || (m.hz > 1 && m.hz < 59)) continue;
    long a = (long)m.w * m.h, b = found ? (long)bestMode.w * bestMode.h : 0;
    if (!found || a > b || (a == b && mode_pref(&m, &bestMode, 0))) { bestMode = m; found = 1; }
  }
  if (!found) { desk_size(&bestMode.w, &bestMode.h); bestMode.bpp = bestMode.hz = 0; }
  bestKnown = 1;
}
static void plat_res_info(int res, int *w, int *h) {
  if (res == RES_BEST) { if (!bestKnown) best_find(); *w = bestMode.w; *h = bestMode.h; }
  else if (res == RES_DESKTOP) desk_size(w, h);
  else { *w = RESOLUTIONS[res].w; *h = RESOLUTIONS[res].h; }
}
static int set_mode(const VMode *m) {
  DEVMODEA dm; mzero(&dm, sizeof dm); dm.dmSize = sizeof dm; dm.dmPelsWidth = m->w; dm.dmPelsHeight = m->h; dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
  if (m->bpp) { dm.dmBitsPerPel = m->bpp; dm.dmFields |= DM_BITSPERPEL; }
  if (m->hz > 1) { dm.dmDisplayFrequency = m->hz; dm.dmFields |= DM_DISPLAYFREQUENCY; if (ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL) return 1; dm.dmFields &= ~DM_DISPLAYFREQUENCY; }
  if (ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL) return 1;
  dm.dmFields &= ~DM_BITSPERPEL; return ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL;
}
/** Fullscreen: BEST and the listed sizes switch the display mode (exclusive); DESKTOP covers the desktop
    with a borderless window. Windowed: a resizable window of the chosen size, fitted to the work area. */
static void plat_video_apply(void) {
  if (!hwnd) return;
  pr_wait();
  int full = Set.fullscreen && !forceWindowed, rw, rh;
  if (modeChanged) { ChangeDisplaySettingsA(NULL, 0); modeChanged = 0; }
  ShowWindow(hwnd, SW_RESTORE);
  plat_res_info(Set.res, &rw, &rh);
  if (full) {
    if (Set.res != RES_DESKTOP) {
      VMode m;
      if (Set.res == RES_BEST && bestMode.bpp) m = bestMode;
      else if (!find_mode(rw, rh, rw == 640, &m)) { m.w = rw; m.h = rh; m.bpp = rw == 640 ? 16 : 32; m.hz = 0; }   /* 640×480 prefers 16-bit: the fast path on old cards */
      if (set_mode(&m)) modeChanged = 1;
    }
    int dw = GetSystemMetrics(SM_CXSCREEN), dh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowLongA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, dw, dh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    isFull = 1;
  } else {
    RECT wa; if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0)) { wa.left = wa.top = 0; wa.right = GetSystemMetrics(SM_CXSCREEN); wa.bottom = GetSystemMetrics(SM_CYSCREEN); }
    int aw = wa.right - wa.left, ah = wa.bottom - wa.top, cw = rw > 0 ? rw : aw, ch = rh > 0 ? rh : ah;
    DWORD st = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    RECT r = { 0, 0, cw, ch }; AdjustWindowRect(&r, st, FALSE);
    int fw = r.right - r.left, fh = r.bottom - r.top;
    if (fw > aw || fh > ah) {   /* shrink to the work area, keeping the aspect */
      float k = minf((float)(aw - (fw - cw)) / cw, (float)(ah - (fh - ch)) / ch); cw = (int)(cw * k); ch = (int)(ch * k);
      r.left = r.top = 0; r.right = cw; r.bottom = ch; AdjustWindowRect(&r, st, FALSE); fw = r.right - r.left; fh = r.bottom - r.top;
    }
    SetWindowLongA(hwnd, GWL_STYLE, st);
    SetWindowPos(hwnd, HWND_NOTOPMOST, wa.left + (aw - fw) / 2, wa.top + (ah - fh) / 2, fw, fh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    isFull = 0;
  }
  ClipCursor(NULL);
  RECT c; GetClientRect(hwnd, &c); video_resize(c.right, c.bottom); needResize = 0;
}
static void toggle_fullscreen(void) { int cur = Set.fullscreen && !forceWindowed; forceWindowed = 0; Set.fullscreen = (u8)!cur; save_write(); plat_video_apply(); }
static int vk_map(WPARAM vk) {
  switch (vk) {
    case 'W': return K_W; case 'A': return K_A; case 'S': return K_S; case 'D': return K_D; case VK_UP: return K_UP; case VK_DOWN: return K_DOWN; case VK_LEFT: return K_LEFT; case VK_RIGHT: return K_RIGHT;
    case VK_SPACE: return K_SPACE; case VK_SHIFT: return K_SHIFT; case 'Q': return K_Q; case 'E': return K_E; case 'P': return K_P; case VK_ESCAPE: return K_ESC; case VK_RETURN: return K_ENTER; case VK_BACK: return K_BACK;
    case '1': case VK_NUMPAD1: return K_1; case '2': case VK_NUMPAD2: return K_2; case '3': case VK_NUMPAD3: return K_3; case 'R': return K_R; case 'T': return K_T;
    case 'K': return K_K; case 'N': return K_N; case 'B': return K_B; case 'G': return K_G; case 'U': return K_U; case 'H': return K_H;
  }
  return -1;
}
static void mouse_pos(LPARAM lp) {
  float x = (short)LOWORD(lp), y = (short)HIWORD(lp);
  if (blitScaled && cliW > 0 && cliH > 0) { x = x * OW / cliW; y = y * OH / cliH; }
  Raw.mx = x; Raw.my = y;
}
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CLOSE: running = 0; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_ACTIVATEAPP:
      active = w != 0;
      if (!active) { if (Game.state == GS_PLAY) ui_pause(); if (isFull) { if (modeChanged) { ChangeDisplaySettingsA(NULL, 0); modeChanged = 0; } ShowWindow(h, SW_MINIMIZE); } mzero(Raw.down, sizeof Raw.down); mzero(Raw.mdown, sizeof Raw.mdown); }
      else if (isFull) plat_video_apply();
      return 0;
    case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT) { SetCursor(Game.state == GS_PLAY && !Game.paused && !U.cur ? NULL : LoadCursor(NULL, IDC_ARROW)); return TRUE; } break;
    case WM_SIZE: if (w != SIZE_MINIMIZED) { cliW = LOWORD(l); cliH = HIWORD(l); blitScaled = 1; needResize = 1; } return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *mm = (MINMAXINFO *)l; mm->ptMinTrackSize.x = 340; mm->ptMinTrackSize.y = 280; return 0; }
    case WM_SYSKEYDOWN: if (w == VK_RETURN) { toggle_fullscreen(); return 0; } if (w == VK_F4) { running = 0; return 0; } break;
    case WM_KEYDOWN: { int k = vk_map(w); Raw.any = 1; if (k >= 0) { if (l & (1 << 30)) Raw.rep[k] = 1; else { Raw.down[k] = 1; Raw.press[k] = 1; Raw.ptime[k] = msgTime(); } } if (w == VK_F11) toggle_fullscreen(); return 0; }
    case WM_KEYUP: { int k = vk_map(w); if (k >= 0) Raw.down[k] = 0; return 0; }
    case WM_MOUSEMOVE: mouse_pos(l); return 0;
    case WM_LBUTTONDOWN: mouse_pos(l); Raw.mdown[0] = Raw.medge[0] = 1; Raw.mtime[0] = msgTime(); Raw.any = 1; SetCapture(h); return 0;
    case WM_LBUTTONUP: Raw.mdown[0] = 0; ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN: Raw.mdown[1] = Raw.medge[1] = 1; Raw.any = 1; return 0;
    case WM_RBUTTONUP: Raw.mdown[1] = 0; return 0;
    case WM_MBUTTONDOWN: Raw.mdown[2] = Raw.medge[2] = 1; Raw.any = 1; return 0;
    case WM_MBUTTONUP: Raw.mdown[2] = 0; return 0;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); present(dc); EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
  }
  return DefWindowProcA(h, m, w, l);
}
static JOYCAPSA jcaps; static int joyOk;
static void poll_pad(void) {
  Raw.padOk = 0; if (!joyOk) return;
  JOYINFOEX ji; mzero(&ji, sizeof ji); ji.dwSize = sizeof ji; ji.dwFlags = JOY_RETURNALL;
  if (joyGetPosEx(JOYSTICKID1, &ji) != JOYERR_NOERROR) return;
#define NAX(v, a, b) ((b) > (a) ? ((float)(v) - (a)) / ((float)(b) - (a)) * 2 - 1 : 0)
  Raw.ax[0] = NAX(ji.dwXpos, jcaps.wXmin, jcaps.wXmax); Raw.ax[1] = NAX(ji.dwYpos, jcaps.wYmin, jcaps.wYmax);
  Raw.ax[2] = jcaps.wNumAxes >= 5 ? NAX(ji.dwUpos, jcaps.wUmin, jcaps.wUmax) : jcaps.wNumAxes >= 4 ? NAX(ji.dwZpos, jcaps.wZmin, jcaps.wZmax) : 0;
  Raw.ax[3] = jcaps.wNumAxes >= 4 ? NAX(ji.dwRpos, jcaps.wRmin, jcaps.wRmax) : 0;
  Raw.ax[4] = jcaps.wNumAxes >= 5 ? NAX(ji.dwZpos, jcaps.wZmin, jcaps.wZmax) : 0;
#undef NAX
  Raw.btn = ji.dwButtons; Raw.padOk = 1;
}
static int benchArg, simdArg = -1;
static void plat_bench_done(const char *text) {
  char path[MAX_PATH]; s_cpy(path, savePath); int n = s_len(path); while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/') n--; s_cpy(path + n, "PHASE95_BENCH.TXT");
  int ok = file_write(path, text, s_len(text));
  if (isFull && modeChanged) { ChangeDisplaySettingsA(NULL, 0); modeChanged = 0; }
  ShowWindow(hwnd, SW_MINIMIZE);
  char msg[MAX_PATH + 64]; s_printf(msg, ok ? "Component benchmark written to\n%s" : "Could not write %s", path);
  MessageBoxA(NULL, msg, "PHASE", MB_OK | MB_ICONINFORMATION);
  ShowWindow(hwnd, SW_RESTORE); if (isFull) plat_video_apply();
}
static int app_main(const char *cmd) {
  InitializeCriticalSection(&snd_cs); /* before anything can queue a sound */
  LARGE_INTEGER f; useQpc = QueryPerformanceFrequency(&f) && f.QuadPart; if (useQpc) { qpf = (double)f.QuadPart; QueryPerformanceCounter(&qpc0); } else t0 = timeGetTime() / 1000.0;
  timeBeginPeriod(1);
  rng_s ^= (u32)GetTickCount() * 2654435761u; if (!rng_s) rng_s = 1;
  save_paths();
  save_read();
#ifdef PH_TEST_XP
  if (Save.bank < (i64)PH_TEST_XP) { Save.bank = (i64)PH_TEST_XP; save_write(); }   /* test build: XP topped up at every launch */
#endif
  for (const char *c = cmd; c && *c; c++) { if (c[0] == '-' && c[1] == 'w') forceWindowed = 1; if (c[0] == '-' && c[1] == 'b' && c[2] == 'e') benchArg = 1; if (c[0] == '-' && c[1] == 's' && c[2] == 'i' && c[5] == '=' && c[6] >= '0' && c[6] <= '2') simdArg = c[6] - '0'; if (c[0] == '-' && c[1] == 'd') debugKeys = 1; }
  HINSTANCE inst = GetModuleHandleA(NULL);
  WNDCLASSA wc; mzero(&wc, sizeof wc); wc.style = CS_OWNDC; wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); wc.lpszClassName = "PHASE95";
  RegisterClassA(&wc);
  RECT r = { 0, 0, 640, 480 }; DWORD st = WS_OVERLAPPEDWINDOW; AdjustWindowRect(&r, st, FALSE);
  hwnd = CreateWindowA("PHASE95", PH_TITLE, st, 40, 40, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
  if (!hwnd) return 1;
  mx_init(22050);
  joyOk = joyGetNumDevs() > 0 && joyGetDevCapsA(JOYSTICKID1, &jcaps, sizeof jcaps) == JOYERR_NOERROR;
  workers_start();
  cpu_detect(); plat_os_detect(); simd_choose(simdArg); pr_start();
  game_boot();
  audioOk = audio_open();
  snd_init(); if (!audioOk) S.ok = 0;
  plat_video_apply();
  if (benchArg) cb_start(); else if (!Save.benchDone) bench_start(plat_time());
  double last = plat_time(), next = last;
  while (running) {
    MSG msg; while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) running = 0; TranslateMessage(&msg); DispatchMessageA(&msg); }
    if (!running) break;
    if (!active && isFull) { Sleep(40); last = next = plat_time(); continue; }
    if (needResize) { RECT c; GetClientRect(hwnd, &c); needResize = 0; video_resize(c.right, c.bottom); }
    if (!audioOk) { SND_LOCK(); aud_rt = plat_time(); aud_wall = aud_rt; SND_UNLOCK(); }
    poll_pad();
    comp_tick(); PROF(C_FRAME);
    double now = plat_time(); float raw = (float)(now - last); last = now;
    game_frame(raw);
    if (prOn && prBits[1]) frame_show(); else { PROF(C_PRESENT); frame_show(); PROFE(C_PRESENT); }
    PROFE(C_FRAME);
    perf_update((float)(plat_time() - now));
    bench_frame(plat_time()); cb_frame();
    /* frame limiter: sleep to within 2 ms, then spin for an exact deadline (30-1000 fps); unlimited and the
       benchmark don't wait at all, so input is read and the frame shown as soon as possible */
    int hz = rate_hz(Set.rate);
    if (Bench.on || CBm.on || (active && !hz)) next = plat_time();
    else {
      double period = !active ? 1.0 / 15 : 1.0 / hz;   /* a background window idles at 15 fps */
      next += period; double wait = next - plat_time();
      if (wait < -0.1) next = plat_time();
      else if (wait > 0) { if (wait > 0.003) Sleep((DWORD)((wait - 0.002) * 1000)); while (plat_time() < next) { } }
    }
  }
  running = 0;
  save_write();
  if (modeChanged) ChangeDisplaySettingsA(NULL, 0);
  if (audioOk) { waveOutReset(wo); waveOutClose(wo); }
  timeEndPeriod(1);
  return 0;
}
void __cdecl WinMainCRTStartup(void) {
  const char *cmd = GetCommandLineA();
  ExitProcess((UINT)app_main(cmd));
}
#endif

/* =========================================================================
   PLATFORM: HEADLESS TEST HARNESS (Linux/macOS)
   Runs the real game loop on a simulated 60 Hz clock, renders frames to PPM
   and the mixed audio to WAV. A pilot plays (the attract-mode AI), so whole
   sectors and guardians can be exercised and inspected without a display.
     ./phase95 --scene play --wave 5 --frames 1800 --shot 300 --out shots --wav run.wav
   ========================================================================= */
#if defined(PH_HEADLESS)
static double hl_time;
static double plat_time(void) { return hl_time; }
static float *mx_alloc(int n) { return calloc(n + 4, sizeof(float)); }
static const char *hl_save = "phase95_test.sav";
static int plat_save(const void *p, int n) { FILE *f = fopen(hl_save, "wb"); if (!f) return 0; int w = (int)fwrite(p, 1, n, f); fclose(f); return w == n; }
static int plat_load(void *p, int n) { FILE *f = fopen(hl_save, "rb"); if (!f) return 0; int r = (int)fread(p, 1, n, f); fclose(f); return r; }
static u32 plat_date(void) { time_t t = time(NULL); struct tm *m = localtime(&t); return (m->tm_year + 1900) * 10000u + (m->tm_mon + 1) * 100u + m->tm_mday; }
static void plat_video_apply(void) {}
static double plat_clock(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void plat_bench_done(const char *text) { fputs(text, stdout); }
static void plat_res_info(int res, int *w, int *h) { (void)res; *w = OW; *h = OH; }
static void *plat_alloc(size_t n) { return calloc(1, n); }
static void plat_free(void *p) { free(p); }
/* worker pool: one semaphore per thread for the sleep side of the shared spin-then-sleep handoff */
static sem_t wkSem[NWMAX];
static void wk_sleep(int i) { while (sem_wait(&wkSem[i]) != 0) {} }
static void wk_wake(int i) { sem_post(&wkSem[i]); }
static void *worker(void *p) { worker_loop((int)(intptr_t)p); return NULL; }
static void workers_start(int n) {
  if (n > NWMAX) n = NWMAX; nWork = n < 1 ? 1 : n; if (nWork < 2) return;
  for (int i = 0; i < nWork; i++) sem_init(&wkSem[i], 0, 0);
  for (int i = 1; i < nWork; i++) { pthread_t t; pthread_create(&t, NULL, worker, (void *)(intptr_t)i); pthread_detach(t); }
}
static double wall(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void write_ppm(const char *path) {
  FILE *f = fopen(path, "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", OW, OH);
  u8 *row = malloc(OW * 3);
  for (int y = 0; y < OH; y++) {
    for (int x = 0; x < OW; x++) {
      u8 *o = row + x * 3;
      if (fmt32) { u32 v = OB32[(size_t)y * OW + x]; o[0] = (u8)(v >> 16); o[1] = (u8)(v >> 8); o[2] = (u8)v; }
      else { u16 v = OB16[(size_t)y * OW + x]; o[0] = (u8)((v >> 11) * 255 / 31); o[1] = (u8)(((v >> 5) & 63) * 255 / 63); o[2] = (u8)((v & 31) * 255 / 31); }
    }
    fwrite(row, 1, OW * 3, f);
  }
  free(row); fclose(f);
}
static void wav_header(FILE *f, int frames) {
  int bytes = frames * 4; u32 v;
  fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
  u16 s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f); v = SR; fwrite(&v, 4, 1, f); v = SR * 4; fwrite(&v, 4, 1, f); s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
  fwrite("data", 1, 4, f); v = bytes; fwrite(&v, 4, 1, f);
}
/* a simple pilot for play testing: the attract-mode AI, dashing on beats */
static void hl_bot(void) {
  if (Game.state != GS_PLAY || Game.paused || !Game.p.alive) return;
  InputState keep = In; game_demoInput(); In.slow = keep.slow; In.pause = 0;
  if (Game.p.hp == 1 && Game.p.bombs > 0 && Game.nen > 30 && chance(0.01f)) In.bomb = 1;
}
int main(int argc, char **argv) {
  int frames = 600, wave = 1, shot = 0, god = 0, quality = 0, ow = 640, oh = 480, f32 = 0, threads = 1, rate = 60, tskip = 30, bench = 0, keep = 0, compb = 0, simdcap = -1; const char *scn = "title", *out = ".", *wavp = NULL; int sr = 22050;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--wave") && i + 1 < argc) wave = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--scene") && i + 1 < argc) scn = argv[++i];
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wavp = argv[++i];
    else if (!strcmp(argv[i], "--quality") && i + 1 < argc) quality = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--sr") && i + 1 < argc) sr = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--god")) god = 1;
    else if (!strcmp(argv[i], "--res") && i + 1 < argc) { char *x; ow = (int)strtol(argv[++i], &x, 10); if (*x) oh = atoi(x + 1); }
    else if (!strcmp(argv[i], "--fmt32")) f32 = 1;
    else if (!strcmp(argv[i], "--bench")) bench = 1;
    else if (!strcmp(argv[i], "--keepsave")) keep = 1;
    else if (!strcmp(argv[i], "--compbench")) compb = 1;
    else if (!strcmp(argv[i], "--simd") && i + 1 < argc) simdcap = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--skip") && i + 1 < argc) tskip = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--rate") && i + 1 < argc) rate = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) rng_s = (u32)atoi(argv[++i]) | 1;
  }
  int playScene = !strcmp(scn, "play") || !strcmp(scn, "upgrade") || !strcmp(scn, "over") || !strcmp(scn, "pause");
  if (!keep) remove(hl_save);
  save_read();
  if (keep) { printf("save: ver %u  bench %d (q%d, %d fps)  res %d  full %d  rate %d  quality %d  offset %d  music %d  bank %lld  records %d\n", Save.ver, Save.benchDone, Save.benchQ, Save.benchFps, Set.res, Set.fullscreen, Set.rate, Set.quality, Set.offset, Set.music, (long long)Save.bank, Save.nrec); if (!quality) quality = Set.quality; }
  else { Save.bank = 300000000; Save.levels = 0xFFFFFFFFu; }
  Set.quality = (u8)quality;
  Set.rate = rate == 30 ? 1 : rate == 120 ? 2 : rate == 240 ? 3 : rate == 1000 ? 4 : rate == 0 ? 5 : 0; if (!rate) rate = 60;
  mx_init(sr); aud_blk = 1.f / rate;
  workers_start(threads);
  cpu_detect(); Cpu.nt = 1; Cpu.osSse = 1; Cpu.logical = (int)sysconf(_SC_NPROCESSORS_ONLN); simd_choose(simdcap);
  game_boot(); snd_init();
  ow = (ow + 3) & ~3; oh = (oh + 3) & ~3; if (ow > OWMAX) ow = OWMAX; if (oh > OHMAX) oh = OHMAX;
  fmt32 = f32; outbuf = calloc((size_t)ow * oh, f32 ? 4 : 2);
  video_changed(ow, oh);
  if (bench) { bench_start(wall()); scn = "boot"; }
  if (compb) { cb_start(); scn = "boot"; frames = 1 << 30; }
  if (playScene) frame_hook = hl_bot;
  FILE *wf = NULL; int wframes = 0; if (wavp) { wf = fopen(wavp, "wb"); wav_header(wf, 0); }
  static short abuf[44100 * 2];
  double tsum = 0; int tcount = 0;
  for (int f = 0; f < frames; f++) {
    if (f == 30 && strcmp(scn, "boot")) {
      ui_toTitle();
      if (!strcmp(scn, "settings")) ui_show(SC_SETTINGS); else if (!strcmp(scn, "levels")) ui_show(SC_LEVELS); else if (!strcmp(scn, "bonus")) ui_show(SC_BONUS);
      else if (!strcmp(scn, "how")) ui_show(SC_HOW); else if (!strcmp(scn, "records")) ui_show(SC_RECORDS);
      else if (playScene) { ui_hide(); game_newRun(wave, 0); Game.god = god; }
    }
    if (!strcmp(scn, "upgrade") && f == frames - 90 && Game.state == GS_PLAY) { Game.afterBoss = 1; game_openUpgrades(); }
    if (!strcmp(scn, "pause") && f == frames - 30 && Game.state == GS_PLAY) ui_pause();
    if (!strcmp(scn, "over") && f == frames - 360 && Game.state == GS_PLAY) { Game.god = 0; Game.p.inv = 0; Game.p.hp = 1; Game.p.dashT = 0; player_hurt(); }
    double c0 = wall(), fdt = 1.0 / rate;
    double target = (f + 1) * fdt; int n = (int)((target * SR - Mx.pos) / CTRL) * CTRL;
    if (n > 0) { double a0 = wall(); mx_render(abuf, n); if (compOn) compAudio += wall() - a0; if (wf) { fwrite(abuf, 4, n, wf); wframes += n; } }
    aud_rt = (double)Mx.pos / SR; hl_time = f * fdt;
    if (!compb) { compOn = 1; if (f == tskip) mzero(compT, sizeof compT); }
    PROF(C_FRAME); game_frame((float)fdt); PROFE(C_FRAME);
    if (f >= tskip) { tsum += wall() - c0; tcount++; }
    if (bench) { int was = Bench.on; bench_frame(wall()); if (was && !Bench.on) printf("benchmark: %.1f fps at %dx%d, scene %dx%d -> quality %d\n", Bench.fps, OW, OH, RW, RH, Set.quality); }
    if (compb) { cb_frame(); if (!CBm.on) break; }
    if (shot && (f + 1) % shot == 0) { char p[256]; snprintf(p, sizeof p, "%s/f%05d.ppm", out, f + 1); write_ppm(p); }
  }
  if (wf) { fseek(wf, 0, SEEK_SET); wav_header(wf, wframes); fclose(wf); }
  printf("frames %d  avg %.2f ms/frame  out %dx%d%s  scene %dx%d  state %d  wave %d  score %lld  enemies %d  particles %d  alive %d  q %d\n", tcount, tsum / (tcount ? tcount : 1) * 1000, OW, OH, fmt32 ? "x32" : "x16", RW, RH, Game.state, Game.wave, (long long)Game.score, Game.nen, pLive, Game.p.alive, Perf.level);
  if (tcount && !compb) { printf("  ms/frame:"); for (int c = 0; c < NCOMP; c++) if (c != C_PRESENT) printf("  %s %.3f", COMP_NAME[c], compT[c] / tcount * 1000); printf("  audio %.1f%%\n", compAudio / (tcount / (double)rate) * 100); }
  return 0;
}
#endif
