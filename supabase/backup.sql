-- =====================================================================
-- ren_phone · backup pengaturan ke database (tanpa akun/sandi)
-- Jalankan SETELAH setup.sql. Aman dijalankan ulang, dan aman di atas draf lama.
--
-- Cara kerja:
--   * "User" = kode perangkat (device_id, tidak bisa diganti).
--   * Kode perangkat BUKAN rahasia (turunan MAC yang terpancar lewat WiFi/BT). Karena itu setiap
--     permintaan wajib membawa TOKEN yang diturunkan dari rahasia chip. Server hanya menyimpan
--     SHA-256 dari token itu; pemegang pertama (first-claim) = pemilik.
--   * Isi backup dienkripsi DI PERANGKAT (AES-256 + HMAC); kuncinya tidak pernah dikirim. Server dan
--     admin database hanya melihat data acak. Server menolak data yang bukan format terenkripsi.
--   * anon tidak bisa membaca/menulis tabel langsung, hanya lewat 3 fungsi di bawah. Tanpa policy apa pun,
--     bahkan akun dashboard admin tidak bisa membaca tabel ini.
--   * Tidak ada pembatas "global" (itu bisa dipakai orang iseng untuk mengunci semua pengguna). Token 256-bit
--     tidak bisa ditebak, jadi tidak perlu.
-- =====================================================================

-- ===== 0. Bersihkan draf lama (versi tanpa token tidak aman: siapa pun yang tahu kode bisa membaca) =====
drop function if exists public.put_backup(text, text);
drop function if exists public.get_backup(text, boolean);
drop function if exists public.delete_backup(text);
drop table    if exists public.backup_miss;

-- ===== 1. TABEL =====
create table if not exists public.device_backups (
  device_id   text primary key references public.devices(device_id) on delete cascade,
  data        text not null,                    -- file terenkripsi (NYXE1) dalam base64
  size_bytes  int  not null,
  updated_at  timestamptz not null default now()
);
alter table public.device_backups add column if not exists auth_hash text;
-- baris dari draf lama tidak punya pemilik terverifikasi dan mungkin tidak terenkripsi -> dibuang
delete from public.device_backups where auth_hash is null;
alter table public.device_backups alter column auth_hash set not null;
alter table public.device_backups drop constraint if exists device_backups_hash_fmt;
alter table public.device_backups add constraint device_backups_hash_fmt check (auth_hash ~ '^[0-9a-f]{64}$');

alter table public.device_backups enable row level security;
revoke all on public.device_backups from anon, authenticated;
-- sengaja TANPA policy: tidak ada akses langsung sama sekali (hanya fungsi security definer di bawah)

-- ===== 2. SIMPAN =====
-- Hasil: ok | unknown_device | invalid | too_big | too_soon | denied | full
create or replace function public.put_backup(p_device_id text, p_token text, p_data text)
returns text
language plpgsql security definer set search_path = public as $$
declare
  h   text;
  cur public.device_backups;
begin
  if p_device_id is null or p_device_id !~ '^[0-9a-f]{16}$' then return 'unknown_device'; end if;
  if p_token is null or p_token !~ '^[0-9a-f]{64}$' then return 'invalid'; end if;
  if p_data is null or length(p_data) < 16 then return 'invalid'; end if;
  if length(p_data) > 140000 then return 'too_big'; end if;            -- ~ 96 KB file asli + overhead enkripsi
  if p_data !~ '^[A-Za-z0-9+/]+={0,2}$' then return 'invalid'; end if;
  -- hanya file TERENKRIPSI: base64 dari magic "NYXE1\0" = TllYRTEA. File biasa ("NYXB1") ditolak.
  if left(p_data, 8) <> 'TllYRTEA' then return 'invalid'; end if;
  if not exists (select 1 from public.devices where device_id = p_device_id) then return 'unknown_device'; end if;

  h := encode(sha256(convert_to(p_token, 'UTF8')), 'hex');

  select * into cur from public.device_backups where device_id = p_device_id for update;
  if found then
    if cur.auth_hash <> h then return 'denied'; end if;
    if cur.updated_at > now() - interval '30 seconds' then return 'too_soon'; end if;
    update public.device_backups
       set data = p_data, size_bytes = (length(p_data) * 3) / 4, updated_at = now()
     where device_id = p_device_id;
    return 'ok';
  end if;

  -- klaim pertama. Batas total supaya database tidak bisa dipenuhi orang iseng (ubah sesuai paket kamu).
  if (select coalesce(sum(size_bytes), 0) from public.device_backups) > 250000000 then return 'full'; end if;
  begin
    insert into public.device_backups (device_id, auth_hash, data, size_bytes)
    values (p_device_id, h, p_data, (length(p_data) * 3) / 4);
  exception when unique_violation then
    return 'denied';                                                   -- keduluan klaim lain pada saat yang sama
  end;
  return 'ok';
end $$;

revoke all on function public.put_backup(text, text, text) from public;
grant execute on function public.put_backup(text, text, text) to anon;

-- ===== 3. AMBIL =====
-- Status: ok | none | invalid. Token salah dan "tidak ada backup" sama-sama 'none' (tidak membocorkan
-- apakah sebuah kode perangkat punya backup). p_meta_only = true -> hanya ukuran & waktu, tanpa isi.
create or replace function public.get_backup(p_device_id text, p_token text, p_meta_only boolean default false)
returns jsonb
language plpgsql security definer set search_path = public as $$
declare
  b public.device_backups;
begin
  if p_device_id is null or p_device_id !~ '^[0-9a-f]{16}$' or p_token is null or p_token !~ '^[0-9a-f]{64}$' then
    return jsonb_build_object('status', 'invalid');
  end if;
  select * into b from public.device_backups
   where device_id = p_device_id
     and auth_hash = encode(sha256(convert_to(p_token, 'UTF8')), 'hex');
  if not found then return jsonb_build_object('status', 'none'); end if;
  if p_meta_only then
    return jsonb_build_object('status', 'ok', 'size', b.size_bytes, 'updated_at', b.updated_at);
  end if;
  return jsonb_build_object('status', 'ok', 'size', b.size_bytes, 'updated_at', b.updated_at, 'data', b.data);
end $$;

revoke all on function public.get_backup(text, text, boolean) from public;
grant execute on function public.get_backup(text, text, boolean) to anon;

-- ===== 4. HAPUS =====
-- Hasil: ok (terhapus) | none (tidak ada / token tidak cocok) | invalid
create or replace function public.delete_backup(p_device_id text, p_token text)
returns text
language plpgsql security definer set search_path = public as $$
declare
  n int;
begin
  if p_device_id is null or p_device_id !~ '^[0-9a-f]{16}$' or p_token is null or p_token !~ '^[0-9a-f]{64}$' then
    return 'invalid';
  end if;
  delete from public.device_backups
   where device_id = p_device_id
     and auth_hash = encode(sha256(convert_to(p_token, 'UTF8')), 'hex');
  get diagnostics n = row_count;
  return case when n > 0 then 'ok' else 'none' end;
end $$;

revoke all on function public.delete_backup(text, text) from public;
grant execute on function public.delete_backup(text, text) to anon;

notify pgrst, 'reload schema';
