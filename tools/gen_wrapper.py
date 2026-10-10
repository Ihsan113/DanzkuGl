#!/usr/bin/env python3
"""
gen_wrapper.py - bikin trampoline arm64 untuk SEMUA simbol fungsi yang diekspor
driver Mali asli, supaya wrapper bisa dipasang sebagai pengganti libGLES_mali.so.

Input  : output `nm -D --defined-only libGLES_mali.so` (format: "<addr> <tipe> <nama>")
Output : <out>/dz_trampolines.S  -> satu trampoline per simbol (tail-jump ke driver asli)
         <out>/dz_names.c        -> daftar nama + tabel pointer ke driver asli

Simbol di --override (hooked_symbols.txt) TIDAK dibuatkan trampoline; simbol itu
harus didefinisikan manual di src/hooks.c.
"""
import argparse
import re
import sys
from pathlib import Path

FUNC_TYPES = set("TtWwi")          # text / weak function / ifunc
NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_.$]*$")
LINE_RE = re.compile(r"^(?:[0-9a-fA-F]+\s+)?([A-Za-z?])\s+(\S+)\s*$")


def parse_symbols(path):
    funcs, data, bad = {}, {}, []
    for raw in Path(path).read_text().splitlines():
        raw = raw.strip()
        if not raw:
            continue
        m = LINE_RE.match(raw)
        if not m:
            bad.append(raw)
            continue
        typ, name = m.group(1), m.group(2).split("@", 1)[0]
        if typ in FUNC_TYPES:
            funcs[name] = typ
        else:
            data[name] = typ
    return funcs, data, bad


def parse_override(path):
    names = []
    if not path:
        return names
    for raw in Path(path).read_text().splitlines():
        raw = raw.split("#", 1)[0].strip()
        if raw:
            names.append(raw)
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--symbols", required=True)
    ap.add_argument("--override", default=None)
    ap.add_argument("--out", required=True)
    ap.add_argument("--allow-skip-data", action="store_true",
                    help="lanjut walau ada simbol data (BUKAN fungsi) yang tidak bisa di-trampoline")
    ap.add_argument("--allow-data", default=None,
                    help="file berisi nama simbol data yang BOLEH dilewati (satu per baris, # komentar)")
    a = ap.parse_args()

    funcs, data, bad = parse_symbols(a.symbols)
    override = parse_override(a.override)

    if bad:
        print(f"[gen] {len(bad)} baris tidak dikenali, contoh: {bad[:3]}", file=sys.stderr)
    if not funcs:
        print("[gen] ERROR: tidak ada simbol fungsi di file symbols. "
              "Pastikan isinya output `nm -D --defined-only` dari driver Mali asli.",
              file=sys.stderr)
        return 2

    if data:
        # vtable/typeinfo libc++ (weak, nama _ZT*) selalu ada juga di libc++.so, aman dilewati
        auto = {n for n, t in data.items() if t in "Vv" and n.startswith("_ZT")}
        listed = set(parse_override(a.allow_data)) if a.allow_data else set()
        blocking = {n: t for n, t in data.items() if n not in auto and n not in listed}
        print(f"[gen] simbol data dilewati: {len(auto)} libc++ (_ZT*), "
              f"{len(listed & set(data))} dari allow-list", file=sys.stderr)
        if blocking:
            kinds = {}
            for n, t in blocking.items():
                kinds.setdefault(t, []).append(n)
            print("[gen] PERINGATAN: ada simbol BUKAN fungsi yang belum di-allow:", file=sys.stderr)
            for t, ns in sorted(kinds.items()):
                print(f"       tipe {t}: {len(ns)} simbol: {sorted(ns)[:12]}", file=sys.stderr)
            if not a.allow_skip_data:
                print("[gen] ERROR: simbol data tidak bisa di-trampoline. Pastikan tidak ada library "
                      "yang memakainya (tools/check_data_users.sh), lalu tambahkan ke "
                      "symbols/allow_skip_data.txt. --allow-skip-data melewati semuanya tanpa cek.",
                      file=sys.stderr)
                return 3

    for n in funcs:
        if not NAME_RE.match(n):
            print(f"[gen] ERROR: nama simbol aneh: {n!r}", file=sys.stderr)
            return 4

    missing_ov = [n for n in override if n not in funcs]
    if missing_ov:
        print(f"[gen] catatan: override tidak ada di driver asli (hook akan forward ke NULL): {missing_ov}",
              file=sys.stderr)

    ov = set(override)
    tramp = sorted(n for n in funcs if n not in ov)

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    # ---- assembly ----
    s = []
    s.append("/* GENERATED oleh tools/gen_wrapper.py - jangan diedit */")
    s.append(f"/* {len(tramp)} trampoline, {len(ov)} override */")
    s.append("\t.hidden dz_real_table")
    s.append("\t.text")
    s.append("\t.p2align 2")
    for i, n in enumerate(tramp):
        s.append(f"\t.globl {n}")
        s.append(f"\t.type {n},%function")
        s.append(f"{n}:")
        s.append("\thint #34")                       # bti c (NOP di CPU lama)
        s.append(f"\tadrp x16, dz_real_table+{8 * i}")
        s.append(f"\tldr x16, [x16, :lo12:dz_real_table+{8 * i}]")
        s.append("\tbr x16")
        s.append(f"\t.size {n}, .-{n}")
    s.append('\t.section .note.GNU-stack,"",%progbits')
    (out / "dz_trampolines.S").write_text("\n".join(s) + "\n")

    # ---- names + table ----
    c = []
    c.append("/* GENERATED oleh tools/gen_wrapper.py - jangan diedit */")
    c.append("#include <stddef.h>")
    c.append(f"#define DZ_N {len(tramp)}u")
    c.append('__attribute__((visibility("hidden"))) const unsigned dz_sym_count = DZ_N;')
    c.append('__attribute__((visibility("hidden"))) const char *const dz_sym_names[DZ_N] = {')
    for n in tramp:
        c.append(f'    "{n}",')
    c.append("};")
    c.append('__attribute__((visibility("hidden"))) void *dz_real_table[DZ_N];')
    (out / "dz_names.c").write_text("\n".join(c) + "\n")

    print(f"[gen] OK: {len(tramp)} trampoline, {len(ov)} override, {len(data)} data dilewati")
    return 0


if __name__ == "__main__":
    sys.exit(main())
