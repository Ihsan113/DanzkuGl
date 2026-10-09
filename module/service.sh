#!/system/bin/sh
# Setelah boot benar-benar selesai: hapus guard, lalu terapkan mode telemetri.
GUARD=/data/adb/danzgl_boot_pending
CFG=/data/adb/danzgl/mode

until [ "$(getprop sys.boot_completed)" = "1" ]; do
  sleep 2
done
sleep 20
rm -f "$GUARD"

# debug.* property hilang tiap reboot, jadi diterapkan lagi dari file.
# Proses game membacanya saat start -> restart game setelah mengubah mode.
if [ -f "$CFG" ]; then
  setprop debug.danzgl.mode "$(cat "$CFG")"
fi
