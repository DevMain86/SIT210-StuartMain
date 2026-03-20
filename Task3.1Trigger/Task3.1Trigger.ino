// Task3.1Trigger
// Terrarium sunlight monitor using BH1750 light sensor
// Sends start/stop webhooks to IFTTT (HTTPS) via WiFiNINA SSL client.
// Secrets in arduino_secrets.h: WIFI_SSID, WIFI_PASS, IFTTT_KEY, IFTTT_EVENT_START, IFTTT_EVENT_STOP
// Board: Arduino Nano 33 IoT (SAMD), BH1750 on I2C on I2C (0x3C).
// Behaviour: read lux every READ_INTERVAL_MS, debounce changes, send IFTTT on START/STOP, show status on OLED.

#include <Wire.h>
#include <BH1750.h>
#include <WiFiNINA.h>
#include "arduino_secrets.h"   // WIFI_SSID, WIFI_PASS, IFTTT_KEY, IFTTT_EVENT_START, IFTTT_EVENT_STOP

// OLED display library (Adafruit SSD1306 + GFX)
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED hardware configuration
#define SCREEN_WIDTH 128         
#define SCREEN_HEIGHT 64            
#define OLED_RESET    -1            
#define OLED_ADDR     0x3C          
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

BH1750 lightMeter;                  // BH1750 light sensor object

// Configuration constants
const char HOST_NAME[] = "maker.ifttt.com";          // IFTTT Webhooks host
const char* EVENT_START = IFTTT_EVENT_START;         // IFTTT event name for sunlight start
const char* EVENT_STOP  = IFTTT_EVENT_STOP;          // IFTTT event name for sunlight stop
const float SUN_THRESHOLD_LUX =
            // Lux threshold to consider "in sunlight" (tune as needed)
const unsigned long READ_INTERVAL_MS = 15000;        // Sensor read interval (ms)
const int DEBOUNCE_REQUIRED = 2;                     // Number of consecutive readings required to confirm a state change

WiFiSSLClient sslClient;            // TLS client provided by WiFiNINA (Nano 33 IoT)

// Runtime state variables
bool isInSunlight = false;        
int consecutiveCount = 0;            
bool lastAbove = false;          
unsigned long sunStartMillis = 0;     
unsigned long lastDisplayUpdate = 0;  

// Helper: format a float lux value as "xxx.x" into a buffer (SAMD-safe, avoids dtostrf)
void formatLux(float lux, char *buf, size_t bufLen) {
  int lux10 = (int)(lux * 10.0 + 0.5); // round to nearest tenth
  int whole = lux10 / 10;
  int frac  = lux10 % 10;
  snprintf(buf, bufLen, "%d.%d", whole, frac);
}

// Connect to WiFi using credentials from arduino_secrets.h
// Waits up to ~20 seconds for association and prints status to Serial.
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;         // already connected
  Serial.print("Connecting to WiFi ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(200);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect timed out");
  }
}

// Build and send an HTTPS GET request to IFTTT Webhooks.
// eventName: IFTTT event (e.g., terrarium_sun_start / terrarium_sun_stop)
// lux: measured lux value (float)
// session_s: session duration in seconds (use 0UL when not applicable)
void sendIFTTT_https(const char* eventName, float lux, unsigned long session_s) {
  // Format lux as one decimal place into a string (no dtostrf)
  int lux10 = (int)(lux * 10.0 + 0.5);
  int lux_whole = lux10 / 10;
  int lux_frac  = lux10 % 10;
  char luxStr[16];
  snprintf(luxStr, sizeof(luxStr), "%d.%d", lux_whole, lux_frac);

  // Build the Webhooks path and full HTTPS URL for debugging
  char path[220];
  snprintf(path, sizeof(path),
           "/trigger/%s/with/key/%s?value1=%s&value2=%lu",
           eventName, IFTTT_KEY, luxStr, session_s);

  char httpsUrl[300];
  snprintf(httpsUrl, sizeof(httpsUrl), "https://%s%s", HOST_NAME, path);

  // Debug output: show exactly what will be sent
  Serial.print("DEBUG luxStr: ");
  Serial.println(luxStr);
  Serial.print("DEBUG HTTPS URL: ");
  Serial.println(httpsUrl);

  // Ensure WiFi is connected before attempting TLS
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Skipping IFTTT send: WiFi not connected");
    return;
  }

  // Attempt TLS connection to IFTTT (port 443)
  if (!sslClient.connect(HOST_NAME, 443)) {
    Serial.println("HTTPS connection failed (sslClient.connect)");
    return;
  }

  // Send a simple HTTP/1.1 GET request over TLS
  sslClient.print("GET ");
  sslClient.print(path);
  sslClient.println(" HTTP/1.1");
  sslClient.print("Host: ");
  sslClient.println(HOST_NAME);
  sslClient.println("User-Agent: Nano33IoT");
  sslClient.println("Connection: close");
  sslClient.println();

  // Read and print response with a short timeout to confirm delivery
  unsigned long start = millis();
  while (sslClient.connected() && (millis() - start) < 5000) {
    while (sslClient.available()) {
      char c = sslClient.read();
      Serial.print(c);
      start = millis(); // extend timeout while data arrives
    }
  }
  sslClient.stop();
  Serial.println();
  Serial.println("IFTTT HTTPS request complete");
}

