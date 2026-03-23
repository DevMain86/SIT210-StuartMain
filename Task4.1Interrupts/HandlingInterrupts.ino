/*
  Task4.1Interrupts - Nano 33 IoT compatible
  PIR on PIR_PIN (interrupt)
  Slider on SLIDER_PIN (interrupt, INPUT_PULLUP)
  BH1750 on I2C (SDA/A4, SCL/A5)
  Two LEDs on LED1_PIN and LED2_PIN
*/

#include <Wire.h>
#include <BH1750.h>

// Pin assignments
const uint8_t PIR_PIN     = 2;
const uint8_t SLIDER_PIN  = 3;
const uint8_t LED1_PIN    = 8;
const uint8_t LED2_PIN    = 9;

// ISR flags and counters 
volatile bool pirFlag = false;
volatile bool sliderFlag = false;
volatile unsigned long pirCount = 0;
volatile unsigned long sliderCount = 0;

// Runtime state and timing
bool lightsOn = false;
unsigned long lastActivityMillis = 0;
const unsigned long AUTO_OFF_MS = 5000UL; // auto-off timeout in milliseconds

// Light sensor (BH1750) state and threshold
BH1750 lightMeter;
bool bhReady = false;
const float LUX_THRESHOLD = 50.0;         // threshold below which we consider it dark
const bool ASSUME_DARK_IF_NO_SENSOR = true; // fallback if BH1750 fails

// --- Interrupt Service Routines ---
// Keep ISRs minimal: set a flag and increment a counter.
void pirISR() {
  pirFlag = true;
  pirCount++;
}

void sliderISR() {
  sliderFlag = true;
  sliderCount++;
}

// Helper to switch both LEDs and update state
void setLights(bool on) {
  digitalWrite(LED1_PIN, on ? HIGH : LOW);
  digitalWrite(LED2_PIN, on ? HIGH : LOW);
  lightsOn = on;
}

// Safe BH1750 read wrapper
// Returns lux or -1.0 on error; marks BH1750 as unavailable on read error.
float safeReadLux() {
  if (!bhReady) return -1.0;
  float lux = lightMeter.readLightLevel();
  if (lux < 0) {
    bhReady = false;
    Serial.println("[BH1750] read error - disabling BH1750 until reset");
    return -1.0;
  }
  return lux;
}

void setup() {
  Serial.begin(115200);
  delay(50);

  // LEDs
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  setLights(false); // ensure lights start OFF

  // Inputs
  pinMode(PIR_PIN, INPUT);
  pinMode(SLIDER_PIN, INPUT_PULLUP); // slider uses internal pull-up; LOW = ON

  // Initialize I2C and BH1750
  Wire.begin();
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    lightMeter.configure(BH1750::CONTINUOUS_HIGH_RES_MODE);
    bhReady = true;
    Serial.println("[BH1750] initialized and configured");
  } else {
    bhReady = false;
    Serial.println("[BH1750] init failed. Check wiring/power");
  }

  // Attach interrupts: PIR on rising edge, slider on any change
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING);
  attachInterrupt(digitalPinToInterrupt(SLIDER_PIN), sliderISR, CHANGE);

  Serial.println("Setup complete.");
  lastActivityMillis = millis();
}

void loop() {
  // Periodic debug print (every 10s)
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 10000) {
    lastDebug = millis();
    Serial.print("Debug: pirCount=");
    Serial.print(pirCount);
    Serial.print(" sliderCount=");
    Serial.print(sliderCount);
    Serial.print(" lightsOn=");
    Serial.println(lightsOn ? "YES" : "NO");
  }

  // Handle PIR event signalled by ISR
  if (pirFlag) {
    pirFlag = false;                     // clear flag set in ISR
    float lux = safeReadLux();           // read ambient light (if available)
    if (lux < 0) {
      Serial.println("[BH1750] unavailable at PIR event");
      if (ASSUME_DARK_IF_NO_SENSOR) lux = 0; // optional fallback
    }
    Serial.print("PIR triggered. lux=");
    Serial.println(lux);

    // If it's dark, turn lights on (or reset timer if already on)
    if (lux < LUX_THRESHOLD) {
      lastActivityMillis = millis();
      if (!lightsOn) {
        setLights(true);
        Serial.println("Motion detected — lights ON (dark).");
      } else {
        Serial.println("Motion detected — lights already ON; timer reset.");
      }
    } else {
      Serial.println("Motion detected but ambient light sufficient; no action.");
    }
  }

  // Handle slider event signalled by ISR (with simple debounce)
  if (sliderFlag) {
    sliderFlag = false;
    static unsigned long lastSliderMillis = 0;
    unsigned long now = millis();
    if (now - lastSliderMillis >= 200) { // debounce window
      lastSliderMillis = now;
      int s = digitalRead(SLIDER_PIN);
      if (s == LOW) { // slider ON (INPUT_PULLUP)
        lastActivityMillis = now;
        if (!lightsOn) {
          setLights(true);
          Serial.println("Slider ON -> lights ON (manual, timer started).");
        } else {
          Serial.println("Slider ON -> lights already ON; timer reset.");
        }
      } else {
        Serial.println("Slider OFF -> manual request ended; lights will auto-off when timer expires.");
      }
    } else {
      Serial.println("Slider event ignored (debounce).");
    }
  }

  // Auto-off logic based on last activity timestamp
  if (lightsOn) {
    unsigned long now = millis();
    if ((now - lastActivityMillis) >= AUTO_OFF_MS) {
      setLights(false);
      Serial.println("Auto-off: no activity -> lights OFF");
    }
  }

  // Periodic status print (every 10s)
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 10000) {
    lastStatus = millis();
    float lux = safeReadLux();
    Serial.print("Status: lightsOn=");
    Serial.print(lightsOn ? "YES" : "NO");
    Serial.print("  lux=");
    if (lux >= 0) Serial.println(lux);
    else Serial.println("N/A");
  }

  delay(10); // small delay to keep loop responsive but not busy
}