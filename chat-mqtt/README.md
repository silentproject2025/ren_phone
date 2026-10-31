# Chat Ren Phone lewat broker MQTT

Tidak ada server buatan sendiri: sebuah **broker MQTT** (TLS port 8883, login username+password) yang jadi "servernya". Firmware tidak terikat ke satu penyedia, jadi pindah broker nanti cukup mengganti isi tiga kolom di HP.

## Soal HiveMQ (jangan dipakai)
HiveMQ Cloud Serverless (paket gratis) dihentikan: sejak 2026-09-30 tidak bisa membuat cluster baru, berhenti berfungsi 2026-12-31, dan datanya dihapus 2027-01-07. Panduan lama yang menyebut HiveMQ gratis sudah tidak berlaku.

## Pilihan gratis yang saya cek: EMQX Cloud Serverless
Menurut halaman paket EMQX saat saya cek: kuota gratis tiap bulan 1 juta menit sesi, 1 GB trafik, 1 juta rule action, sampai 1.000 koneksi, dengan TLS. Serverless punya batas koneksi, throughput, langganan, dan metode koneksi. Soal "tanpa kartu kredit" saya hanya menemukannya di situs pihak ketiga, jadi lihat sendiri saat mendaftar. Setelah kejadian HiveMQ, cek ulang syarat sebelum bergantung padanya.

1. Daftar di https://www.emqx.com/en/cloud, buat project, lalu buat deployment **Serverless**.
2. Menu **Overview**: catat **alamat koneksi (host)**. Port **8883 (mqtts)** sudah aktif secara bawaan; Serverless memang mewajibkan TLS. Bentuk alamatnya mirip `xxxxxxxx.ala.eu-central-1.emqxsl.com` (pakai persis yang tertera di konsolmu).
3. Menu **Access Control -> Authentication** (di dokumentasi lain disebut Client Authentication): klik **Add**, isi **username + password** khusus HP, lalu Confirm. Di menu Access Control juga ada pengaturan otorisasi (ACL); kalau mau, batasi kredensial itu ke topik `renphone/chat/#` (publish + subscribe). Nama menu bisa sedikit berbeda dari ini karena konsol sering diperbarui.

## Isi di HP
App **Chat** -> isi Broker, Username, Password, Nama -> *Simpan & sambung*.
Mengetik di keyboard layar melelahkan, jadi bisa juga taruh file `/chat.cfg` di SD card:
```
host=xxxxxxxx.ala.eu-central-1.emqxsl.com
user=namauser
pass=passwordnya
name=Ren
```
Lalu buka app Chat (atau tombol *Muat SD* di layar pengaturan).

## Cara kerja singkat
- Topik `renphone/chat/m/<slot>`, 24 slot bergilir, tiap pesan dikirim *retained*. Saat HP tersambung, broker langsung mengirim isi 24 slot itu = riwayat 24 pesan terakhir, termasuk yang dikirim waktu app Chat kamu tertutup.
- Payload `seq|waktu|nama|pesan`. Nomor `seq` dipakai untuk mengurutkan dan membuang duplikat.
- HP hanya tersambung selagi app Chat terbuka (hemat RAM dan baterai).

## Yang sudah dan belum terverifikasi
- Dari dokumentasi EMQX (batas Serverless): pesan *retained* didukung, maksimal 2.000 pesan, ukuran maksimal 1 MB per pesan, dan tidak kedaluwarsa. Chat ini hanya memakai 24 pesan retained, jauh di bawah batas. Dokumen batas itu terlihat agak lama, jadi tetap tes sendiri.
- Serverless meminta SNI = alamat koneksi. App Chat menyambung memakai nama host, jadi SNI ikut terkirim; tapi ini belum saya coba di broker sungguhan.
- Jumlah deployment Serverless dibatasi 2; yang kedua butuh metode pembayaran. Satu deployment cukup.
- Kode ini belum dicoba dengan broker sungguhan. Cara tes riwayat: kirim pesan dari HP A, tutup app di HP B, kirim lagi dari A, lalu buka app di B; riwayat harus muncul.

## Batasan
- Tidak ada notifikasi saat app tertutup; pesan baru terlihat begitu Chat dibuka.
- Dua HP yang mengirim di detik yang persis sama bisa bertabrakan di slot yang sama (satu pesan hilang dari riwayat).
- Layar hanya menampilkan ASCII (aksen/emoji jadi `?`).
- Koneksi memakai `setInsecure()` (sertifikat broker tidak diverifikasi), sama seperti fitur jaringan lain di firmware ini. Jangan kirim hal sensitif.

## Alternatif kalau EMQX nanti berubah juga
- Broker sendiri (mis. Mosquitto) di PC/Raspberry Pi: gratis, tapi agar bisa diakses dari luar rumah butuh port forwarding atau tunnel.
- Server sendiri di Cloudflare Workers + Durable Objects (paket gratis yang saya cek sebelumnya). Itu butuh versi app Chat yang memakai HTTP polling, bukan MQTT.
- Topik `renphone/chat/t` (non-retained, QoS 0): indikator "sedang mengetik" berisi nama pengirim; sudah tercakup ACL `renphone/chat/#`.