// Initialise the SSD1306 OLED display. If allocation fails, continue without OLED.
void oledInit() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 allocation failed");
    // continue without OLED
    return;
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.display();
  delay(50);
}

// Render current status on the OLED: lux, state, session duration and WiFi IP.
// lux: current lux reading; inSun: debounced sunlight state; session_s: seconds in current session
void oledShowStatus(float lux, bool inSun, unsigned long session_s) {
  char luxBuf[16];
  formatLux(lux, luxBuf, sizeof(luxBuf));

  display.clearDisplay();

  // Title (small)
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Terrarium Monitor");

  // Large lux readout
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print("Lux: ");
  display.print(luxBuf);

  // State label (SUN / NO SUN)
  display.setTextSize(1);
  display.setCursor(0, 44);
  display.print("State: ");
  display.print(inSun ? "SUN" : "NO SUN");

  // Session duration (seconds) — useful when STOP occurs
  display.setCursor(72, 44);
  display.print("Dur:");
  display.print(session_s);
  display.print("s");

  // WiFi IP or placeholder if disconnected
  display.setTextSize(1);
  display.setCursor(0, 56);
  if (WiFi.status() == WL_CONNECTED) {
    display.print(WiFi.localIP());
  } else {
    display.print("WiFi: -");
  }

  // Push buffer to the display
  display.display();
}

// Setup: initialise I2C, Serial, BH1750, OLED and WiFi
void setup() {
  Wire.begin();
  Serial.begin(115200);
  while (!Serial); // wait for Serial console

  // Initialise BH1750 light sensor; halt if it fails
  if (!lightMeter.begin()) {
    Serial.println("BH1750 init failed");
    while (1) delay(1000);
  }

  // Initialise OLED (optional)
  oledInit();

  // Connect to WiFi (non-blocking beyond the internal timeout)
  connectWiFi();
  Serial.println("Ready");
}

// Main loop: periodic sensor read, debounce logic, IFTTT sends on state change, OLED update
void loop() {
  static unsigned long lastRead = 0;
  unsigned long now = millis();

  // Enforce read interval; return early if not time yet
  if (now - lastRead < READ_INTERVAL_MS) return;
  lastRead = now;

  // Read lux from BH1750 (float, lux)
  float lux = lightMeter.readLightLevel();
  Serial.print("Lux: ");
  Serial.println(lux, 1);

  // Determine whether current reading is above threshold
  bool above = lux >= SUN_THRESHOLD_LUX;

  // Debounce: require DEBOUNCE_REQUIRED consecutive identical comparisons
  if (above == lastAbove) {
    consecutiveCount++;
  } else {
    consecutiveCount = 1;
    lastAbove = above;
  }

  // If debounce satisfied and state differs from current debounced state, toggle and send webhook
  if (consecutiveCount >= DEBOUNCE_REQUIRED && above != isInSunlight) {
    if (above) {
      // Transition: NO SUN -> SUN (START)
      isInSunlight = true;
      sunStartMillis = now;                       // record session start time
      sendIFTTT_https(EVENT_START, lux, 0UL);     // send START event (session not applicable)
      Serial.println("Event: SUN START");
    } else {
      // Transition: SUN -> NO SUN (STOP)
      isInSunlight = false;
      unsigned long session_s = 0;
      if (sunStartMillis) session_s = (now - sunStartMillis) / 1000UL; // compute session seconds
      sendIFTTT_https(EVENT_STOP, lux, session_s); // send STOP event with session duration
      Serial.print("Event: SUN STOP, session_s=");
      Serial.println(session_s);
      sunStartMillis = 0;                         // reset session start
    }
    consecutiveCount = 0;                         // reset debounce counter after handling
  }

  // Update OLED display with current values (session shown while in sun)
  unsigned long session_s_display = 0;
  if (isInSunlight && sunStartMillis) session_s_display = (now - sunStartMillis) / 1000UL;
  oledShowStatus(lux, isInSunlight, session_s_display);

  // Ensure WiFi remains connected; attempt reconnect if dropped
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
}