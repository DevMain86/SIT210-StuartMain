/*
  Nano 33 IoT - HC-SR04 gestures with MQTT
  - Uses WiFiNINA + ArduinoMqttClient
  - Publishes to ES/Wave and ES/Pat (payload includes MY_NAME from arduino_secrets.h)
  - Subscribes to ES/Wave and ES/Pat and toggles two LEDs
  - Distance prints throttled to once every 5 seconds
  - Centralized LED control via setLEDs()
*/

#include <WiFiNINA.h>              
#include <ArduinoMqttClient.h>      
#include "arduino_secrets.h"        

// Broker
const char *mqttBroker = "broker.emqx.io"; 
int mqttPort = 1883;                     

// Pins (adjust if needed)
const int trigPin = 2;     
const int echoPin = 3;  
const int hallwayLED = 8; 
const int bathroomLED = 9; 

// LED polarity: set true if pin HIGH = ON (your wiring uses pin -> resistor -> anode -> GND)
const bool LED_ACTIVE_HIGH = true; // indicates how to drive LEDs in setLEDs()

// Gesture tuning
const float DETECT_DISTANCE = 10.0;        // cm threshold for approach (tune for your mounting)
const unsigned long PAT_THRESHOLD = 1200;  // ms hold time to consider a "pat"
const unsigned long SAMPLE_INTERVAL = 120; // ms between distance reads

// Serial print throttle
const unsigned long PRINT_INTERVAL_MS = 5000; 
unsigned long lastPrintMs = 0;                

WiFiClient wifiClient;                 
MqttClient mqttClient(wifiClient);    

// Gesture state
bool objectPresent = false;           // whether an object is currently considered "present"
unsigned long gestureStartTime = 0;   // timestamp when object was first detected
unsigned long lastSampleMs = 0;       // timestamp of last sensor sample

// LED state
bool ledsOn = false;                 

// Forward declarations for functions used later
float getDistance();
void detectGesture(float distance);
void onMqttMessage(int messageSize);
void setLEDs(bool on);

// setLEDs: centralised LED control that respects wiring polarity
void setLEDs(bool on) {
  // on == true -> turn LEDs ON
  if (LED_ACTIVE_HIGH) {
    digitalWrite(hallwayLED, on ? HIGH : LOW);
    digitalWrite(bathroomLED, on ? HIGH : LOW);
  } else {
    // if LEDs are active-low, invert the logic
    digitalWrite(hallwayLED, on ? LOW : HIGH);
    digitalWrite(bathroomLED, on ? LOW : HIGH);
  }
  ledsOn = on; // keep local state in sync
}

void setup() {
  Serial.begin(115200); 
  delay(2000);          

  // Configure sensor and LED pins
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(hallwayLED, OUTPUT);
  pinMode(bathroomLED, OUTPUT);

  // Ensure known OFF state at startup using the helper
  setLEDs(false);

  // Connect to WiFi using credentials from arduino_secrets.h
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
    // If connection takes too long, retry the attempt
    if (millis() - wifiStart > 30000) {
      Serial.println();
      Serial.println("WiFi connect timeout, retrying...");
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      wifiStart = millis();
    }
  }
  Serial.println(" connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP()); 

  // Create a unique client id for MQTT to avoid collisions
  String clientId = String("nano33iot_") + String(random(0xffff), HEX);
  mqttClient.setId(clientId.c_str());

  // Register the callback that will be invoked when messages arrive
  mqttClient.onMessage(onMqttMessage);

  // Connect to the MQTT broker (blocking retry loop)
  Serial.print("Connecting to MQTT broker");
  while (!mqttClient.connect(mqttBroker, mqttPort)) {
    Serial.print(".");
    delay(5000);
  }
  Serial.println(" connected!");

  // Subscribe to the two topics used by the task
  mqttClient.subscribe("ES/Wave");
  mqttClient.subscribe("ES/Pat");
  Serial.println("Subscribed to ES/Wave and ES/Pat");

  // Publish a startup message to ES/Wave (non-retained)
  mqttClient.beginMessage("ES/Wave");
  mqttClient.print(String(MY_NAME) + " - startup");
  mqttClient.endMessage();
  Serial.println("Published startup test to ES/Wave");
}

