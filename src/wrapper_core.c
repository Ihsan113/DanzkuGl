/*
 * DanzGL shim - inti wrapper.
 *
 * Library ini dipasang menggantikan /vendor/lib64/egl/mt6789/libGLES_mali.so.
 * Driver Mali asli disalin modul ke libGLES_mali_real.so (folder yang sama).
 * Saat dimuat: dlopen driver asli, isi tabel pointer; trampoline (dz_trampolines.S)
 * lalu tail-jump ke fungsi asli, jadi argumen/varargs/stack tidak tersentuh.
 *
 * PENTING: jangan link ke libEGL / libGLESv* (libEGL-lah yang memuat kita).
 */
#define _GNU_SOURCE
#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dz.h"

#define DZ_TAG "DanzGL"

extern void *dz_real_table[];
extern const char *const dz_sym_names[];
extern const unsigned dz_sym_count;

/* string ini dicari customize.sh (grep) untuk mendeteksi "ini wrapper, bukan driver asli" */
__attribute__((used)) static const char dz_marker_str[] = "DANZGL_SHIM_MARKER_v1";

/* dipakai untuk mendeteksi dlopen yang tidak sengaja memuat wrapper sendiri */
DZ_EXPORT int dz_shim_marker(void) { return 1; }

static void *g_real;
static char g_proc[160];

void dz_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, DZ_TAG, fmt, ap);
    va_end(ap);
}

const char *dz_proc_name(void) { return g_proc; }

void *dz_real_sym(const char *name) { return g_real ? dlsym(g_real, name) : NULL; }

static long dz_missing(void) { return 0; }

static void read_proc_name(void) {
    int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        strcpy(g_proc, "?");
        return;
    }
    ssize_t n = read(fd, g_proc, sizeof(g_proc) - 1);
    close(fd);
    if (n <= 0) strcpy(g_proc, "?");
    else g_proc[n] = '\0'; /* cmdline NUL-terminated per argumen; argv[0] saja yang terbaca sebagai string */
}

static void *try_open(const char *path) {
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        dz_log("dlopen(%s) gagal: %s", path, dlerror());
        return NULL;
    }
    if (dlsym(h, "dz_shim_marker")) {
        dz_log("dlopen(%s) ternyata memuat wrapper sendiri, dilewati", path);
        dlclose(h);
        return NULL;
    }
    return h;
}

__attribute__((constructor(101))) static void dz_init(void) {
    char buf[512];
    Dl_info info;

    read_proc_name();

    /* kandidat 1: folder tempat wrapper ini berada */
    if (dladdr((void *)dz_init, &info) && info.dli_fname && strchr(info.dli_fname, '/')) {
        const char *slash = strrchr(info.dli_fname, '/');
        size_t dirlen = (size_t)(slash - info.dli_fname);
        if (dirlen + 1 + sizeof("libGLES_mali_real.so") < sizeof(buf)) {
            memcpy(buf, info.dli_fname, dirlen);
            strcpy(buf + dirlen, "/libGLES_mali_real.so");
            g_real = try_open(buf);
        }
    }
    /* kandidat 2: path tetap */
    if (!g_real) g_real = try_open("/vendor/lib64/egl/mt6789/libGLES_mali_real.so");

    if (!g_real) {
        dz_log("FATAL: driver asli tidak bisa dimuat (proc=%s). Semua fungsi GL akan return 0.", g_proc);
        for (unsigned i = 0; i < dz_sym_count; i++) dz_real_table[i] = (void *)dz_missing;
        return;
    }

    unsigned miss = 0;
    for (unsigned i = 0; i < dz_sym_count; i++) {
        void *p = dlsym(g_real, dz_sym_names[i]);
        if (!p) {
            miss++;
            p = (void *)dz_missing;
        }
        dz_real_table[i] = p;
    }
    if (miss) dz_log("peringatan: %u simbol tidak ketemu di driver asli (proc=%s)", miss, g_proc);
}
