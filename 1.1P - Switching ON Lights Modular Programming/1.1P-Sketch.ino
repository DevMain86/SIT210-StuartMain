// Task1.1Lights_ON
// Hardware: Button on D2 (to GND, using INPUT_PULLUP), Porch LED on D5, Hallway LED on D6
// Porch: 30 seconds, Hallway: 60 seconds
// Behavior: button press starts (or restarts) both timers; non-blocking timing; debounced button.

const uint8_t PIN_BUTTON = 2;
const uint8_t PIN_PORCH  = 5;
const uint8_t PIN_HALL   = 6;

const unsigned long PORCH_MS   = 30UL * 1000UL; // 30 seconds
const unsigned long HALL_MS    = 60UL * 1000UL; // 60 seconds
const unsigned long DEBOUNCE_MS = 50UL;         // debounce window

// runtime state
unsigned long lastButtonChange = 0;
bool lastButtonState = HIGH;    // INPUT_PULLUP: HIGH when idle
bool buttonPressedEvent = false;

unsigned long porchEndTime = 0;
unsigned long hallEndTime  = 0;
bool porchActive = false;
bool hallActive  = false;

void setupPins() {
  pinMode(PIN_PORCH, OUTPUT);
  pinMode(PIN_HALL, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  digitalWrite(PIN_PORCH, LOW);
  digitalWrite(PIN_HALL, LOW);
}

bool readButtonDebounced() {
  bool raw = digitalRead(PIN_BUTTON);
  if (raw != lastButtonState) {
    lastButtonChange = millis();
    lastButtonState = raw;
  }
  if ((millis() - lastButtonChange) >= DEBOUNCE_MS) {
    // stable state for debounce window
    // detect falling edge (HIGH -> LOW) as a press
    static bool reportedState = HIGH;
    if (lastButtonState != reportedState) {
      reportedState = lastButtonState;
      if (reportedState == LOW) return true; // press event
    }
  }
  return false;
}

void activatePorch() {
  porchEndTime = millis() + PORCH_MS;
  porchActive = true;
  digitalWrite(PIN_PORCH, HIGH);
}

void activateHall() {
  hallEndTime = millis() + HALL_MS;
  hallActive = true;
  digitalWrite(PIN_HALL, HIGH);
}

void updateTimers() {
  unsigned long now = millis();

  if (porchActive && (long)(now - porchEndTime) >= 0) {
    porchActive = false;
    digitalWrite(PIN_PORCH, LOW);
  }

  if (hallActive && (long)(now - hallEndTime) >= 0) {
    hallActive = false;
    digitalWrite(PIN_HALL, LOW);
  }
}

void setup() {
  setupPins();
}

void loop() {
  // check button (debounced)
  if (readButtonDebounced()) {
    // on each valid press we restart both timers
    activatePorch();
    activateHall();
  }

  // update LED timers (non-blocking)
  updateTimers();

  // keep loop responsive; no long delays
  delay(5);
}