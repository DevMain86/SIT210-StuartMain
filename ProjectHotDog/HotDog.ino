#include <Arduino.h>
#include <DHT.h>
#include <ArduinoJson.h>

// PIN DEFINITIONS
// Pin assignments for sensors and fan control. 
#define DHTPIN          2
#define DHTTYPE         DHT22
#define TRIG_PIN        3
#define ECHO_PIN        4
#define FAN_SIGNAL_PIN  9  

// TIMING
// Serial and timing constants
// TELEMETRY_INTERVAL_MS: how often telemetry is sent to the Pi.
// DHT_INTERVAL_MS: minimum interval between DHT reads (DHT22 requires 2s).
// HEARTBEAT_TIMEOUT_MS: how long the Arduino will wait for a Pi heartbeat before falling back to autonomous thermostat mode.
// PRESENCE_PERSIST_MS: how long a detected presence is considered valid after last trigger.
// PI_COMMAND_OVERRIDE_MS: how long a Pi command keeps the Arduino in Pi-controlled mode.
#define SERIAL_BAUD               115200
const unsigned long TELEMETRY_INTERVAL_MS   = 1000UL;
const unsigned long DHT_INTERVAL_MS         = 2500UL;
const unsigned long HEARTBEAT_TIMEOUT_MS    = 5000UL;
const unsigned long PRESENCE_PERSIST_MS     = 10UL * 60UL * 1000UL;  // 10 minutes
const unsigned long PI_COMMAND_OVERRIDE_MS  = 5000UL;

// TEMPERATURE THRESHOLDS (°C) 
// TEMP_THRESHOLD: minimum temperature at which the fan logic will consider turning on.
// T1/T2/T3: tiered thresholds used to select LOW/MED/HIGH fan levels when temperature is high.
// FALLBACK_TEMP_THRESHOLD: temp-only activation point used when the Pi is offline
//   and presence can no longer be confirmed by the camera (raised so the fan only
//   runs when it is genuinely warm).
const int TEMP_THRESHOLD = 15;          // minimum temp to activate fan
const int T1 = 28;                       // >= T1  → FAN_LOW
const int T2 = 30;                       // >= T2  → FAN_MED
const int T3 = 32;                       // >= T3  → FAN_HIGH
const int FALLBACK_TEMP_THRESHOLD = 26;  // temp-only activation when Pi is offline

// FAN LEVELS
// FanLevel is an enum for readability. The sketch uses digital on/off control:
// any non-zero level is treated as ON (digital HIGH) and zero as OFF (digital LOW).
enum class FanLevel : uint8_t { FAN_OFF = 0, FAN_LOW = 1, FAN_MED = 2, FAN_HIGH = 3 };

// ULTRASONIC TUNING
// Parameters controlling ultrasonic baseline, detection delta, and debounce.
const int   ULTRA_DETECT_CM       = 120;
const int   BASELINE_MIN_VALID_CM = 30;
const float BASELINE_ALPHA        = 0.15f;
const int   PRESENCE_DELTA_CM     = 25;
const int   ULTRA_REQUIRED        = 2;   

// STATE 
// Global state variables for sensors, timing, and control.
DHT dht(DHTPIN, DHTTYPE);

unsigned long lastTelemetry   = 0;
unsigned long lastDHTRead     = 0;
unsigned long lastHeartbeat   = 0;
unsigned long lastPresenceTime = 0;
unsigned long lastPiCommand   = 0;

FanLevel currentFan  = FanLevel::FAN_OFF;
float    lastTemp    = NAN;
int      occupantCount = 0;

float ultra_baseline = -1.0f;
int   ultra_counter  = 0;


// HELPERS

// sendTelemetry: build a small JSON object and send it over Serial.
// The telemetry object contains timestamp, temperature,
// ultrasonic reading and baseline, camera occupant count, current fan level,
// and the active control mode (supervised or fallback).
// The Pi expects newline-terminated JSON lines.
void sendTelemetry(long dist) {
  StaticJsonDocument<256> doc;
  JsonObject t = doc.createNestedObject("telemetry");
  t["ts_ms"]         = millis();
  if (!isnan(lastTemp)) t["temp"] = lastTemp;
  t["ultra_cm"]      = dist;
  t["ultra_baseline"] = (ultra_baseline > 0.0f) ? ultra_baseline : -1;
  t["count"]         = occupantCount;
  t["fan_level"]     = (int)currentFan;
  // Report control mode so the Pi/dashboard can see when we are in fallback
  bool pi_alive = (millis() - lastHeartbeat) < HEARTBEAT_TIMEOUT_MS;
  t["mode"]          = pi_alive ? "supervised" : "fallback";
  serializeJson(doc, Serial);
  Serial.println();
}