void loop() {
  // Keep the MQTT client processing incoming packets and maintaining connection
  mqttClient.poll();

  // If disconnected, attempt to reconnect and resubscribe
  if (!mqttClient.connected()) {
    Serial.println("MQTT disconnected, reconnecting...");
    if (mqttClient.connect(mqttBroker, mqttPort)) {
      mqttClient.subscribe("ES/Wave");
      mqttClient.subscribe("ES/Pat");
      Serial.println("Reconnected and resubscribed");
    } else {
      // If reconnect fails, wait briefly and return to loop to try again
      delay(2000);
      return;
    }
  }

  // Sample the ultrasonic sensor at the configured interval
  unsigned long now = millis();
  if (now - lastSampleMs >= SAMPLE_INTERVAL) {
    lastSampleMs = now;
    float distance = getDistance();

    // Throttled distance print: only print every PRINT_INTERVAL_MS to avoid flooding serial
    if (distance > 0 && distance < 1000 && (now - lastPrintMs >= PRINT_INTERVAL_MS)) {
      lastPrintMs = now;
      Serial.print("Distance: ");
      Serial.print(distance);
      Serial.println(" cm");
    }

    // Run gesture detection logic on the latest distance sample
    detectGesture(distance);
  }

  // Small delay to yield CPU and avoid a tight busy loop
  delay(10);
}

// getDistance: triggers the HC-SR04 and measures echo time, returns distance in cm
float getDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000); // timeout 30ms to avoid blocking forever
  if (duration == 0) return 999.0;               // no echo detected -> return large value
  // convert microsecond duration to centimeters (speed of sound ~0.0343 cm/us)
  return (duration * 0.0343) / 2.0;
}

// detectGesture: simple state machine that detects object appear/disappear and classifies PAT vs WAVE
void detectGesture(float distance) {
  // object appears: distance falls below threshold and we were previously clear
  if (distance < DETECT_DISTANCE && !objectPresent) {
    objectPresent = true;             
    gestureStartTime = millis();       
    Serial.print("Object detected at ");
    Serial.print(distance);
    Serial.println(" cm");
  }
  // object removed: distance rises above threshold and we previously saw an object
  else if (distance >= DETECT_DISTANCE && objectPresent) {
    unsigned long holdTime = millis() - gestureStartTime; 
    objectPresent = false;                               

    Serial.print("Hold time: ");
    Serial.print(holdTime);
    Serial.println(" ms");

    // Classify as PAT if hold time exceeds PAT_THRESHOLD
    if (holdTime >= PAT_THRESHOLD) {
      Serial.println(">>> PAT detected!");
      String message = String(MY_NAME) + " - Pat detected";
      mqttClient.beginMessage("ES/Pat");
      mqttClient.print(message);
      mqttClient.endMessage();
      Serial.println("Published to ES/Pat");
      // reflect the action locally by turning LEDs off
      setLEDs(false);
    }
    // Otherwise, if there was a short presence, classify as WAVE
    else if (holdTime > 50) {
      Serial.println(">>> WAVE detected!");
      String message = String(MY_NAME) + " - Wave detected";
      mqttClient.beginMessage("ES/Wave");
      mqttClient.print(message);
      mqttClient.endMessage();
      Serial.println("Published to ES/Wave");
      // reflect the action locally by turning LEDs on
      setLEDs(true);
    }

    delay(1500); // debounce to avoid immediate re-detection after a gesture
  }
}

// onMqttMessage: called by the MQTT client when a message arrives on a subscribed topic
void onMqttMessage(int messageSize) {
  String topic = mqttClient.messageTopic(); // get the topic string
  String message = "";
  // read the full payload into a String
  while (mqttClient.available()) {
    message += (char)mqttClient.read();
  }

  // Print received topic and payload for debugging
  Serial.print(">>> Received on ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  // React to the two topics by setting LEDs accordingly
  if (topic == "ES/Wave") {
    setLEDs(true); // turn LEDs on when a Wave message is received
    Serial.println("LEDs ON (Wave)");
  } else if (topic == "ES/Pat") {
    setLEDs(false); // turn LEDs off when a Pat message is received
    Serial.println("LEDs OFF (Pat)");
  }
}
