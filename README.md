# FieldWise Complete — standalone prototype

This is a **new standalone project**, not a drop-in patch for an existing repository. Includes ESP32 firmware, Vercel API, Supabase SQL, React dashboard, local weather forecast, voice status, sowing calendar. Extra features from brainstorming such as solar hardware, NPK, QR plots, and community scheduling are **not** implemented.

## Setup
1. Create a new Supabase project; run `supabase/schema.sql` in SQL Editor. If reusing an existing DB, back it up first and compare schema/column names; this SQL is intended for a new prototype database.
2. Copy `.env.example` to `.env.local`; fill server Supabase URL, service-role key, new random device key, and VITE public URL/anon key. Never expose service-role or OpenAI keys in VITE variables or Git.
3. `npm install` then `npm run dev`. Vite alone does not run Vercel API endpoints locally: use `npx vercel dev` for integrated API testing or deploy to Vercel.
4. Push to a **new private repository** and import into Vercel. Add the same environment variables in Vercel settings and deploy. Production domain must be public for ESP32.
5. In `firmware/FieldWise_ESP32.ino`, enter your Wi-Fi SSID/password, **new** device key and production API/command URLs. Arduino IDE: install ESP32 board package, Adafruit DHT sensor library, Adafruit Unified Sensor, ArduinoJson v7. Board ESP32 Dev Module; serial 115200.
6. Check Serial Monitor for command HTTP 200 and cloud HTTP 201. Dashboard refreshes every 10 seconds. Crop selector requires device key for prototype; a production deployment needs authenticated access control.

## Wiring
Soil VCC→3V3, GND→GND, **AO→GPIO34**; DO unused. DHT22 data→GPIO4 (suitable pullup as module requires), PIR OUT→GPIO32, relay IN→GPIO27, buzzer input→GPIO33. All low-voltage control grounds must be correctly referenced for your modules. Check relay voltage, 3.3V logic compatibility, pump supply, relay contact ratings and flyback suppression. Do not connect pump to GPIO or connect 5V signal to GPIO34. Keep pump supply disconnected until bench tests pass.

## Safety and limitations
- Raw 4095 (or near rails) is a sensor fault, **not 0% moisture**. Firmware must fail OFF. Calibrate SOIL_DRY_RAW and SOIL_WET_RAW with your sensor.
- Firmware HTTPS uses `setInsecure()` for prototype only. For production use certificate verification.
- Example crop thresholds are demonstration values, not agricultural advice.
- Public anonymous read policy in SQL is for a prototype; real farms require authenticated tenant-specific RLS. Device-key-based dashboard crop control is not production-grade user auth. Rotate any previously exposed credentials.
- Browser location permission is required for weather. Voice recognition support varies by browser; voice status output is English, not full Tamil conversational AI.
sybg49i8rn34tm90i9i87h45rcy67j8k99ibvfve467uyh8iu976jhnhjhmi87rrtny55y5kmuymtut