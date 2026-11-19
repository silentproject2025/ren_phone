-- =====================================================================
-- Supabase: daftar perangkat + username publik ren_phone.
-- Jalankan di SQL Editor. Aman dijalankan ULANG di database yang sudah ada (idempotent):
-- baris lama tetap, hanya kolom/fungsi baru yang ditambahkan.
--
-- Prinsip keamanan:
--   * anon (kunci di firmware) TIDAK bisa membaca/menulis tabel langsung. Hanya boleh memanggil
--     register_device(), set_username(), dan public_board().
--   * device_id (16 hex) TIDAK PERNAH keluar lewat public_board(). Hanya pemilik perangkat yang
--     tahu id-nya, jadi id itulah "kunci" untuk mengganti username perangkatnya.
--   * Username opsional: perangkat tanpa username tidak tampil di dashboard publik.
--   * Membaca tabel utuh + moderasi hanya untuk akun dashboard milikmu (Authentication > Users).
-- GANTI 'GANTI_EMAIL_KAMU' (2 tempat) dengan email akun Supabase Auth yang dipakai login dashboard.
-- =====================================================================
create table if not exists public.devices (
  device_id   text primary key check (device_id ~ '^[0-9a-f]{16}$'),
  first_seen  timestamptz not null default now(),
  last_seen   timestamptz not null default now(),
  fw_version  text,
  fw_build    int,
  fw_branch   text,
  hw          text,
  ping_count  int not null default 1
);

-- ---- kolom baru: username publik + moderasi ----
alter table public.devices add column if not exists username        text;
alter table public.devices add column if not exists name_changed_at timestamptz;
alter table public.devices add column if not exists hidden          boolean not null default false;

alter table public.devices drop constraint if exists devices_username_fmt;
alter table public.devices add constraint devices_username_fmt
  check (username is null or username ~ '^[A-Za-z0-9_.-]{3,16}$');

-- unik tanpa membedakan huruf besar/kecil: "Nyx" dan "nyx" dianggap sama
create unique index if not exists devices_username_ci on public.devices (lower(username));

alter table public.devices enable row level security;
revoke all on public.devices from anon, authenticated;
grant select on public.devices to authenticated;
grant update (hidden, username) on public.devices to authenticated;

drop policy if exists devices_owner_read on public.devices;
create policy devices_owner_read on public.devices
  for select to authenticated
  using ((auth.jwt() ->> 'email') = 'GANTI_EMAIL_KAMU');

drop policy if exists devices_owner_moderate on public.devices;
create policy devices_owner_moderate on public.devices
  for update to authenticated
  using ((auth.jwt() ->> 'email') = 'GANTI_EMAIL_KAMU')
  with check ((auth.jwt() ->> 'email') = 'GANTI_EMAIL_KAMU');

-- ---- daftar perangkat (dipanggil firmware) ----
create or replace function public.register_device(
  p_device_id text, p_version text, p_build int, p_branch text, p_hw text
) returns void
language plpgsql security definer set search_path = public as $$
begin
  if p_device_id is null or p_device_id !~ '^[0-9a-f]{16}$' then
    raise exception 'bad device id';
  end if;
  insert into public.devices (device_id, fw_version, fw_build, fw_branch, hw)
  values (p_device_id, left(p_version, 32), p_build, left(p_branch, 32), left(p_hw, 16))
  on conflict (device_id) do update set
    last_seen  = now(),
    ping_count = public.devices.ping_count + 1,
    fw_version = excluded.fw_version,
    fw_build   = excluded.fw_build,
    fw_branch  = excluded.fw_branch,
    hw         = excluded.hw;
end $$;

revoke all on function public.register_device(text, text, int, text, text) from public;
grant execute on function public.register_device(text, text, int, text, text) to anon;

-- ---- atur username (dipanggil firmware). Hasil berupa teks, bukan error, supaya HP bisa menampilkannya:
--   ok | cleared | invalid | reserved | taken | unknown_device
-- p_username kosong = hapus username (perangkat hilang dari dashboard publik).
-- Daftar nama yang dicadangkan: ubah sesuai selera.
create or replace function public.set_username(p_device_id text, p_username text)
returns text
language plpgsql security definer set search_path = public as $$
declare
  n text := nullif(btrim(coalesce(p_username, '')), '');
begin
  if p_device_id is null or p_device_id !~ '^[0-9a-f]{16}$' then
    return 'unknown_device';
  end if;
  if not exists (select 1 from public.devices where device_id = p_device_id) then
    return 'unknown_device';
  end if;

  if n is null then
    update public.devices set username = null, name_changed_at = now() where device_id = p_device_id;
    return 'cleared';
  end if;

  if n !~ '^[A-Za-z0-9_.-]{3,16}$' then
    return 'invalid';
  end if;
  if lower(n) in ('admin','administrator','moderator','system','support','official','staff',
                  'nyx','nyxos','accretion','renphone','ren_phone','root','null','undefined') then
    return 'reserved';
  end if;

  begin
    update public.devices set username = n, name_changed_at = now() where device_id = p_device_id;
  exception when unique_violation then
    return 'taken';
  end;
  return 'ok';
end $$;

revoke all on function public.set_username(text, text) from public;
grant execute on function public.set_username(text, text) to anon;

-- ---- data dashboard publik: HANYA perangkat ber-username yang tidak disembunyikan ----
-- Tanpa device_id. 'total' menghitung semua perangkat terdaftar (termasuk yang tanpa username).
create or replace function public.public_board()
returns jsonb
language sql stable security definer set search_path = public as $$
  select jsonb_build_object(
    'total',  (select count(*) from public.devices),
    'named',  (select count(*) from public.devices where username is not null and not hidden),
    'at',     now(),
    'rows',   coalesce((
      select jsonb_agg(r) from (
        select username, hw, fw_version as fw, first_seen, last_seen
        from public.devices
        where username is not null and not hidden
        order by last_seen desc
        limit 500
      ) r), '[]'::jsonb)
  );
$$;

revoke all on function public.public_board() from public;
grant execute on function public.public_board() to anon;