// readUltrasonicCm: trigger the ultrasonic sensor and measure echo time.
// Returns -1 on timeout/error, otherwise distance in cm.
long readUltrasonicCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  // Reduced timeout from 30 ms to 10 ms (170 cm max range)
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 10000UL);
  if (duration == 0) return -1;
  long cm = (long)(duration / 29UL / 2UL);
  return cm;
}

// setFanLevel: digital on/off control for the fan driver.
// Any non-zero FanLevel sets the control pin HIGH; FAN_OFF sets it LOW.
// Serial debug prints show the action for easier troubleshooting.
void setFanLevel(FanLevel level) {
  currentFan = level;
  if (level == FanLevel::FAN_OFF) {
    digitalWrite(FAN_SIGNAL_PIN, LOW);
    Serial.println("[DBG] Fan OFF (digital LOW)");
  } else {
    digitalWrite(FAN_SIGNAL_PIN, HIGH);
    Serial.print("[DBG] Fan ON (digital HIGH) level=");
    Serial.println((int)level);
  }
}

// runFallbackThermostat: autonomous mode used when the Pi heartbeat is lost.
// With the Pi offline the camera can no longer confirm presence, so the fan
// runs on temperature alone using FALLBACK_TEMP_THRESHOLD. This keeps the pet
// cooled when warm while ensuring the fan is never left running uncontrolled.
void runFallbackThermostat() {
  FanLevel desired = FanLevel::FAN_OFF;
  if (!isnan(lastTemp) && lastTemp >= FALLBACK_TEMP_THRESHOLD) {
    if      (lastTemp >= T3) desired = FanLevel::FAN_HIGH;
    else if (lastTemp >= T2) desired = FanLevel::FAN_MED;
    else                     desired = FanLevel::FAN_LOW;
  }
  setFanLevel(desired);
}


// COMMAND HANDLER
// handleCommand parses newline-terminated JSON commands from the Pi.
// Supported commands:
//  - heartbeat: updates lastHeartbeat so watchdog stays in supervised mode.
//  - set_fan: immediate fan override from Pi (also updates lastPiCommand).
//  - set_count: camera-provided occupant count (does NOT update lastPiCommand).
//  - presence: explicit presence flag (sets lastPresenceTime).
//  - calibrate_ultra: set or sample ultrasonic baseline.
void handleCommand(const String &line) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) return;
  if (!doc.containsKey("cmd")) return;

  const char* cmd = doc["cmd"];

  if (strcmp(cmd, "heartbeat") == 0) {
    // Pi heartbeat keeps Arduino in supervised mode (not fallback)
    lastHeartbeat = millis();
    Serial.println("[DBG] got heartbeat");

  } else if (strcmp(cmd, "set_fan") == 0 && doc.containsKey("level")) {
    // Pi explicitly requests a fan level (0..3). This is treated as a Pi override.
    int lvl = doc["level"];
    if (lvl >= 0 && lvl <= 3) {
      setFanLevel((FanLevel)lvl);
      // Only set lastPiCommand on set_fan, not set_count.
      // Previously, set_count also reset lastPiCommand which suppressed the
      // Arduino's own autonomous fan logic even when the Pi only sent a count.
      lastPiCommand = millis();
      Serial.print("[DBG] set_fan cmd -> ");
      Serial.println(lvl);
    }

  } else if (strcmp(cmd, "set_count") == 0 && doc.containsKey("count")) {
    // Camera reports occupant count; used by autonomous logic but does not
    // count as a Pi override (so Arduino can still run its own policy).
    occupantCount = doc["count"];
    // lastPiCommand intentionally NOT updated here (see note above)
    Serial.print("[DBG] set_count -> ");
    Serial.println(occupantCount);

  } else if (strcmp(cmd, "presence") == 0 && doc.containsKey("value")) {
    // Explicit presence signal (rarely used now that PIR is removed).
    int v = doc["value"];
    lastPresenceTime = v ? millis() : 0;
    Serial.print("[DBG] presence -> ");
    Serial.println(v);

  } else if (strcmp(cmd, "calibrate_ultra") == 0) {
    // Calibrate ultrasonic baseline either from a provided distance or by sampling now.
    if (doc.containsKey("dist")) {
      float d = doc["dist"];
      if (d > 0) {
        ultra_baseline = d;
        Serial.print("[DBG] ultra_baseline set -> ");
        Serial.println(ultra_baseline);
      }
    } else {
      long d = readUltrasonicCm();
      if (d > 0) {
        ultra_baseline = (float)d;
        Serial.print("[DBG] ultra_baseline sampled -> ");
        Serial.println(ultra_baseline);
      }
    }
  }
}


// SETUP
// Configure serial, sensors, and control pin. Ensure fan is off at startup.
void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.setTimeout(50);
  dht.begin();

  pinMode(TRIG_PIN,       OUTPUT);
  pinMode(ECHO_PIN,       INPUT);
  pinMode(FAN_SIGNAL_PIN, OUTPUT);
  digitalWrite(FAN_SIGNAL_PIN, LOW);  // ensure fan is off at startup

  lastTelemetry  = millis();
  lastDHTRead    = millis();
  lastHeartbeat  = millis();  // start in supervised mode, not fallback
  lastPresenceTime = 0;

  Serial.println("[INFO] HotDog controller starting");
}


