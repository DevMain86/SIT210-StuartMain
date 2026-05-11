// voice_control_ble.ino
// BLE-enabled version of voice_control.ino
// Requires ArduinoBLE library (install via Library Manager)

#include <ArduinoBLE.h>
#include <Servo.h>

// Pin assignments for actuators and sensor
const int BATH_LED_PIN = 7;        // digital pin controlling bathroom LED
const int HALL_LED_PIN = 6;        // digital pin controlling hallway LED
const int LIGHT_SENSOR_PIN = A0;   // analog pin reading LDR voltage divider
const int SERVO_PIN = 8;           // PWM pin for servo signal

// Servo object and positions used to simulate fan on/off
Servo fanServo;
const int SERVO_ON_POS = 90;   // servo angle when "fan" is on (visible movement)
const int SERVO_OFF_POS = 0;   // servo angle when "fan" is off

// Timing and light sampling state
unsigned long lastLightSampleMs = 0;
const unsigned long LIGHT_SAMPLE_INTERVAL = 500; // sample every 500 ms
int lastLightValue = 0;                           // stores last averaged light reading

// Simple moving-average buffer to smooth noisy analog readings from LDR
const int LIGHT_SAMPLES = 6;
int lightBuf[LIGHT_SAMPLES];
int lightIdx = 0;
bool lightBufFilled = false;

// BLE service and characteristics UUIDs
// These UUIDs must match the client (RPi) code to communicate over BLE.
const char* SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0";
const char* CMD_CHAR_UUID = "12345678-1234-5678-1234-56789abcdef1";   
const char* STATUS_CHAR_UUID = "12345678-1234-5678-1234-56789abcdef2"; 

// Create BLE service and characteristics objects
BLEService voiceService(SERVICE_UUID);
BLECharacteristic cmdChar(CMD_CHAR_UUID, BLEWrite, 64);      // central writes commands here
BLECharacteristic statusChar(STATUS_CHAR_UUID, BLENotify, 128); // Arduino notifies status here

void setup() {
  // Initialize USB Serial for debugging at 115200 baud.
  // Serial is still useful for development even when BLE is used.
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { /* wait briefly for Serial */ }

  // Configure LED pins as outputs and ensure they start LOW (off)
  pinMode(BATH_LED_PIN, OUTPUT);
  pinMode(HALL_LED_PIN, OUTPUT);
  digitalWrite(BATH_LED_PIN, LOW);
  digitalWrite(HALL_LED_PIN, LOW);

  // Attach servo to its pin and set to OFF position
  fanServo.attach(SERVO_PIN);
  fanServo.write(SERVO_OFF_POS);

  // Initialize the light buffer with initial analog readings to avoid spikes
  for (int i = 0; i < LIGHT_SAMPLES; ++i) lightBuf[i] = analogRead(LIGHT_SENSOR_PIN);

  // Initialize BLE stack and advertise the service
  // If BLE.begin() fails, the sketch will still run but BLE features won't be available.
  if (!BLE.begin()) {
    Serial.println("BLE init failed");
  } else {
    // Set the advertised local name and the service to advertise
    BLE.setLocalName("VoiceNode");
    BLE.setAdvertisedService(voiceService);

    // Add characteristics to the service and register the service with the BLE stack
    voiceService.addCharacteristic(cmdChar);
    voiceService.addCharacteristic(statusChar);
    BLE.addService(voiceService);

    // Initialize characteristic values (empty)
    cmdChar.writeValue((const unsigned char*)"", 0);
    statusChar.writeValue((const unsigned char*)"", 0);

    // Register a handler that will be called when the central writes to cmdChar
    cmdChar.setEventHandler(BLEWritten, onCmdWritten);

    // Start advertising so a central (RPi) can discover and connect
    BLE.advertise();
    Serial.println("BLE advertising as VoiceNode");
  }

  // Small delay and signal readiness over Serial
  delay(200);
  Serial.println("READY");
}

// Buffer for reading tokens from Serial (USB) for debugging
String lastCmdFromSerial = "";

// Read a newline-terminated token from USB Serial if available
String readTokenFromSerial() {
  if (!Serial.available()) return "";
  String s = Serial.readStringUntil('\n');
  s.trim();
  return s;
}

