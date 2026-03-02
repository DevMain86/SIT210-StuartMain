// Task1.1Lights_ON - commented version
// Button on D2 (to GND, using INPUT_PULLUP), Porch LED on D5, Hallway LED on D6
// Porch: 30 seconds, Hallway: 60 seconds
// Non-blocking timers and debounced button; pressing restarts both timers.

#include <Arduino.h>


// Pin and timing constants

// Hardware pin assignments (single place to change wiring if needed)
const uint8_t PIN_BUTTON = 2;
const uint8_t PIN_PORCH  = 5;
const uint8_t PIN_HALL   = 6;

// Timing constants (clear, named durations)
const unsigned long PORCH_MS   = 30UL * 1000UL; // 30 seconds
const unsigned long HALL_MS    = 60UL * 1000UL; // 60 seconds
const unsigned long DEBOUNCE_MS = 50UL;         // debounce window (ms)


// Runtime state variables

// Track last raw button change time and state for debounce logic
unsigned long lastButtonChange = 0;
bool lastButtonState = HIGH;    // INPUT_PULLUP: HIGH when idle

// Timer end timestamps and active flags for each LED
unsigned long porchEndTime = 0;
unsigned long hallEndTime  = 0;
bool porchActive = false;
bool hallActive  = false;


// Hardware setup

// Encapsulate pinMode and initial output states so setup() stays tidy
void setupPins() {
  pinMode(PIN_PORCH, OUTPUT);        // porch LED output
  pinMode(PIN_HALL, OUTPUT);         // hallway LED output
  pinMode(PIN_BUTTON, INPUT_PULLUP); // button input with internal pull-up

  // Ensure LEDs start OFF
  digitalWrite(PIN_PORCH, LOW);
  digitalWrite(PIN_HALL, LOW);
}


// Input handling: debounced button read

// Returns true once when a clean press (HIGH -> LOW) is detected.
// Debouncing is handled here so the rest of the code receives a single, reliable event.
bool readButtonDebounced() {
  bool raw = digitalRead(PIN_BUTTON);          // read raw pin (HIGH idle, LOW pressed)
  if (raw != lastButtonState) {                // raw state changed
    lastButtonChange = millis();               // note time of change
    lastButtonState = raw;                     // update last observed raw state
  }

  // If the raw state has been stable for the debounce window, consider it valid
  if ((millis() - lastButtonChange) >= DEBOUNCE_MS) {
    // Use a static variable to remember the last reported stable state
    static bool reportedState = HIGH;
    if (lastButtonState != reportedState) {    // stable state differs from reported
      reportedState = lastButtonState;         // update reported state
      if (reportedState == LOW) return true;   // falling edge = press event
    }
  }
  return false; // no new press event
}


// Activation functions (single responsibility)

// Each function sets the LED HIGH and computes the future end time.
// Keeping activation logic here makes it easy to change behavior (e.g., add logging).
void activatePorch() {
  porchEndTime = millis() + PORCH_MS; // compute when to turn off
  porchActive = true;                 // mark active
  digitalWrite(PIN_PORCH, HIGH);      // turn LED on
}

void activateHall() {
  hallEndTime = millis() + HALL_MS;   // compute when to turn off
  hallActive = true;                  // mark active
  digitalWrite(PIN_HALL, HIGH);       // turn LED on
}


// Timer update (non-blocking timing)

// Called frequently from loop(); checks whether each LED's end time has passed.
// Using millis() avoids blocking delays so both timers can run concurrently.
void updateTimers() {
  unsigned long now = millis();

  // When now >= porchEndTime, turn porch LED off
  if (porchActive && (long)(now - porchEndTime) >= 0) {
    porchActive = false;
    digitalWrite(PIN_PORCH, LOW);
  }

  // When now >= hallEndTime, turn hallway LED off
  if (hallActive && (long)(now - hallEndTime) >= 0) {
    hallActive = false;
    digitalWrite(PIN_HALL, LOW);
  }
}


// Arduino lifecycle

void setup() {
  setupPins(); // configure hardware
}

void loop() {
  // 1) Poll the debounced button; on a valid press restart both timers
  if (readButtonDebounced()) {
    activatePorch();
    activateHall();
  }

  // 2) Maintain timers and turn LEDs off when their durations expire
  updateTimers();

  // 3) Small delay to keep loop responsive but avoid busy-waiting
  delay(5);
}
