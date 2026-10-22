# Telepon NyxOS (SIP + antar-HP lewat WiFi)

## Pasang
Dari folder repo (ada `phone.ino`), taruh `call_renphone.ino` + `apply_call_patch.py` lalu:
```
python3 apply_call_patch.py
git add -A && git commit -m "Telepon SIP + LAN" && git push
```
Skrip mengubah `phone.ino`, workflow build, `rplink.h` (kedua salinan) dan firmware CAM. Aman dijalankan ulang.
Firmware CAM boleh di-flash ulang (opsional) supaya suara lebih cepat (+140 ms lebih rendah).

## Mode 1: SIP (akun gratis)
1. Buat akun di https://subscribe.linphone.org (layanan gratis, tanpa jaminan).
2. App Telepon > Atur: Server `sip.linphone.org`, username, password, nama. (Atau taruh `/sip.cfg` di SD: `host=`, `user=`, `pass=`, `name=`.)
3. Tab SIP: ketik `teman` atau `teman@sip.linphone.org` > Panggil.
Hanya bisa menelepon alamat SIP lain. Nomor HP/PSTN TIDAK bisa lewat layanan gratis.

## Mode 2: LAN antar HP NyxOS (tanpa server/internet)
Tab LAN: isi IP HP lawan atau tekan "Cari HP NyxOS" (broadcast SIP OPTIONS), ketuk hasilnya.
HP penerima harus WiFi sama dan "Siaga terima panggilan" aktif (Atur).

## Syarat & batasan (jujur)
- Tidak ada speaker: suara masuk HARUS lewat TWS (Musik > cari TWS). Mic = INMP441.
- Belum diuji di HP asli; hanya cek sintaks + uji codec/parser di PC.
- G.711 8 kHz, UDP polos tanpa enkripsi (password dikirim sebagai hash digest, suara tidak terenkripsi).
- Tanpa ICE: panggilan via internet bisa satu arah/bisu di NAT tertentu. LAN paling andal.
- Latensi satu arah perkiraan 300-500 ms (A2DP).
- Siaga default MATI (hemat RAM/baterai). Panggilan masuk ditolak otomatis saat NES/DOOM jalan.
- Mic dipakai bersama fitur lain: tutup Voice Memo/Mic Level sebelum menelepon. Wake word dijeda otomatis.
