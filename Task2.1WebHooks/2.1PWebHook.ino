// 2.1PWebHook 
// DHT22 on DHTPIN (D2), LDR voltage divider midpoint on LDR_PIN (A0) 
// WiFi credentials and ThingSpeak keys stored in arduino_secrets.h  
// ThingSpeak mapping: Field 1 = temperature (°C), Field 2 = brightness (0..100); writes every 30 s 
// DHT22 timing: ~2 s minimum between reads; Nano 33 IoT uses WiFiNINA (2.4 GHz only); Serial @115200

#include "arduino_secrets.h"
#include <WiFiNINA.h>
#include <ThingSpeak.h>
#include <DHT.h>

// Hardware pin assignments
#define DHTPIN 2
#define DHTTYPE DHT22
#define LDR_PIN A0

// Global objects and constants
WiFiClient client;
DHT dht(DHTPIN, DHTTYPE);

// Initialise serial, sensors, WiFi and ThingSpeak
void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  dht.begin();

  // Start WiFi connection using credentials from arduino_secrets.h
  if (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    Serial.println("Connecting to WiFi...");
  }

  // wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  ThingSpeak.begin(client);
}

void loop() {

  // Read temperature from DHT22 (returns Celsius). If the read fails, the library returns NAN.
  float temperature = dht.readTemperature(); // Celsius
  int ldrRaw = analogRead(LDR_PIN); // 0..1023

  // Optional: map to 0..100 relative brightness
  int brightness = map(ldrRaw, 0, 1023, 100, 0); 

  // Print sensor readings to Serial for debugging and verification.
  Serial.print("Temp: "); Serial.print(temperature);
  Serial.print(" C, LDR raw: "); Serial.print(ldrRaw);
  Serial.print(", Brightness: "); Serial.println(brightness);

  // Write to ThingSpeak fields
  ThingSpeak.setField(1, temperature);
  ThingSpeak.setField(2, brightness);
  int response = ThingSpeak.writeFields(channelID, writeAPIKey);
  if (response == 200) {
    Serial.println("ThingSpeak update successful.");
  } else {
    Serial.print("ThingSpeak error: ");
    Serial.println(response);
  }

  delay(30000); // 30 seconds
}
