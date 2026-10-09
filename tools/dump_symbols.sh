#!/data/data/com.termux/files/usr/bin/sh
# Jalankan di Termux (butuh: pkg install binutils). Mengambil daftar simbol driver Mali ASLI.
# Hasil: symbols/mali_symbols.txt  (hanya NAMA simbol, bukan binary driver -> aman di-commit)
set -e
OUT="${1:-symbols/mali_symbols.txt}"
TMP="$HOME/mali_real.so"
VENDOR=/vendor/lib64/egl/mt6789/libGLES_mali.so
MODREAL=/data/adb/modules/danzgl/system/vendor/lib64/egl/mt6789/libGLES_mali_real.so

SRC=$VENDOR
# kalau modul sudah terpasang, /vendor menampilkan wrapper -> ambil salinan driver asli dari modul
if su -c "grep -c DANZGL_SHIM_MARKER $VENDOR" 2>/dev/null | grep -qv '^0$'; then
  echo "[dump] modul terpasang, memakai salinan driver asli dari modul"
  SRC=$MODREAL
fi

su -c "cat $SRC" > "$TMP"
echo "[dump] ukuran driver: $(wc -c < "$TMP") byte (harusnya ~38 juta)"

mkdir -p "$(dirname "$OUT")"
nm -D --defined-only "$TMP" > "$OUT"
echo "[dump] $(wc -l < "$OUT") simbol -> $OUT"
echo "[dump] ringkasan per tipe (T/W = fungsi, lainnya = data):"
awk '{print $2}' "$OUT" | sort | uniq -c
rm -f "$TMP"
