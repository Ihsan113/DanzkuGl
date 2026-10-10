#!/data/data/com.termux/files/usr/bin/sh
# Jalankan di Termux. Memeriksa apakah ada library sistem/vendor yang memakai 5 simbol DATA
# driver Mali (yang tidak bisa diteruskan wrapper). Kalau tidak ada yang memakai, aman dilewati.
# Butuh: pkg install binutils
NAMES="HMI g_counting_index_ex g_counting_index_in prof_buffer_exclude_switch_time prof_buffer_include_switch_time"
PAT=$(echo $NAMES | tr ' ' '|')
TMP="$HOME/chk_lib.so"
FOUND=0

LIBS="/vendor/lib64/egl/libGLES_meow.so /vendor/lib64/egl/libMEOW_data.so /vendor/lib64/egl/libMEOW_gift.so \
/vendor/lib64/egl/libMEOW_qt.so /vendor/lib64/egl/libMEOW_trace.so \
/system/lib64/libEGL.so /system/lib64/libGLESv2.so /system/lib64/libGLESv3.so /system/lib64/libGLESv1_CM.so \
/vendor/lib64/libged.so /vendor/lib64/libgpud.so /vendor/lib64/libgpu_aux.so /vendor/lib64/libgralloc_extra.so"

for f in $LIBS; do
  su -c "[ -f $f ]" 2>/dev/null || continue
  su -c "cat $f" > "$TMP" 2>/dev/null
  # 1) simbol undefined (di-link langsung)   2) string nama utuh (dlsym lewat teks)
  U=$(nm -D --undefined-only "$TMP" 2>/dev/null | awk '{print $NF}' | grep -E "^($PAT)$")
  S=$(grep -a -o -w -E "($PAT)" "$TMP" | sort -u)
  if [ -n "$U$S" ]; then
    FOUND=1
    echo "!! $f"
    [ -n "$U" ] && echo "   undefined: $(echo $U)"
    [ -n "$S" ] && echo "   string   : $(echo $S)"
  fi
done
rm -f "$TMP"

if [ "$FOUND" = 0 ]; then
  echo "OK: tidak ada library yang dicek memakai simbol data tersebut."
else
  echo "ADA pemakai (lihat tanda !!). Kirim hasil ini ke Claude sebelum memasang modul."
fi
