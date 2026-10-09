#!/system/bin/sh
# Pengaman boot (best effort).
# - Kalau boot sebelumnya tidak sampai selesai (guard masih ada) -> modul menonaktifkan dirinya.
# - Kalau vendor berubah (OTA) -> salinan driver asli basi -> nonaktifkan modul.
MODDIR=${0%/*}
GUARD=/data/adb/danzgl_boot_pending

if [ -f "$GUARD" ]; then
  touch "$MODDIR/disable"
  rm -f "$GUARD"
  exit 0
fi

CUR=$(getprop ro.vendor.build.fingerprint)
[ -z "$CUR" ] && CUR=$(getprop ro.build.fingerprint)
SAVED=$(cat "$MODDIR/fp.txt" 2>/dev/null)
if [ -n "$SAVED" ] && [ "$CUR" != "$SAVED" ]; then
  touch "$MODDIR/disable"
  exit 0
fi

touch "$GUARD"
