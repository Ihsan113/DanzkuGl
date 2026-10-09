#!/system/bin/sh
# DanzGL Shim - installer (KernelSU / Magisk).
# Menyalin driver Mali ASLI ke folder modul sebagai libGLES_mali_real.so, lalu wrapper
# (libGLES_mali.so dari zip) menggantikan driver di /vendor lewat mount modul.

SKIPUNZIP=0

EGLDIR=/vendor/lib64/egl/mt6789
ORIG=$EGLDIR/libGLES_mali.so
DEST=$MODPATH/system$EGLDIR
OLDREAL=/data/adb/modules/danzgl/system$EGLDIR/libGLES_mali_real.so
CTX=u:object_r:same_process_hal_file:s0
MINSIZE=5000000   # driver asli ~38 MB, wrapper < 1 MB

ui_print "- DanzGL Shim v0.1 (stage 1: passthrough + telemetri)"

[ "$ARCH" = "arm64" ] || abort "! Hanya arm64 (ARCH=$ARCH)"
[ -f "$DEST/libGLES_mali.so" ] || abort "! wrapper tidak ada di dalam zip"

# --- tentukan sumber driver asli ---
is_wrapper() {
  f="$1"
  [ -f "$f" ] || return 0
  sz=$(wc -c < "$f")
  [ "$sz" -lt "$MINSIZE" ] && return 0
  [ "$(grep -c DANZGL_SHIM_MARKER "$f" 2>/dev/null)" != "0" ] && return 0
  return 1
}

if is_wrapper "$ORIG"; then
  # modul sedang terpasang (reinstall/update) -> /vendor menampilkan wrapper, ambil salinan lama
  ui_print "- /vendor menampilkan wrapper, memakai salinan driver asli dari instalasi sebelumnya"
  is_wrapper "$OLDREAL" && abort "! Driver asli tidak ditemukan. Hapus modul lama, reboot, lalu pasang ulang."
  SRC=$OLDREAL
else
  SRC=$ORIG
fi

ui_print "- Menyalin driver asli dari $SRC"
cp "$SRC" "$DEST/libGLES_mali_real.so" || abort "! gagal menyalin driver asli"
cmp -s "$SRC" "$DEST/libGLES_mali_real.so" || abort "! salinan driver asli tidak identik"
is_wrapper "$DEST/libGLES_mali_real.so" && abort "! salinan driver terlihat seperti wrapper, dibatalkan"

# --- sidik jari vendor, untuk deteksi OTA saat boot ---
FP=$(getprop ro.vendor.build.fingerprint)
[ -z "$FP" ] && FP=$(getprop ro.build.fingerprint)
echo "$FP" > "$MODPATH/fp.txt"
sha256sum "$DEST/libGLES_mali_real.so" | cut -d' ' -f1 > "$MODPATH/real.sha256"

set_perm "$DEST/libGLES_mali.so" 0 0 0644 $CTX
set_perm "$DEST/libGLES_mali_real.so" 0 0 0644 $CTX

mkdir -p /data/adb/danzgl
[ -f /data/adb/danzgl/mode ] || echo 0 > /data/adb/danzgl/mode

ui_print "- Selesai. Reboot untuk mengaktifkan."
ui_print "- Mode default 0 (passthrough). Telemetri: su -c 'echo 1 > /data/adb/danzgl/mode', reboot,"
ui_print "  lalu buka game dan baca: su -c 'logcat -d -s DanzGL'"
ui_print "- Kalau bootloop: tahan Volume Bawah (safe mode KSU) atau hapus /data/adb/modules/danzgl dari recovery."
