/*
 FieldWise ESP32 - complete prototype firmware
 DHT22, capacitive soil sensor, PIR, active-low relay, active-high buzzer.
 Website crop commands: GET /api/command; telemetry: POST /api/telemetry.
 Arduino libraries: DHT sensor library (Adafruit), Adafruit Unified Sensor,
 ArduinoJson v7; ESP32 Arduino board package.

 IMPORTANT: low-voltage DC pump with external supply and appropriately rated
 relay/driver only. Never connect a pump to an ESP32 GPIO. Test with pump
 disconnected first. PIR is a demonstration inhibit, not safety-rated.
 HTTPS setInsecure() is for a supervised prototype, not production security.
*/
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <math.h>

// -------- EDIT ONLY THESE PRIVATE SETTINGS --------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* DEVICE_API_KEY = "YOUR_NEW_DEVICE_API_KEY";
// Prefer the PUBLIC PRODUCTION domain (not a Vercel-protected branch preview).
const char* API_URL = "https://YOUR-PUBLIC-PRODUCTION-DOMAIN.vercel.app/api/telemetry";
const char* COMMAND_URL = "https://YOUR-PUBLIC-PRODUCTION-DOMAIN.vercel.app/api/command";

// -------- PINS / MODULE POLARITY --------
constexpr uint8_t SOIL_PIN = 34;
constexpr uint8_t DHT_PIN = 4;
#define DHT_TYPE DHT22
constexpr uint8_t PIR_PIN = 32;
constexpr uint8_t RELAY_PIN = 27;
constexpr uint8_t BUZZER_PIN = 33;
const bool RELAY_ACTIVE_HIGH = false; // most relay modules are active LOW
const bool BUZZER_ACTIVE_HIGH = true; // change if your buzzer module is active LOW
const bool PIR_ACTIVE_HIGH = true;
DHT dht(DHT_PIN, DHT_TYPE);

// CALIBRATE using YOUR soil sensor: raw in dry air, then raw in wet soil.
// With these example values, raw 3000 means 0%, raw 1300 means 100%.
const int SOIL_DRY_RAW = 3000;
const int SOIL_WET_RAW = 1300;
// A rail reading often means disconnected/bad wiring. Do not water on it.
const int SOIL_RAIL_LOW = 10;
const int SOIL_RAIL_HIGH = 4085;

// Prototype demonstration thresholds, NOT agricultural recommendations.
struct CropTarget { float start; float stop; };
const CropTarget TARGETS[5] = {{0,0},{45,65},{35,55},{30,50},{40,60}};
enum Crop : uint8_t { NO_CROP=0, RICE=1, WHEAT=2, COTTON=3, TOMATO=4 };
Crop selectedCrop = NO_CROP;

constexpr unsigned long SENSOR_INTERVAL = 2500; // DHT22 requires >=2 s
constexpr unsigned long CLOUD_INTERVAL = 20000;
constexpr unsigned long COMMAND_INTERVAL = 10000;
constexpr unsigned long WIFI_RETRY_INTERVAL = 10000;
constexpr unsigned long MAX_PUMP_RUNTIME = 15000; // timeout latches OFF
constexpr unsigned long MIN_PUMP_OFFTIME = 60000;
constexpr unsigned long PIR_WARMUP_TIME = 30000;
constexpr unsigned long MOTION_CLEAR_TIME = 5000;
constexpr unsigned long BUZZER_BEEP_PERIOD = 700;
constexpr unsigned long BUZZER_BEEP_ON = 180;

int soilRaw = -1;
float soilMoisture = NAN;
float temperatureC = NAN;
float humidityPct = NAN;
bool soilValid = false, dhtValid = false;
bool humanMotion = false, motionInhibit = true;
bool pumpOn = false, irrigationRequired = false;
bool pumpTimeoutLockout = false;
bool buzzerOn = false;
bool haveSoilReading = false;
unsigned long bootTime = 0, lastSensorTime = 0, lastCloudTime = 0;
unsigned long lastCommandTime = 0, lastWiFiAttempt = 0;
unsigned long pumpStartedTime = 0, lastPumpOffTime = 0, lastMotionTime = 0;
long lastAppliedRevision = -1;

