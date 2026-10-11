/*
 * DanzGL shim - hook (stage 1: TELEMETRI SAJA, belum mengubah gambar).
 *
 * Aktif hanya jika:
 *   - nama proses ada di kWhitelist, DAN
 *   - property debug.danzgl.mode >= 1  (default 0 = passthrough murni)
 *
 * Yang dicatat tiap 5 detik ke logcat (tag DanzGL): fps, histogram ukuran glViewport,
 * jumlah glBindFramebuffer (fbo 0 vs lainnya), jumlah glFramebufferTexture2D.
 * Gunanya: tahu game benar-benar memanggil apa dan di resolusi berapa, sebelum
 * menulis MSAA / supersampling.
 *
 * Catatan desain: fungsi hook sebenarnya adalah h_* (hidden). Nama ekspor (glViewport dst)
 * hanya pembungkus tipis ke h_*, dan tabel kHook menunjuk ke h_* langsung, supaya tidak
 * pernah terselesaikan ke simbol bernama sama di library lain (mis. libGLESv2.so).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>

#include "dz.h"

typedef uint32_t EGLBoolean;
typedef int32_t GLint;
typedef int32_t GLsizei;
typedef uint32_t GLenum;
typedef uint32_t GLuint;

static const char *const kWhitelist[] = {"com.mobile.legends", NULL};

/* State PER PROSES. Driver GL sudah dimuat di zygote (preloadOpenGL) lalu proses app di-fork,
 * jadi constructor tidak jalan lagi di app dan nama proses saat itu masih "zygote64".
 * Karena itu whitelist dicek malas: tiap panggilan hook membandingkan pid; kalau berubah
 * (proses baru / anak fork), keputusan dihitung ulang dari cmdline yang sudah final. */
static volatile int g_active;
static volatile pid_t g_pid;
static volatile int g_initlock;

/* ---------- pointer ke fungsi asli ---------- */
typedef void *(*fn_egpa)(const char *);
typedef EGLBoolean (*fn_swap)(void *, void *);
typedef void (*fn_viewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (*fn_bindfb)(GLenum, GLuint);
typedef void (*fn_fbtex2d)(GLenum, GLenum, GLenum, GLuint, GLint);

static fn_egpa r_egpa;
static fn_swap r_swap;
static fn_viewport r_viewport;
static fn_bindfb r_bindfb;
static fn_fbtex2d r_fbtex2d;

#define REAL(var, name) ((var) ? (var) : ((var) = (__typeof__(var))dz_real_sym(name)))

/* ---------- statistik ---------- */
#define HN 16
struct hist_ent {
    int32_t w, h;
    uint32_t n;
};
static struct hist_ent g_hist[HN];
static uint32_t g_hist_over;
static volatile int g_hlock;

static uint32_t g_frames, g_bind0, g_bind1, g_attach;
static uint64_t g_t0;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void hist_lock(void) {
    while (__sync_lock_test_and_set(&g_hlock, 1)) {
    }
}
static void hist_unlock(void) { __sync_lock_release(&g_hlock); }

static void hist_add(int w, int h) {
    hist_lock();
    int placed = 0;
    for (int i = 0; i < HN; i++) {
        if (g_hist[i].n && g_hist[i].w == w && g_hist[i].h == h) {
            g_hist[i].n++;
            placed = 1;
            break;
        }
    }
    if (!placed) {
        for (int i = 0; i < HN; i++) {
            if (!g_hist[i].n) {
                g_hist[i].w = w;
                g_hist[i].h = h;
                g_hist[i].n = 1;
                placed = 1;
                break;
            }
        }
    }
    if (!placed) g_hist_over++;
    hist_unlock();
}

static void flush_window(uint64_t t) {
    double secs = (double)(t - g_t0) / 1e9;
    if (secs <= 0.0) secs = 1.0;
    uint32_t fr = __atomic_exchange_n(&g_frames, 0, __ATOMIC_RELAXED);
    uint32_t b0 = __atomic_exchange_n(&g_bind0, 0, __ATOMIC_RELAXED);
    uint32_t b1 = __atomic_exchange_n(&g_bind1, 0, __ATOMIC_RELAXED);
    uint32_t at = __atomic_exchange_n(&g_attach, 0, __ATOMIC_RELAXED);

    struct hist_ent snap[HN];
    hist_lock();
    memcpy(snap, g_hist, sizeof(snap));
    memset(g_hist, 0, sizeof(g_hist));
    uint32_t over = g_hist_over;
    g_hist_over = 0;
    hist_unlock();

    char vp[192];
    size_t off = 0;
    vp[0] = '\0';
    for (int k = 0; k < 4; k++) {
        int best = -1;
        for (int i = 0; i < HN; i++)
            if (snap[i].n && (best < 0 || snap[i].n > snap[best].n)) best = i;
        if (best < 0) break;
        int w = snprintf(vp + off, sizeof(vp) - off, "%s%dx%d:%u", off ? " " : "",
                         snap[best].w, snap[best].h, snap[best].n);
        if (w < 0 || (size_t)w >= sizeof(vp) - off) break;
        off += (size_t)w;
        snap[best].n = 0;
    }

    dz_log("proc=%s fps=%.1f frames=%u bindFB(fbo0/lain)=%u/%u attachTex2D=%u viewport[%s]%s",
           dz_proc_name(), fr / secs, fr, b0, b1, at, vp[0] ? vp : "-",
           over ? " (histogram penuh)" : "");
}

/* ---------- keputusan aktif/mati per proses ---------- */
static int is_listed(const char *proc) {
    for (int i = 0; kWhitelist[i]; i++)
        if (!strcmp(proc, kWhitelist[i])) return 1;
    return 0;
}

static void reinit_slow(pid_t pid) {
    while (__sync_lock_test_and_set(&g_initlock, 1)) {
    }
    if (g_pid != pid) {
        dz_proc_refresh();
        const char *proc = dz_proc_name();
        int listed = is_listed(proc);
        int mode = 0, plen = 0;
        if (listed) {
            char val[PROP_VALUE_MAX] = {0};
            plen = __system_property_get("debug.danzgl.mode", val);
            mode = atoi(val);
        }

        /* statistik milik proses ini: buang warisan dari proses induk */
        __atomic_store_n(&g_frames, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&g_bind0, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&g_bind1, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&g_attach, 0, __ATOMIC_RELAXED);
        g_t0 = 0;
        hist_lock();
        memset(g_hist, 0, sizeof(g_hist));
        g_hist_over = 0;
        hist_unlock();

        g_active = 0;
        if (listed && mode >= 1) {
            REAL(r_egpa, "eglGetProcAddress");
            REAL(r_swap, "eglSwapBuffers");
            REAL(r_viewport, "glViewport");
            REAL(r_bindfb, "glBindFramebuffer");
            REAL(r_fbtex2d, "glFramebufferTexture2D");
            g_active = 1;
        }
        if (listed)
            dz_log("hooks %s: proc=%s pid=%d mode=%d (panjang prop=%d)", g_active ? "AKTIF" : "mati",
                   proc, (int)pid, mode, plen);
        __atomic_store_n(&g_pid, pid, __ATOMIC_RELEASE);
    }
    __sync_lock_release(&g_initlock);
}

static inline void ensure_init(void) {
    pid_t pid = getpid();
    if (__builtin_expect(__atomic_load_n(&g_pid, __ATOMIC_ACQUIRE) != pid, 0)) reinit_slow(pid);
}

/* ---------- hook (h_*) ---------- */
DZ_HIDDEN EGLBoolean h_eglSwapBuffers(void *dpy, void *surf) {
    ensure_init();
    fn_swap r = REAL(r_swap, "eglSwapBuffers");
    if (g_active) {
        __atomic_add_fetch(&g_frames, 1, __ATOMIC_RELAXED);
        uint64_t t = now_ns();
        if (!g_t0) {
            g_t0 = t;
        } else if (t - g_t0 >= 5000000000ull) {
            flush_window(t);
            g_t0 = t;
        }
    }
    return r ? r(dpy, surf) : 0;
}

DZ_HIDDEN void h_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    ensure_init();
    fn_viewport r = REAL(r_viewport, "glViewport");
    if (g_active) hist_add(w, h);
    if (r) r(x, y, w, h);
}

