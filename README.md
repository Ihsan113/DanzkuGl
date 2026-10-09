# DanzGL Shim (stage 1)

Shim driver GLES untuk Mali-G57 MC2 (mt6789, KernelSU). Menggantikan
`/vendor/lib64/egl/mt6789/libGLES_mali.so`; driver asli disalin modul sebagai
`libGLES_mali_real.so` dan semua fungsi diteruskan lewat trampoline.

**Stage 1 belum mengubah gambar.** Isinya passthrough murni + telemetri (fps, ukuran
viewport, pemakaian FBO) khusus `com.mobile.legends`. Hasil telemetri dipakai untuk
menulis MSAA (`GL_EXT_multisampled_render_to_texture`) atau supersampling di stage 2.

Status uji: generator, kompilasi, link, dan cek ekspor lolos di host dengan header stub.
**Belum diuji di perangkat.** Build asli lewat NDK r26d di GitHub Actions.

## Alur

1. Di Termux (sudah `pkg install binutils zip`):
   ```
   sh tools/dump_symbols.sh
   ```
   Lihat ringkasan tipe. Kalau ada tipe selain `T`/`W`, kirim daftarnya dulu
   (generator sengaja berhenti, karena simbol data tidak bisa di-trampoline).
2. Commit `symbols/mali_symbols.txt` (hanya nama simbol, bukan binary) lalu push ke GitHub.
3. Actions `build-danzgl` jalan, unduh artifact `DanzGL-module` (zip modul).
4. KernelSU Manager, Modul, Pasang dari penyimpanan, reboot.
5. Tes boot dulu dengan mode 0 (default). Kalau normal, aktifkan telemetri:
   ```
   su -c 'echo 1 > /data/adb/danzgl/mode'
   ```
   Reboot, buka MLBB, lalu:
   ```
   su -c 'logcat -d -s DanzGL'
   ```

## Pengaman

- Boot guard: kalau boot sebelumnya tidak selesai, modul menonaktifkan dirinya (best effort,
  bisa butuh sampai 2 kali reboot).
- Deteksi OTA: fingerprint vendor berubah, modul nonaktif sendiri (salinan driver asli basi).
- Manual: tahan Volume Bawah saat boot (safe mode KSU), atau hapus `/data/adb/modules/danzgl`
  dari recovery.
- Setelah modul menonaktifkan diri: hapus file `disable` di folder modul atau aktifkan lagi
  lewat KSU Manager, dan pasang ulang bila OTA.

## Catatan teknis

- Wrapper tidak boleh link ke libEGL/libGLESv*; CI menolak build kalau ada.
- Hook diekspor sebagai pembungkus tipis dan juga dikembalikan lewat `eglGetProcAddress`,
  supaya jalur `dlsym` maupun jalur loader sama-sama tertangkap.
- Whitelist proses ada di `src/hooks.c` (`kWhitelist`). Mode dibaca dari property
  `debug.danzgl.mode` saat proses start, jadi restart game setelah mengubah mode.
- Yang belum terbukti: bagaimana `libGLES_meow.so` membuka Mali dan apakah ia membaca struktur
  internal driver lewat `MEOW_get_raw_ddk_hook`. Itu yang diuji oleh tes boot mode 0.
