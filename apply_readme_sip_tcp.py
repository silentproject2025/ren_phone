#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_readme_sip_tcp.py -- jalankan dari root repo ren_phone (folder yang ada README.md).
Memperbarui README.md dan call-sip/README.md: fitur Telepon sudah diuji dengan domain
sip2sip.info (SIP lewat TCP), bukan lagi "belum pernah dites".
Aman dijalankan ulang (yang sudah terpasang dilewati). Satu file diubah semua-atau-tidak-sama-sekali.
"""
import io, os, sys

FILES = {}

FILES["README.md"] = [
 ("boot log",
  "[BETA ] phone-call   SIP + LAN         <- belum diuji       │",
  "[ OK  ] phone-call   SIP + LAN         <- diuji: sip2sip    │",
  "<- diuji: sip2sip"),
 ("catatan status",
  "Satu fitur, **Telepon**, baru dirilis dan **belum pernah dites**, jadi bug-nya mungkin ada. Dua hal lain punya catatan khusus:",
  "Satu fitur, **Telepon**, sudah diuji dengan domain `sip2sip.info` (SIP lewat TCP), tetapi penyedia SIP lain dan mode LAN belum dikonfirmasi. Dua hal lain punya catatan khusus:",
  "sudah diuji dengan domain `sip2sip.info` (SIP lewat TCP), tetapi"),
 ("tabel status",
  "| **Telepon (SIP + LAN)** | **`BETA`** | **Fitur baru rilis, belum pernah dites** |",
  "| **Telepon (SIP + LAN)** | `OK` | Teruji dengan domain `sip2sip.info` (SIP lewat TCP). Penyedia lain dan mode LAN belum dikonfirmasi |",
  "Teruji dengan domain `sip2sip.info`"),
 ("pengantar bagian 2",
  "Bagian ini menjelaskan tiga fitur yang statusnya bukan `OK`, supaya",
  "Bagian ini menjelaskan tiga fitur yang statusnya bukan `OK` penuh (Telepon teruji sebagian), supaya",
  "(Telepon teruji sebagian)"),
 ("judul 2.1",
  "### 2.1 Telepon (SIP + antar-HP lewat WiFi) &nbsp; `BETA`",
  "### 2.1 Telepon (SIP + antar-HP lewat WiFi) &nbsp; `TERUJI`",
  "&nbsp; `TERUJI`"),
 ("tautan 2.1",
  "#### Telepon &nbsp; `BETA`\n\nLihat [bagian 2.1](#21-telepon-sip--antar-hp-lewat-wifi--beta).",
  "#### Telepon &nbsp; `TERUJI`\n\nLihat [bagian 2.1](#21-telepon-sip--antar-hp-lewat-wifi--teruji).",
  "lewat-wifi--teruji"),
 ("transport",
  "Keduanya memakai satu \"agen SIP\" yang sama di UDP port 5060, dengan suara G.711 (PCMU/PCMA, 8 kHz, paket 20 ms) lewat RTP.",
  "Keduanya memakai satu \"agen SIP\" yang sama (port 5060), dengan suara G.711 (PCMU/PCMA, 8 kHz, paket 20 ms) lewat RTP. "
  "Mode SIP menghubungi server lewat **TCP** secara default (bisa diganti ke UDP dengan `transport=udp` di `sip.cfg`; "
  "kalau TCP gagal tersambung, otomatis turun ke UDP). Mode LAN memakai UDP.",
  "menghubungi server lewat **TCP** secara default"),
 ("status pengujian",
  "**Kenapa disebut Beta?** Karena belum ada satu pun panggilan nyata yang pernah dilakukan dengan fitur ini di HP asli. "
  "Yang sudah dicek baru sintaks kode serta uji codec dan parser di PC. "
  "Artinya, risiko bug ada di mana-mana: dari pendaftaran akun, bunyi dering, sampai suara tidak terdengar.",
  "**Status pengujian.** Sudah diuji di HP asli dengan akun `sip2sip.info` (SIP lewat TCP): pendaftaran akun berhasil, "
  "serta panggilan keluar dan masuk antara Ren Phone dan HP Android (aplikasi Linphone) tersambung. "
  "Yang belum dikonfirmasi: penyedia SIP lain, mode LAN antar dua HP NyxOS, dan kondisi NAT yang berbeda-beda, jadi bug masih mungkin ada.",
  "**Status pengujian.**"),
 ("cara pakai SIP",
  "2. **Mode SIP:** daftar akun gratis di <https://subscribe.linphone.org>. Di app Telepon buka *Atur*, isi Server `sip.linphone.org`, username, password, dan nama.",
  "2. **Mode SIP:** daftar akun gratis. Yang sudah teruji: `sip2sip.info` (isi Server `sip2sip.info`). "
  "Alternatif: <https://subscribe.linphone.org> (Server `sip.linphone.org`, belum dikonfirmasi). "
  "Di app Telepon buka *Atur*, isi Server, username, password, dan nama.",
  "Yang sudah teruji: `sip2sip.info`"),
 ("enkripsi",
  "- UDP polos tanpa enkripsi. Password dikirim sebagai hash digest, tetapi suaranya tidak terenkripsi.",
  "- Tanpa enkripsi: SIP lewat TCP/UDP polos (belum TLS) dan suara RTP tidak terenkripsi. Password dikirim sebagai hash digest.",
  "(belum TLS) dan suara RTP"),
 ("saran uji",
  "2. Mode SIP ke akun Linphone lain.",
  "2. Mode SIP ke penyedia selain `sip2sip.info` (mis. akun Linphone lain).",
  "penyedia selain `sip2sip.info`"),
 ("tabel sip.cfg",
  "| `/sip.cfg` | Telepon: `host=`, `user=`, `pass=`, `name=` |",
  "| `/sip.cfg` | Telepon: `host=`, `user=`, `pass=`, `name=`, `transport=tcp` atau `udp` (opsional, default tcp) |",
  "`transport=tcp` atau `udp`"),
 ("batasan",
  "- **Telepon belum teruji** (lihat bagian 2.1).",
  "- **Telepon teruji terbatas:** berhasil dengan `sip2sip.info` lewat TCP; penyedia lain dan mode LAN belum dikonfirmasi (lihat bagian 2.1).",
  "**Telepon teruji terbatas:**"),
]

FILES["call-sip/README.md"] = [
 ("akun SIP",
  "1. Buat akun di https://subscribe.linphone.org (layanan gratis, tanpa jaminan).\n"
  "2. App Telepon > Atur: Server `sip.linphone.org`, username, password, nama. (Atau taruh `/sip.cfg` di SD: `host=`, `user=`, `pass=`, `name=`.)",
  "1. Buat akun gratis. Teruji: https://www.sip2sip.info (Server `sip2sip.info`). "
  "Alternatif: https://subscribe.linphone.org (Server `sip.linphone.org`, belum dikonfirmasi).\n"
  "2. App Telepon > Atur: Server, username, password, nama. SIP ke server lewat TCP secara default. "
  "(Atau taruh `/sip.cfg` di SD: `host=`, `user=`, `pass=`, `name=`, `transport=tcp|udp` opsional.)",
  "Teruji: https://www.sip2sip.info"),
 ("contoh tujuan",
  "3. Tab SIP: ketik `teman` atau `teman@sip.linphone.org` > Panggil.",
  "3. Tab SIP: ketik `teman` atau `teman@sip2sip.info` > Panggil.",
  "`teman@sip2sip.info`"),
 ("status uji",
  "- Belum diuji di HP asli; hanya cek sintaks + uji codec/parser di PC.",
  "- Sudah diuji di HP asli dengan akun sip2sip.info (SIP lewat TCP): panggilan keluar dan masuk tersambung. Penyedia lain dan mode LAN belum dikonfirmasi.",
  "Sudah diuji di HP asli dengan akun sip2sip.info"),
 ("transport",
  "- G.711 8 kHz, UDP polos tanpa enkripsi (password dikirim sebagai hash digest, suara tidak terenkripsi).",
  "- G.711 8 kHz. SIP ke server lewat TCP (default; `transport=udp` untuk UDP), tanpa enkripsi/TLS (password dikirim sebagai hash digest, suara tidak terenkripsi). Mode LAN memakai UDP.",
  "SIP ke server lewat TCP (default;"),
]


def main():
    bad = 0
    for path, reps in FILES.items():
        if not os.path.exists(path):
            print("LEWATI (tidak ada): " + path); bad += 1; continue
        s = io.open(path, encoding="utf-8").read()
        out, notes, fail = s, [], False
        for name, old, new, marker in reps:
            if marker in out:
                notes.append("sudah ada : " + name); continue
            if out.count(old) != 1:
                notes.append("GAGAL     : %s (teks lama ditemukan %dx)" % (name, out.count(old))); fail = True; continue
            out = out.replace(old, new)
            notes.append("diganti   : " + name)
        print("== " + path)
        print("\n".join("   " + n for n in notes))
        if fail:
            print("   -> file TIDAK diubah (ada yang gagal)"); bad += 1
        elif out != s:
            io.open(path, "w", encoding="utf-8", newline="").write(out)
            print("   -> disimpan")
        else:
            print("   -> tidak ada perubahan")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