DZ_HIDDEN void h_glBindFramebuffer(GLenum target, GLuint fb) {
    ensure_init();
    fn_bindfb r = REAL(r_bindfb, "glBindFramebuffer");
    if (g_active) __atomic_add_fetch(fb ? &g_bind1 : &g_bind0, 1, __ATOMIC_RELAXED);
    if (r) r(target, fb);
}

DZ_HIDDEN void h_glFramebufferTexture2D(GLenum target, GLenum att, GLenum textarget, GLuint tex,
                                        GLint level) {
    ensure_init();
    fn_fbtex2d r = REAL(r_fbtex2d, "glFramebufferTexture2D");
    if (g_active) __atomic_add_fetch(&g_attach, 1, __ATOMIC_RELAXED);
    if (r) r(target, att, textarget, tex, level);
}

static const struct {
    const char *name;
    void *fn;
} kHook[] = {
    {"eglSwapBuffers", (void *)h_eglSwapBuffers},
    {"glViewport", (void *)h_glViewport},
    {"glBindFramebuffer", (void *)h_glBindFramebuffer},
    {"glFramebufferTexture2D", (void *)h_glFramebufferTexture2D},
};

DZ_HIDDEN void *h_eglGetProcAddress(const char *name) {
    ensure_init();
    fn_egpa r = REAL(r_egpa, "eglGetProcAddress");
    if (!r) return NULL;
    void *p = r(name);
    if (p && g_active && name) {
        for (unsigned i = 0; i < sizeof(kHook) / sizeof(kHook[0]); i++)
            if (!strcmp(kHook[i].name, name)) return kHook[i].fn;
    }
    return p;
}

/* ---------- nama yang diekspor (pembungkus tipis) ---------- */
DZ_EXPORT void *eglGetProcAddress(const char *name) { return h_eglGetProcAddress(name); }
DZ_EXPORT EGLBoolean eglSwapBuffers(void *dpy, void *surf) { return h_eglSwapBuffers(dpy, surf); }
DZ_EXPORT void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { h_glViewport(x, y, w, h); }
DZ_EXPORT void glBindFramebuffer(GLenum target, GLuint fb) { h_glBindFramebuffer(target, fb); }
DZ_EXPORT void glFramebufferTexture2D(GLenum target, GLenum att, GLenum textarget, GLuint tex,
                                      GLint level) {
    h_glFramebufferTexture2D(target, att, textarget, tex, level);
}
