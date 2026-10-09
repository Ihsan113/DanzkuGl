#!/usr/bin/env python3
"""
check_exports.py - pastikan wrapper hasil build mengekspor SEMUA simbol fungsi driver asli
dan SONAME-nya libGLES_mali.so. Dipakai di CI sebelum zip dibuat.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

LINE_RE = re.compile(r"^(?:[0-9a-fA-F]+\s+)?([A-Za-z?])\s+(\S+)\s*$")


def names_from_nm(text, only_funcs):
    out = set()
    for raw in text.splitlines():
        m = LINE_RE.match(raw.strip())
        if not m:
            continue
        typ, name = m.group(1), m.group(2).split("@", 1)[0]
        if only_funcs and typ not in "TtWwi":
            continue
        out.add(name)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--symbols", required=True)
    ap.add_argument("--so", required=True)
    ap.add_argument("--nm", default="llvm-nm")
    ap.add_argument("--readelf", default="llvm-readelf")
    a = ap.parse_args()

    expected = names_from_nm(Path(a.symbols).read_text(), True)
    got_txt = subprocess.run([a.nm, "-D", "--defined-only", a.so],
                             capture_output=True, text=True, check=True).stdout
    got = names_from_nm(got_txt, False)

    missing = sorted(expected - got)
    extra = sorted(n for n in got - expected)
    print(f"[check] diharapkan={len(expected)} diekspor={len(got)}")
    if extra:
        print(f"[check] ekspor tambahan (normal: dz_shim_marker dsb): {extra}")
    if missing:
        print(f"[check] GAGAL: {len(missing)} simbol hilang, contoh: {missing[:10]}")
        return 1

    dyn = subprocess.run([a.readelf, "-d", a.so], capture_output=True, text=True, check=True).stdout
    if "[libGLES_mali.so]" not in dyn:
        print("[check] GAGAL: SONAME bukan libGLES_mali.so")
        return 1
    bad_needed = [l for l in dyn.splitlines()
                  if "NEEDED" in l and re.search(r"lib(EGL|GLESv[123])", l)]
    if bad_needed:
        print(f"[check] GAGAL: wrapper bergantung ke libEGL/libGLESv*, bisa deadlock: {bad_needed}")
        return 1
    print("[check] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
