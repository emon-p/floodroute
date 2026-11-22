#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// FloodRoute ESP32 + HC-SR04 + I2C LCD + local web dashboard
// TRIG=GPIO5, ECHO=GPIO18 through voltage divider
// LCD SDA=GPIO21, SCL=GPIO22
// Set TANK_DEPTH_CM to your actual sensor-to-tank-bottom distance.

const int TRIG_PIN = 5, ECHO_PIN = 18, SDA_PIN = 21, SCL_PIN = 22;
const float TANK_DEPTH_CM = 20.0;  // CHANGE this to your measured tank depth
const float LOW_LEVEL_CM = 5.0, HIGH_LEVEL_CM = 12.0; // demo-only thresholds

const char* AP_SSID = "FloodRoute-ESP32";
const char* AP_PASS = "Flood1234"; // 8+ characters

WebServer server(80);
LiquidCrystal_I2C lcd(0x27, 16, 2); // If blank, try address 0x3F

float distanceCm = -1, waterLevelCm = 0;
String riskStatus = "WAITING", roadStatus = "WAITING";
unsigned long lastMeasureMs = 0;

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FloodRoute Dashboard</title>
<style>
body{margin:0;background:#0b1220;color:#edf6ff;font-family:Arial,sans-serif}
header{padding:22px;background:#101c30;border-bottom:1px solid #263b55}
h1{margin:0;color:#36d9df}header p,.label,small{color:#a9bbcf}
main{max-width:1000px;margin:22px auto;padding:0 16px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:14px}
.card{background:#142238;border:1px solid #263b55;border-radius:14px;padding:20px}
.label{font-size:14px}.value{font-size:30px;font-weight:bold;margin-top:12px}
.status{font-size:20px;font-weight:bold;margin-top:12px}
.note{margin-top:18px;padding:15px;border-radius:10px;background:#1d2d43;color:#a9bbcf;line-height:1.5}
</style></head><body>
<header><h1>FloodRoute</h1><p>ESP32 live water-level monitoring prototype</p></header>
<main><div class="grid">
<div class="card"><div class="label">Distance to water</div><div class="value" id="distance">-- cm</div></div>
<div class="card"><div class="label">Estimated water level</div><div class="value" id="level">-- cm</div></div>
<div class="card"><div class="label">Prototype risk status</div><div class="status" id="risk">CONNECTING</div></div>
<div class="card"><div class="label">Road status (demo)</div><div class="status" id="road">WAITING</div></div>
</div><div class="note"><b>Prototype limitation:</b> This reads water at one sensor point only.
Risk thresholds are illustrative, not validated for real roads or flood warnings.
No cloud service, city map, or automatic route engine is connected yet.</div>
<p><small>Auto-updates every 2 seconds • ESP32 local Wi-Fi</small></p></main>
<script>
async function update(){try{
const r=await fetch('/data',{cache:'no-store'});const d=await r.json();
document.getElementById('distance').textContent=d.distance_cm===null?'No reading':d.distance_cm.toFixed(1)+' cm';
document.getElementById('level').textContent=d.water_level_cm.toFixed(1)+' cm';
document.getElementById('risk').textContent=d.risk;
document.getElementById('road').textContent=d.road;
}catch(e){document.getElementById('risk').textContent='OFFLINE';}}
update();setInterval(update,2000);
</script></body></html>
)HTML";

String asJsonFloat(float v) { return v < 0 ? "null" : String(v, 1); }

void measureWater() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);

  if (duration == 0) {
    distanceCm = -1; waterLevelCm = 0;
    riskStatus = "NO SENSOR"; roadStatus = "CHECK SENSOR";
    return;
  }

  distanceCm = duration * 0.0343f / 2.0f;
  waterLevelCm = TANK_DEPTH_CM - distanceCm;
  if (waterLevelCm < 0) waterLevelCm = 0;
  if (waterLevelCm > TANK_DEPTH_CM) waterLevelCm = TANK_DEPTH_CM;

  if (waterLevelCm >= HIGH_LEVEL_CM) {
    riskStatus = "HIGH (DEMO)"; roadStatus = "RISKY (DEMO)";
  } else if (waterLevelCm >= LOW_LEVEL_CM) {
    riskStatus = "MEDIUM (DEMO)"; roadStatus = "CAUTION (DEMO)";
  } else {
    riskStatus = "LOW (DEMO)"; roadStatus = "OPEN (DEMO)";
  }
}

void updateLCD() {
  lcd.clear();
  if (distanceCm < 0) {
    lcd.setCursor(0,0); lcd.print("FloodRoute");
    lcd.setCursor(0,1); lcd.print("Sensor: No Echo");
    return;
  }
  lcd.setCursor(0,0); lcd.print("Dist:"); lcd.print(distanceCm,1); lcd.print("cm");
  lcd.setCursor(0,1); lcd.print("Lvl:"); lcd.print(waterLevelCm,1); lcd.print("cm ");
  if (waterLevelCm >= HIGH_LEVEL_CM) lcd.print("HIGH");
  else if (waterLevelCm >= LOW_LEVEL_CM) lcd.print("MED");
  else lcd.print("LOW");
}

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", PAGE); }
void handleData() {
  String json = "{\"distance_cm\":" + asJsonFloat(distanceCm) +
    ",\"water_level_cm\":" + String(waterLevelCm,1) +
    ",\"risk\":\"" + riskStatus + "\",\"road\":\"" + roadStatus + "\"}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init(); lcd.backlight();
  lcd.setCursor(0,0); lcd.print("FloodRoute");
  lcd.setCursor(0,1); lcd.print("Starting...");

  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println("\nFloodRoute ESP32");
  if (ok) {
    Serial.print("Wi-Fi name: "); Serial.println(AP_SSID);
    Serial.print("Password: "); Serial.println(AP_PASS);
    Serial.print("Dashboard: http://"); Serial.println(WiFi.softAPIP());
  } else Serial.println("Wi-Fi AP start failed");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.begin();
  delay(500); measureWater(); updateLCD();
}

void loop() {
  server.handleClient();
  if (millis() - lastMeasureMs >= 1000) {
    lastMeasureMs = millis();
    measureWater(); updateLCD();
    Serial.print("Distance: ");
    if (distanceCm < 0) Serial.print("No reading"); else Serial.print(distanceCm,1);
    Serial.print(" cm | Water level: "); Serial.print(waterLevelCm,1);
    Serial.print(" cm | Risk: "); Serial.println(riskStatus);
  }
}
