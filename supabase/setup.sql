-- =====================================================================
-- Supabase: daftar perangkat ren_phone. Jalankan SEKALI di SQL Editor.
-- Prinsip: anon (kunci di firmware) TIDAK bisa membaca/menulis tabel langsung; hanya boleh memanggil
-- register_device(). Membaca data hanya untuk akun dashboard milikmu (Authentication > Users).
-- GANTI 'GANTI_EMAIL_KAMU' di bawah dgn email akun Supabase Auth yg dipakai login dashboard.
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

alter table public.devices enable row level security;
revoke all on public.devices from anon, authenticated;
grant select on public.devices to authenticated;

drop policy if exists devices_owner_read on public.devices;
create policy devices_owner_read on public.devices
  for select to authenticated
  using ((auth.jwt() ->> 'email') = 'GANTI_EMAIL_KAMU');

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
