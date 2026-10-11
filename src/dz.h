#ifndef DZ_H
#define DZ_H

#define DZ_EXPORT __attribute__((visibility("default"), used))
#define DZ_HIDDEN __attribute__((visibility("hidden")))

DZ_HIDDEN void dz_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* dlsym ke driver Mali asli (NULL kalau driver asli gagal dimuat) */
DZ_HIDDEN void *dz_real_sym(const char *name);
/* nama proses (isi /proc/self/cmdline). Nilainya BASI di proses hasil fork dari zygote
 * sampai dz_proc_refresh() dipanggil (nama proses baru diganti setelah fork). */
DZ_HIDDEN const char *dz_proc_name(void);
DZ_HIDDEN void dz_proc_refresh(void);

#endif
