create table if not exists public.device_commands (
 id text primary key default 'main', crop_code integer not null default 0 check (crop_code between 0 and 4),
 crop_name text not null default 'Not selected', updated_at timestamptz not null default now()
);
insert into public.device_commands(id) values ('main') on conflict(id) do nothing;
create table if not exists public.sensor_readings (
 id bigint generated always as identity primary key, created_at timestamptz not null default now(),
 crop_code integer not null default 0, crop_name text not null default 'Not selected',
 soil_raw integer not null, soil_moisture numeric null, temperature numeric null, humidity numeric null,
 motion_detected boolean not null default false, pump_on boolean not null default false,
 irrigation_required boolean not null default false, pump_lockout boolean not null default false,
 website_synchronized boolean not null default false, wifi_rssi integer null
);
alter table public.sensor_readings alter column soil_moisture drop not null;
create index if not exists sensor_readings_created_at_idx on public.sensor_readings(created_at desc);
alter table public.sensor_readings enable row level security;
alter table public.device_commands enable row level security;
-- Prototype public read-only dashboard. For production use authenticated per-farm RLS.
create policy "prototype read telemetry" on public.sensor_readings for select to anon using (true);
create policy "prototype read command" on public.device_commands for select to anon using (true);
