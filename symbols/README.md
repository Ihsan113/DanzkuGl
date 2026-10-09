# symbols/

Taruh `mali_symbols.txt` di sini (hasil `tools/dump_symbols.sh` di HP). Isinya cuma
nama simbol driver Mali asli, bukan binary-nya, jadi aman di-commit.

File ini WAJIB ada sebelum build; tanpa itu CMake dan GitHub Actions berhenti dengan pesan jelas.
