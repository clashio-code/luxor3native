# Kebijakan Aset — Luxor 3 (MumboJumbo)

## Aset berhak cipta

Konten berikut berasal dari game komersial Luxor 3 (© MumboJumbo / publisher
atau penerus hak cipta):

- `data/gfx/` — sprite, background map, UI, partikel, font atlas
- `data/sound/` — OGG musik, SFX, UI
- `data/levels/`, `data/maps/`, definisi effect/anim/config game
- `Luxor3.exe`, `fmod.dll`, icon, splash screen, installer/uninstaller

**Jangan commit / upload** ke repo publik, GitHub releases publik, artifact publik,
container image publik, CI cache publik, atau website file sharing.

## Pembagian repo

| Lokasi | Isi yang diperbolehkan |
|---|---|
| **Repo port publik** | source C/C++, build scripts, parser yang ditulis sendiri, dokumentasi, test fixture kecil buatan sendiri, patch/tooling yang tidak menyalin konten asli |
| **Repo private / Git LFS** | aset asli hasil instalasi game yang dimiliki user, dengan akses hanya untuk tim yang memang berhak |
| **CI publik** | build dari source-only + placeholder assets sintetis; tidak menarik aset berhak cipta |
| **CI privat** | boleh mengakses aset dari storage privat yang dikonfigurasi pemilik, hanya untuk membangun paket privat |
| **SD card user** | folder port lokal terpisah dengan binary native + data aset lokal dari salinan game milik user |

## Aturan teknis

1. **Jangan memasukkan `data/` ke git.** `.gitignore` port harus mengabaikan
   `data/`, `assets/`, `.asset-cache/`, `Luxor3.exe`, `fmod.dll`.
2. Aplikasi tidak mengeksekusi `.exe`; aset hanya dibaca oleh loader native.
3. Jangan redistribusi aset di release yang bisa diunduh siapa saja.
4. README publik boleh menjelaskan cara menyalin `data/` dari instalasi game
   yang dimiliki user, tanpa menyertakan file itu.
5. Hash file atau inventaris ukuran/ekstensi boleh dipakai untuk audit internal;
   jangan unggah aset, atlas, screenshot, atau data hasil ekstraksi yang
   mengandung artwork ke repositori publik.
6. Lisensi untuk reverse engineering, ekstraksi, penggunaan aset, dan distribusi
   bergantung pada yurisdiksi dan EULA yang berlaku. Dokumen ini bukan nasihat
   hukum. Bila distribusi publik direncanakan, minta izin pemegang hak terlebih
   dahulu.

## `.gitignore` yang disarankan

```gitignore
# Copyrighted Luxor 3 assets (never commit)
/data/
/assets/
.asset-cache/
Luxor3.exe
fmod.dll
unins000.exe
unins000.dat

# Local native builds and runtime data
/build/
/dist/
save/
config/local*
```