// Read the light sensor and update the moving-average buffer
// Returns the averaged light value and updates lastLightValue
int sampleLight() {
  int v = analogRead(LIGHT_SENSOR_PIN);
  lightBuf[lightIdx++] = v;
  if (lightIdx >= LIGHT_SAMPLES) {
    lightIdx = 0;
    lightBufFilled = true;
  }
  int count = lightBufFilled ? LIGHT_SAMPLES : lightIdx;
  long sum = 0;
  for (int i = 0; i < count; ++i) sum += lightBuf[i];
  int avg = (int)(sum / count);
  lastLightValue = avg;
  return avg;
}

// Build and send a status string describing current actuator states and light level.
// The status is printed to Serial for debugging and sent as a BLE notification if connected.
void sendStatus() {
  String st = "STATUS BATH:";
  st += (digitalRead(BATH_LED_PIN) ? "ON" : "OFF");
  st += ";HALL:";
  st += (digitalRead(HALL_LED_PIN) ? "ON" : "OFF");
  st += ";FAN:";
  st += (fanServo.read() > 0 ? "ON" : "OFF");
  st += ";LIGHT:";
  st += String(lastLightValue);

  // Print to Serial for debugging and logging
  Serial.println(st);

  // If a BLE central is connected and subscribed, send the status as a notification
  if (BLE.connected()) {
    // Convert String to C string and write as bytes to the characteristic
    const char* buf = st.c_str();
    statusChar.writeValue((const unsigned char*)buf, strlen(buf));
  }
}

// Handle a received token and perform the corresponding action.
// After handling, send an updated status.
void handleToken(const String &token) {
  if (token == "BATH_ON") {
    digitalWrite(BATH_LED_PIN, HIGH);
    Serial.println("ACK BATH_ON");
  } else if (token == "BATH_OFF") {
    digitalWrite(BATH_LED_PIN, LOW);
    Serial.println("ACK BATH_OFF");
  } else if (token == "HALL_ON") {
    digitalWrite(HALL_LED_PIN, HIGH);
    Serial.println("ACK HALL_ON");
  } else if (token == "HALL_OFF") {
    digitalWrite(HALL_LED_PIN, LOW);
    Serial.println("ACK HALL_OFF");
  } else if (token == "FAN_SIM_ON") {
    fanServo.write(SERVO_ON_POS);
    Serial.println("ACK FAN_SIM_ON");
  } else if (token == "FAN_SIM_OFF") {
    fanServo.write(SERVO_OFF_POS);
    Serial.println("ACK FAN_SIM_OFF");
  } else if (token == "STATUS_REQ") {
    // STATUS_REQ triggers an immediate status report (no extra ACK)
    sendStatus();
    return;
  } else {
    // Unknown token received
    Serial.println("ERR UNKNOWN");
    return;
  }

  // After handling a valid command, send updated status to central and Serial
  sendStatus();
}

// BLE write handler: called when a central writes to cmdChar
// This reads the written bytes, converts to a String token, and passes it to handleToken.
void onCmdWritten(BLEDevice central, BLECharacteristic characteristic) {
  // Determine how many bytes were written
  int len = characteristic.valueLength();
  if (len <= 0) return;

  // Read into a local buffer (ensure room for null terminator)
  char buf[65];
  int readLen = characteristic.readValue((unsigned char*)buf, sizeof(buf)-1);
  if (readLen <= 0) return;
  buf[readLen] = '\0';

  // Convert to Arduino String, trim whitespace, and handle the token
  String token = String(buf);
  token.trim();
  Serial.print("BLE CMD: ");
  Serial.println(token);
  handleToken(token);
}

void loop() {
  // Poll BLE stack to handle connections, events, and callbacks
  BLE.poll();

  // Periodically sample the light sensor at the configured interval
  if (millis() - lastLightSampleMs >= LIGHT_SAMPLE_INTERVAL) {
    sampleLight();
    lastLightSampleMs = millis();
  }

  // Also accept commands over USB Serial for debugging and manual control
  String token = readTokenFromSerial();
  if (token.length() > 0) {
    Serial.print("Serial CMD: ");
    Serial.println(token);
    handleToken(token);
  }

  // static unsigned long lastNotify = 0;
  // if (BLE.connected() && millis() - lastNotify > 5000) {
  //   sendStatus();
  //   lastNotify = millis();
  // }
}
