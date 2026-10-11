# DanzGL Shim (stage 1)

Shim driver GLES untuk Mali-G57 MC2 (mt6789, KernelSU). Menggantikan
`/vendor/lib64/egl/mt6789/libGLES_mali.so`; driver asli disalin modul sebagai
`libGLES_mali_real.so` dan semua fungsi diteruskan lewat trampoline.

**Stage 1 belum mengubah gambar.** Isinya passthrough murni + telemetri (fps, ukuran
viewport, pemakaian FBO) khusus `com.mobile.legends`. Hasil telemetri dipakai untuk
menulis MSAA (`GL_EXT_multisampled_render_to_texture`) atau supersampling di stage 2.

Status uji: generator, kompilasi arm64, link, dan cek ekspor lolos dengan 1010 simbol asli dari
HP (976 fungsi, 5 hook, 971 trampoline). Logika runtime (muat driver asli, penerusan, whitelist,
mode 0, telemetri, driver asli hilang) lolos di host x86 dengan driver palsu.
**Belum diuji di perangkat.** Build asli lewat NDK r26d di GitHub Actions.

## Alur

1. `symbols/mali_symbols.txt` sudah terisi (dump dari POCO M5-mu). Kalau driver vendor berubah
   (OTA), ulangi: `sh tools/dump_symbols.sh`.
2. Di Termux, cek dulu bahwa 5 simbol DATA driver (`HMI`, `g_counting_index_*`, `prof_buffer_*`)
   tidak dipakai library lain (wrapper tidak bisa meneruskan data, hanya fungsi):
   ```
   sh tools/check_data_users.sh
   ```
   Harus `OK`. Kalau ada baris `!!`, kirim hasilnya ke Claude sebelum lanjut.
3. Push repo ke GitHub (symbols ikut). Actions `build-danzgl` jalan, unduh artifact
   `DanzGL-module` (zip modul).
4. KernelSU Manager, Modul, Pasang dari penyimpanan, reboot.
5. Tes boot dulu dengan mode 0 (default). Kalau normal, aktifkan telemetri:
   ```
   su -c 'echo 1 > /data/adb/danzgl/mode'
   ```
   Reboot, buka MLBB, lalu:
   ```
   su -c 'logcat -d -s DanzGL'
   ```

## Catatan: driver dimuat di zygote

Android memuat driver GL sekali di zygote lalu mem-fork proses app. Karena itu whitelist TIDAK
boleh dicek di constructor (nama proses saat itu masih `zygote64`). Hook memeriksa `getpid()` di
tiap panggilan dan menghitung ulang keputusan (whitelist, mode, statistik) kalau pid berubah.
Baris `hooks AKTIF: proc=com.mobile.legends pid=... mode=...` muncul di logcat begitu game
memanggil GL pertama kali.

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