const char* cropName() {
 switch(selectedCrop) {
  case RICE: return "Rice"; case WHEAT: return "Wheat";
  case COTTON: return "Cotton"; case TOMATO: return "Tomato";
  default: return "Not selected";
 }
}
void relayWrite(bool on) {
 digitalWrite(RELAY_PIN, (on == RELAY_ACTIVE_HIGH) ? HIGH : LOW);
}
void buzzerWrite(bool on) {
 buzzerOn = on;
 digitalWrite(BUZZER_PIN, (on == BUZZER_ACTIVE_HIGH) ? HIGH : LOW);
}
void setPump(bool on) {
 if (pumpOn == on) return;
 relayWrite(on);
 pumpOn = on;
 if(on) { pumpStartedTime = millis(); Serial.println("PUMP: ON"); }
 else { lastPumpOffTime = millis(); Serial.println("PUMP: OFF"); }
}
void resetSystem() {
 setPump(false);
 selectedCrop = NO_CROP;
 irrigationRequired = false;
 pumpTimeoutLockout = false;
 lastPumpOffTime = millis(); // no immediate restart
 Serial.println("RESET: crop cleared, timeout lockout cleared; pump OFF");
}
void selectCrop(Crop crop) {
 setPump(false);
 selectedCrop = crop;
 irrigationRequired = false;
 lastPumpOffTime = millis();
 Serial.print("Website selected crop: "); Serial.println(cropName());
 if(pumpTimeoutLockout) Serial.println("Timeout lockout remains active until Reset.");
}
void updateMotion(unsigned long now) {
 humanMotion = (digitalRead(PIR_PIN) == (PIR_ACTIVE_HIGH ? HIGH : LOW));
 if(humanMotion) lastMotionTime = now;
 motionInhibit = (now - bootTime < PIR_WARMUP_TIME) || humanMotion ||
                 (now - lastMotionTime < MOTION_CLEAR_TIME);
 if(motionInhibit && pumpOn) {
  setPump(false);
  Serial.println("PIR inhibit: pump stopped.");
 }
 // Beep intermittently on detected motion, not continuously on PIR warm-up.
 buzzerWrite(humanMotion && (now % BUZZER_BEEP_PERIOD < BUZZER_BEEP_ON));
}
void checkPumpTimeout(unsigned long now) {
 if(pumpOn && now - pumpStartedTime >= MAX_PUMP_RUNTIME) {
  setPump(false);
  pumpTimeoutLockout = true;
  irrigationRequired = false;
  Serial.println("SAFETY LOCKOUT: maximum pump runtime reached. Inspect, then use website Reset.");
 }
}
void readSensors() {
 // Average samples for slightly steadier analog measurements.
 uint32_t sum=0;
 for(int i=0;i<8;i++) { sum += analogRead(SOIL_PIN); delay(2); }
 soilRaw = (int)(sum/8);
 soilValid = SOIL_DRY_RAW != SOIL_WET_RAW &&
             soilRaw > SOIL_RAIL_LOW && soilRaw < SOIL_RAIL_HIGH;
 if(soilValid) {
  soilMoisture = 100.0f * (SOIL_DRY_RAW - soilRaw) / (SOIL_DRY_RAW - SOIL_WET_RAW);
  soilMoisture = constrain(soilMoisture, 0.0f, 100.0f);
 } else {
  soilMoisture = NAN;
  irrigationRequired = false;
  setPump(false);
  Serial.println("SOIL SENSOR FAULT: rail reading or invalid calibration. Pump inhibited.");
 }
 haveSoilReading = true;
 float t=dht.readTemperature();
 float h=dht.readHumidity();
 dhtValid = isfinite(t) && isfinite(h) && t >= -40 && t <= 80 && h >= 0 && h <= 100;
 if(dhtValid) { temperatureC=t; humidityPct=h; }
 else { temperatureC=NAN; humidityPct=NAN; Serial.println("DHT22 reading invalid; check wiring/pull-up."); }
 Serial.println("\n------ SENSOR DATA ------");
 Serial.print("Crop: "); Serial.println(cropName());
 Serial.print("Soil raw: "); Serial.println(soilRaw);
 Serial.print("Soil: "); if(soilValid) { Serial.print(soilMoisture,1); Serial.println("%"); } else Serial.println("INVALID");
 Serial.print("Temperature: "); if(dhtValid) { Serial.print(temperatureC,1); Serial.println(" C"); } else Serial.println("INVALID");
 Serial.print("Humidity: "); if(dhtValid) { Serial.print(humidityPct,1); Serial.println("%"); } else Serial.println("INVALID");
 Serial.print("PIR: "); Serial.println(humanMotion ? "MOTION" : "CLEAR");
 Serial.print("Pump: "); Serial.println(pumpOn ? "ON" : "OFF");
 Serial.print("Lockout: "); Serial.println(pumpTimeoutLockout ? "YES" : "NO");
}
void manageIrrigation(unsigned long now) {
 checkPumpTimeout(now);
 if(!haveSoilReading || !soilValid || selectedCrop==NO_CROP || pumpTimeoutLockout) {
  irrigationRequired=false; setPump(false); return;
 }
 const CropTarget &target = TARGETS[(int)selectedCrop];
 if(soilMoisture <= target.start) irrigationRequired=true;
 else if(soilMoisture >= target.stop) irrigationRequired=false;
 // Stop promptly on adequate moisture, even while PIR is inhibited.
 if(pumpOn && !irrigationRequired) setPump(false);
 if(motionInhibit) { setPump(false); return; }
 if(!pumpOn && irrigationRequired && now-lastPumpOffTime >= MIN_PUMP_OFFTIME) setPump(true);
}
bool cloudConfigured() {
 return strlen(WIFI_SSID)>0 && String(WIFI_SSID)!="YOUR_WIFI_NAME" &&
  String(API_URL).indexOf("YOUR-PUBLIC-PRODUCTION-DOMAIN")<0 &&
  String(COMMAND_URL).indexOf("YOUR-PUBLIC-PRODUCTION-DOMAIN")<0 &&
  String(DEVICE_API_KEY)!="YOUR_NEW_DEVICE_API_KEY" && strlen(DEVICE_API_KEY)>0;
}
void maintainWiFi(unsigned long now) {
 if(WiFi.status()==WL_CONNECTED || String(WIFI_SSID)=="YOUR_WIFI_NAME") return;
 if(now-lastWiFiAttempt < WIFI_RETRY_INTERVAL) return;
 lastWiFiAttempt=now;
 WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
 Serial.println("Wi-Fi reconnect attempt");
}
void uploadToCloud() {
 if(!cloudConfigured() || WiFi.status()!=WL_CONNECTED || !haveSoilReading) return;
 // Report invalid soil as null; server stores sensor fault while pump remains OFF.
 WiFiClientSecure client;
 client.setInsecure(); // DEMO ONLY; replace with CA validation for production
 HTTPClient http;
 http.setTimeout(3500);
 if(!http.begin(client,API_URL)) { Serial.println("Telemetry HTTP begin failed"); return; }
 http.addHeader("Content-Type","application/json");
 http.addHeader("x-device-key",DEVICE_API_KEY);
 JsonDocument doc;
 doc["device_id"]="fieldwise-esp32-01";
 doc["crop_code"]=(int)selectedCrop;
 doc["crop_name"]=cropName();
 if(soilValid) doc["soil_moisture"]=soilMoisture; else doc["soil_moisture"]=nullptr;
 doc["soil_raw"]=soilRaw;
 if(dhtValid) { doc["temperature"]=temperatureC; doc["humidity"]=humidityPct; }
 else { doc["temperature"]=nullptr; doc["humidity"]=nullptr; }
 doc["pump_on"]=pumpOn;
 doc["irrigation_required"]=irrigationRequired;
 doc["pump_lockout"]=pumpTimeoutLockout;
 doc["motion_detected"]=humanMotion;
 doc["wifi_rssi"]=WiFi.RSSI();
 doc["website_synchronized"]=(lastAppliedRevision>=0);
 String payload;
 serializeJson(doc,payload);
 int status=http.POST(payload);
 Serial.print("Cloud HTTP status: "); Serial.println(status);
 if(status<200 || status>=300) Serial.println(http.getString());
 http.end();
}
void pollCropCommand() {
 if(!cloudConfigured() || WiFi.status()!=WL_CONNECTED) return;
 WiFiClientSecure client;
 client.setInsecure(); // DEMO ONLY
 HTTPClient http;
 http.setTimeout(3500);
 if(!http.begin(client,COMMAND_URL)) return;
 http.addHeader("x-device-key",DEVICE_API_KEY);
 int status=http.GET();
 if(status==200) {
  String body=http.getString();
  JsonDocument doc;
  DeserializationError err=deserializeJson(doc,body);
  if(err) Serial.println("Invalid crop command JSON");
  else if(doc["crop_code"].is<int>() && (doc["revision"].is<long>() || doc["revision"].is<int>())) {
   int code=doc["crop_code"].as<int>();
   long revision=doc["revision"].as<long>();
   if(code>=0 && code<=4 && revision!=lastAppliedRevision) {
    // Revision zero is the server's default unselected crop, not a user reset.
    if(code==0 && revision==0) { selectedCrop=NO_CROP; setPump(false); }
    else if(code==0) resetSystem();
    else selectCrop((Crop)code);
    lastAppliedRevision=revision;
    Serial.printf("Cloud crop command applied: %d, revision %ld\n",code,revision);
   }
  } else Serial.println("Unexpected crop command format");
 } else {
  Serial.printf("Crop command HTTP status: %d\n",status);
  if(status==401 || status==302) Serial.println("Check public Vercel production URL and DEVICE_API_KEY.");
 }
 http.end();
}
void setup() {
 Serial.begin(115200);
 // Preload OFF levels before switching pins to OUTPUT to reduce startup glitches.
 digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
 digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_HIGH ? LOW : HIGH);
 pinMode(RELAY_PIN,OUTPUT);
 pinMode(BUZZER_PIN,OUTPUT);
 relayWrite(false); buzzerWrite(false);
 pinMode(SOIL_PIN,INPUT);
 pinMode(PIR_PIN,INPUT);
 analogReadResolution(12);
 analogSetPinAttenuation(SOIL_PIN,ADC_11db);
 dht.begin();
 bootTime=millis();
 lastPumpOffTime=bootTime;
 lastMotionTime=bootTime;
 WiFi.mode(WIFI_STA);
 if(String(WIFI_SSID)!="YOUR_WIFI_NAME") {
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  lastWiFiAttempt=millis();
 }
 Serial.println("\nFieldWise DHT22 firmware ready. Pump OFF; website crop selection enabled.");
 Serial.println("PIR warming up; pump held OFF initially.");
}
void loop() {
 unsigned long now=millis();
 updateMotion(now);
 checkPumpTimeout(now);
 if(now-lastSensorTime >= SENSOR_INTERVAL) {
  lastSensorTime=now;
  readSensors();
  manageIrrigation(millis());
 }
 maintainWiFi(millis());
 if(millis()-lastCommandTime >= COMMAND_INTERVAL) {
  lastCommandTime=millis();
  pollCropCommand();
 }
 // Recheck outputs after any blocking HTTP call.
 updateMotion(millis());
 checkPumpTimeout(millis());
 manageIrrigation(millis());
 if(millis()-lastCloudTime >= CLOUD_INTERVAL) {
  lastCloudTime=millis();
  uploadToCloud();
 }
 updateMotion(millis());
 checkPumpTimeout(millis());
 delay(5);
}
