-- =====================================================================
-- Supabase: kredensial broker MQTT untuk app Chat dan Catur (EMQX).
-- Jalankan di SQL Editor SETELAH setup.sql. Aman dijalankan ulang (idempotent).
--
-- Cara kerja:
--   * Tabel broker_config menyimpan 1 baris per layanan: 'chat' dan 'chess' (broker TERPISAH).
--   * anon TIDAK bisa membaca tabel. Hanya boleh memanggil get_broker_config().
--   * Firmware memanggil get_broker_config(layanan, device_id) saat app dibuka, lalu menyimpan
--     hasilnya di NVS. Pengguna tidak perlu mengetik host/user/password.
--   * Ganti password di EMQX + update baris di sini -> semua HP ikut ter-update saat app dibuka.
--   * enabled = false -> fungsi mengembalikan null (saklar darurat tanpa update firmware).
--
-- PENTING: kunci publishable ada di firmware/repo publik, jadi SIAPA PUN bisa memanggil fungsi ini.
-- Anggap kredensial ini tidak rahasia. Pengamannya ada di EMQX: pakai user KHUSUS per broker dan
-- batasi ACL (chat: renphone/chat/#  |  chess: topik catur saja). Jangan pakai akun admin EMQX.
-- =====================================================================
create table if not exists public.broker_config (
  service    text primary key check (service in ('chat', 'chess')),
  host       text not null,
  username   text not null,
  password   text not null,
  enabled    boolean not null default true,
  updated_at timestamptz not null default now()
);

alter table public.broker_config enable row level security;
revoke all on public.broker_config from anon, authenticated;
-- Tanpa policy = tidak ada yang bisa baca lewat API. Edit lewat Table Editor / SQL Editor (service role).

-- ---- ISI / GANTI NILAI INI ----
-- host diambil dari screenshot "MQTT Connection Information" (tanpa skema dan tanpa port; port 8883 TLS).
-- username/password: buat di EMQX Console > Access Control > Authentication > (user baru).
insert into public.broker_config (service, host, username, password) values
  ('chat',  'p69c193e.ala.eu-central-1.emqxsl.com', 'GANTI_USER_CHAT',  'GANTI_PASSWORD_CHAT'),
  ('chess', 'x473ff09.ala.eu-central-1.emqxsl.com', 'GANTI_USER_CHESS', 'GANTI_PASSWORD_CHESS')
on conflict (service) do nothing;

-- ---- dipanggil firmware ----
create or replace function public.get_broker_config(p_service text, p_device_id text)
returns json
language plpgsql security definer set search_path = public as $$
declare r public.broker_config;
begin
  if p_device_id is null or p_device_id !~ '^[0-9a-f]{16}$' then
    raise exception 'bad device id';
  end if;
  select * into r from public.broker_config where service = p_service and enabled;
  if not found then
    return null;
  end if;
  return json_build_object('host', r.host, 'user', r.username, 'pass', r.password);
end $$;

revoke all on function public.get_broker_config(text, text) from public;
grant execute on function public.get_broker_config(text, text) to anon;