// LOOP
// Main loop: process incoming serial commands, read sensors on a schedule,
// update ultrasonic baseline and presence debounce, and select one of three
// control modes (fallback when Pi offline, Pi-controlled, or Pi-supervised).
void loop() {
  // Read incoming serial commands from Pi
  while (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    handleCommand(line);
  }

  // Heartbeat freshness is evaluated in the 1 Hz block below (mode selection)

  // DHT22 read (max 1 read per 2.5 s per datasheet)
  if (millis() - lastDHTRead >= DHT_INTERVAL_MS) {
    lastDHTRead = millis();
    float t = dht.readTemperature();
    if (!isnan(t)) lastTemp = t;
  }

  // Telemetry + sensor polling at 1 Hz
  if (millis() - lastTelemetry >= TELEMETRY_INTERVAL_MS) {
    lastTelemetry = millis();

    long dist = readUltrasonicCm();

    // Ultrasonic baseline update (only when no occupant detected by camera)
    // Baseline seeding: first valid reading while empty sets baseline directly.
    // EMA update: while empty, baseline slowly adapts to gradual changes.
    if (dist > 0 && dist < ULTRA_DETECT_CM) {
      if (ultra_baseline < 0.0f && occupantCount == 0) {
        // First valid reading while empty → set baseline directly
        ultra_baseline = (float)dist;
      } else if (ultra_baseline >= 0.0f && occupantCount == 0) {
        // Update baseline with exponential moving average while empty
        ultra_baseline = BASELINE_ALPHA * (float)dist
                         + (1.0f - BASELINE_ALPHA) * ultra_baseline;
      }

      // Debounced presence detection: increment counter when reading is
      // significantly closer than baseline (baseline - PRESENCE_DELTA_CM).
      if (ultra_baseline >= BASELINE_MIN_VALID_CM
          && dist < (ultra_baseline - PRESENCE_DELTA_CM)) {
        ultra_counter++;
      } else {
        ultra_counter = 0;
      }
    } else {
      ultra_counter = 0;
    }

    // If ultrasonic condition holds for ULTRA_REQUIRED consecutive samples,
    // update lastPresenceTime so presence persists for PRESENCE_PERSIST_MS.
    if (ultra_counter >= ULTRA_REQUIRED) {
      lastPresenceTime = millis();
    }

    // Control mode selection (priority order):
    //  - fallback: Pi heartbeat lost → run standalone temperature-only thermostat
    //  - Pi-controlled: Pi issued a recent set_fan → hold its commanded level
    //  - Pi-supervised: presence-gated temperature control (normal operation)
    bool pi_alive          = (millis() - lastHeartbeat) < HEARTBEAT_TIMEOUT_MS;
    bool pi_control_active = (millis() - lastPiCommand) < PI_COMMAND_OVERRIDE_MS;

    if (!pi_alive) {
      // Pi offline — run as a standalone temperature-only thermostat
      runFallbackThermostat();
      Serial.println("[DBG] MODE: fallback thermostat (Pi heartbeat lost)");

    } else if (pi_control_active) {
      // Pi is in control — reassert the last commanded level
      setFanLevel(currentFan);

    } else {
      // Pi-supervised autonomous control
      // Determine whether presence is currently considered true
      bool present = (millis() - lastPresenceTime) < PRESENCE_PERSIST_MS;
      FanLevel desired = FanLevel::FAN_OFF;

      // Only consider turning the fan on if temperature is above TEMP_THRESHOLD
      // and presence is detected. Then choose a level based on T1/T2/T3.
      if (!isnan(lastTemp) && lastTemp >= TEMP_THRESHOLD && present) {
        if      (lastTemp >= T3) desired = FanLevel::FAN_HIGH;
        else if (lastTemp >= T2) desired = FanLevel::FAN_MED;
        else if (lastTemp >= T1) desired = FanLevel::FAN_LOW;
      }

      // Increase fan level if multiple occupants are present
      if (occupantCount > 1 && desired != FanLevel::FAN_OFF) {
        int bumped = min((int)FanLevel::FAN_HIGH, (int)desired + 1);
        desired = (FanLevel)bumped;
      }

      // Short-range boost: if subject is very close, bump the level
      if (dist >= 0 && dist < 50 && desired != FanLevel::FAN_OFF) {
        int bumped = min((int)FanLevel::FAN_HIGH, (int)desired + 1);
        desired = (FanLevel)bumped;
      }

      // Apply autonomous decision
      setFanLevel(desired);
    }

    // Send telemetry to Pi for logging and decision-making on the Pi side
    sendTelemetry(dist);
  }
}
  }
}
