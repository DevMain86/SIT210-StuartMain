import cv2
import time
import threading
import serial
import json
import csv
import os
import argparse
from datetime import datetime

# MQTT publisher is optional; the system still runs if it is unavailable.
try:
    from mqtt_publisher import MqttPublisher
    MQTT_AVAILABLE = True
except ImportError:
    MQTT_AVAILABLE = False

# CONFIG
# Camera and serial configuration and tuning constants.

CAM_INDEX = 0
FRAME_W = 640
FRAME_H = 480
FPS = 7

SERIAL_PORT = '/dev/ttyACM0'
SERIAL_BAUD = 115200
HEARTBEAT_INTERVAL_S = 1.0

TELEMETRY_LOG = 'telemetry.csv'
SHOW_WINDOW_DEFAULT = False

# MQTT
# Broker connection details. Mosquitto runs locally on the Pi and Node-RED
# subscribes to the published topics for email alerts and the dashboard.
MQTT_HOST = 'localhost'
MQTT_PORT = 1883

# Camera tuning
# MIN_CONTOUR_AREA: ignore blobs smaller (reduces false positives)
# MAX_CONTOUR_AREA: ignore extremely large blobs (sudden lighting changes)
MIN_CONTOUR_AREA = 3000
MAX_CONTOUR_AREA = 80000          
MOG_HISTORY = 500
MOG_VAR_THRESHOLD = 50
MOG_DETECT_SHADOWS = False
STABLE_FRAMES_REQUIRED = 5        # number of frames required to accept a new camera count

# Ultrasonic tuning 
# ULTRA_DETECT_CM: ignore readings beyond this distance
# PRESENCE_DELTA_CM: how much closer than baseline counts as presence
ULTRA_DETECT_CM = 120
PRESENCE_DELTA_CM = 25
ULTRA_REQUIRED = 2                # consecutive ultrasonic frames required to confirm presence
BASELINE_ALPHA = 0.15             # EMA smoothing factor for baseline updates
BASELINE_MIN_EMPTY_SEC = 3        # seconds camera must be empty before baseline updates
BASELINE_MIN_VALID_CM = 30        # ignore obviously invalid baseline values below this

# Temperature threshold (must match Arduino TEMP_THRESHOLD)
# The Pi will only request fan activation when Arduino reports temp >= this value.
TEMP_THRESHOLD = 15

# Energy conservation (presence-gated sensing)
# The camera pipeline is the heaviest CPU/power consumer, so it only runs when
# the pet is (or may be) present. ACTIVE = full camera pipeline; IDLE = camera
# skipped, only the cheap ultrasonic telemetry is polled until something approaches.
ENERGY_SAVING_ENABLED = True      # master switch for ACTIVE/IDLE sensing
IDLE_POLL_INTERVAL_S = 3.0        # seconds between ultrasonic checks while IDLE
ACTIVE_HOLD_S = 30.0             # stay ACTIVE this long after the last presence
IDLE_WAKE_DELTA_CM = 20           # ultrasonic drop below baseline that wakes the camera

# GLOBALS
# latest_telemetry stores the most recent telemetry object received from Arduino.
latest_telemetry = {}
telemetry_lock = threading.Lock()  # protect access to latest_telemetry across threads

# SERIAL 
# ArduinoSerial encapsulates the serial connection, a reader thread that parses
# newline-terminated JSON telemetry, and a heartbeat thread that periodically
# sends {"cmd":"heartbeat"} to the Arduino so the Arduino watchdog knows Pi is alive.
class ArduinoSerial:
    def __init__(self, port=SERIAL_PORT, baud=SERIAL_BAUD):
        self.port = port
        self.baud = baud
        self.ser = None
        self.lock = threading.Lock()
        self.running = False
        self.reader_thread = None
        self.heartbeat_thread = None

    def open(self):
        # Attempt to open the serial port and start background threads.
        # This allows reconnect attempts after a failure.
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=1)
            self.running = True
            # Start reader thread if not already running
            if self.reader_thread is None or not self.reader_thread.is_alive():
                self.reader_thread = threading.Thread(
                    target=self._reader_thread, daemon=True)
                self.reader_thread.start()
            # Start heartbeat thread if not already running
            if self.heartbeat_thread is None or not self.heartbeat_thread.is_alive():
                self.heartbeat_thread = threading.Thread(
                    target=self._heartbeat_thread, daemon=True)
                self.heartbeat_thread.start()
            print("Opened serial", self.port)
            return True
        except Exception as e:
            # Opening failed (device not present or permission issue)
            print("Failed to open serial:", e)
            self.ser = None
            self.running = False
            return False

    def close(self):
        # Stop threads and close serial port cleanly.
        self.running = False
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None

    def _reader_thread(self):
        # Background thread: read lines, parse JSON, and store latest telemetry.
        global latest_telemetry
        while self.running:
            # If serial disappears mid-run, exit thread so open() can restart it.
            if self.ser is None:
                break
            try:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                    # Telemetry may be nested under 'telemetry' key; normalize it.
                    t = obj.get('telemetry', obj)
                    t['__recv_ts'] = datetime.utcnow().isoformat()
                    with telemetry_lock:
                        latest_telemetry = t
                    # Print telemetry for debugging and visibility
                    print("[ARD]", t)
                except json.JSONDecodeError:
                    # If the line isn't JSON, print raw for debugging
                    print("[ARD RAW]", line)
            except Exception as e:
                # Serial read error (device disconnected, etc.)
                print("Serial read error:", e)
                # Clear ser so main loop can detect and attempt reconnect
                self.ser = None
                self.running = False
                break

    def _heartbeat_thread(self):
        # Periodically send heartbeat commands to Arduino so it knows Pi is alive.
        while self.running:
            self.send({"cmd": "heartbeat"})
            time.sleep(HEARTBEAT_INTERVAL_S)

    def send(self, obj):
        # Send a JSON command object to Arduino (newline-terminated).
        if not self.ser:
            return False
        try:
            s = json.dumps(obj) + '\n'
            with self.lock:
                self.ser.write(s.encode('utf-8'))
            return True
        except Exception as e:
            print("Serial write error:", e)
            return False

    def get_latest(self):
        # Return a shallow copy of the latest telemetry dict (thread-safe).
        with telemetry_lock:
            return dict(latest_telemetry) if latest_telemetry else None

# CAMERA HELPERS
# estimate_count uses MOG2 background subtraction and contour filtering to
# estimate the number of moving blobs (people/pets) in the frame.
def estimate_count(frame_gray, bg_sub):
    fg = bg_sub.apply(frame_gray)
    # Morphological cleanup reduces speckle and small noise blobs.
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))
    fg = cv2.morphologyEx(fg, cv2.MORPH_OPEN,  kernel, iterations=1)
    fg = cv2.morphologyEx(fg, cv2.MORPH_CLOSE, kernel, iterations=1)
    # Find contours in the foreground mask
    contours, _ = cv2.findContours(
        fg, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    count = 0
    boxes = []
    for c in contours:
        area = cv2.contourArea(c)
        # Filter by area to ignore small noise and huge irrelevant blobs
        if area < MIN_CONTOUR_AREA or area > MAX_CONTOUR_AREA:
            continue
        x, y, w, h = cv2.boundingRect(c)
        boxes.append((x, y, w, h, area))
        count += 1
    return count, fg, boxes

# LOGGING
# Append a telemetry row to TELEMETRY_LOG CSV for offline analysis and tuning.
def log_row(row):
    header = ['ts', 'camera_count', 'ultra_cm', 'ultra_baseline',
              'presence_score', 'fan_cmd', 'note']
    exists = os.path.exists(TELEMETRY_LOG)
    with open(TELEMETRY_LOG, 'a', newline='') as f:
        w = csv.writer(f)
        if not exists:
            w.writerow(header)
        w.writerow(row)

# FAN LOGIC
# Map a fused presence_score to discrete fan levels (0..3).
def presence_to_fan_level(score):
    if score >= 3:
        return 3
    if score == 2:
        return 2
    if score == 1:
        return 1
    return 0

# MAIN 
# The main loop captures frames, estimates camera counts, reads Arduino telemetry,
# maintains an ultrasonic baseline, fuses sensors, and sends set_fan commands.
# It also publishes telemetry/events to MQTT and gates the camera pipeline for
# energy conservation when no pet is present.
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--serial', default=SERIAL_PORT)
    parser.add_argument('--show', action='store_true',
                        default=SHOW_WINDOW_DEFAULT)
    parser.add_argument('--mqtt-host', default=MQTT_HOST)
    parser.add_argument('--no-mqtt', action='store_true')   # disable MQTT publishing
    args = parser.parse_args()

    # Create Arduino serial helper and attempt to open the port.
    ard = ArduinoSerial(args.serial)
    if not ard.open():
        print("Serial not available at startup; will retry in main loop.")

    # MQTT is optional: if the broker or library is unavailable, core control
    # still runs and only notifications/dashboard are lost (graceful degradation).
    mqtt_pub = None
    if not args.no_mqtt and MQTT_AVAILABLE:
        mqtt_pub = MqttPublisher(host=args.mqtt_host, port=MQTT_PORT)
        if mqtt_pub.connect():
            print("MQTT publisher started ->", args.mqtt_host)
        else:
            print("MQTT connect failed; continuing without notifications.")
            mqtt_pub = None
    elif not MQTT_AVAILABLE:
        print("paho-mqtt not installed; continuing without notifications.")

    # Open camera capture and set parameters.
    # Force the V4L2 backend: on Raspberry Pi OS the default GStreamer pipeline
    # often fails to open USB webcams ("Internal data stream error").
    cap = cv2.VideoCapture(CAM_INDEX, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  FRAME_W)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_H)
    cap.set(cv2.CAP_PROP_FPS, FPS)
    if not cap.isOpened():
        print("Cannot open camera")
        return

    # Create background subtractor (MOG2) for motion-based people/pet counting
    bg_sub = cv2.createBackgroundSubtractorMOG2(
        history=MOG_HISTORY,
        varThreshold=MOG_VAR_THRESHOLD,
        detectShadows=MOG_DETECT_SHADOWS)

    # Initialize local state for debouncing and presence tracking
    stable_count = -1   # start at -1 so first observed count is treated as a change
    stable_counter = 0
    last_presence_ts = 0

    # Ultrasonic baseline state on Pi
    ultra_baseline = None
    empty_start_ts = None
    ultra_counter = 0

    # Event edge-detection state: publish MQTT events only on transitions,
    # not every cycle, so the user is not flooded with duplicate emails.
    prev_temp_exceeded = False
    prev_fan_on = False

    # Energy-saving state: ACTIVE = full camera pipeline, IDLE = ultrasonic poll only.
    sensing_active = True            # start ACTIVE so the baseline can establish
    last_active_ts = time.time()
    prev_sensing_active = True

    print("Starting capture loop. Press Ctrl-C to stop.")
    try:
        while True:
            # Serial reconnect: if serial object is None, try to reopen
            if ard.ser is None:
                if ard.open():
                    print("Serial reconnected")
                else:
                    time.sleep(5.0)
                    continue

            t0 = time.time()

            # Energy-saving gate: while IDLE, skip the camera pipeline entirely
            # and just poll the Arduino's ultrasonic telemetry. Wake to ACTIVE
            # if something is detected approaching the bed.
            if ENERGY_SAVING_ENABLED and not sensing_active:
                tele_idle = ard.get_latest()
                d_idle = tele_idle.get('ultra_cm', -1) if tele_idle else -1
                b_idle = tele_idle.get('ultra_baseline', None) if tele_idle else None
                # Wake if a valid reading is meaningfully closer than the baseline
                woke = (b_idle is not None and float(b_idle) > BASELINE_MIN_VALID_CM
                        and isinstance(d_idle, (int, float)) and d_idle > 0
                        and d_idle < (float(b_idle) - IDLE_WAKE_DELTA_CM))
                if woke:
                    sensing_active = True
                    last_active_ts = time.time()
                    print("[ENERGY] Wake -> ACTIVE (ultrasonic detected approach)")
                else:
                    # Still idle: publish a light telemetry update for the dashboard,
                    # then sleep for the slow poll interval and skip the camera.
                    if mqtt_pub is not None and tele_idle is not None:
                        mqtt_pub.publish_telemetry({"ts": time.time(),
                                                    "temp": tele_idle.get('temp'),
                                                    "ultra_cm": d_idle,
                                                    "count": 0, "score": 0,
                                                    "fan_level": tele_idle.get('fan_level', 0),
                                                    "sensing": "idle"})
                    time.sleep(IDLE_POLL_INTERVAL_S)
                    continue

            ret, frame = cap.read()
            if not ret:
                # Camera read failed then wait briefly and retry
                time.sleep(0.1)
                continue

            # Convert to grayscale for background subtraction
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            count, fgmask, boxes = estimate_count(gray, bg_sub)

            # Debounce camera count
            # Accumulate counter while count stays the same then reset on change.
            if count == stable_count:
                stable_counter += 1
                if stable_counter >= STABLE_FRAMES_REQUIRED:
                    # When stable for required frames, notify Arduino of the stable count
                    stable_counter = 0
                    ok = ard.send({"cmd": "set_count", "count": int(stable_count)})
                    print("Camera stable count ->", stable_count,
                          "sent set_count ok?", ok)
            else:
                # New count seen — start accumulating from 1
                stable_count = count
                stable_counter = 1


            # Read latest telemetry from Arduino (thread-safe copy)
            tele = ard.get_latest()
            dist      = tele.get('ultra_cm',       -1)   if tele else -1
            ultra_b   = tele.get('ultra_baseline', None) if tele else None
            current_fan = tele.get('fan_level',    None) if tele else None
            temp      = tele.get('temp',           None) if tele else None

            now = time.time()

            # FIX: seed Pi-side baseline from Arduino when not yet set
            # If Arduino already has a baseline
            # use it to initialize the Pi-side baseline so detection works at startup.
            if ultra_baseline is None and ultra_b is not None and float(ultra_b) > 0:
                ultra_baseline = float(ultra_b)
                print("Ultra baseline seeded from Arduino:", ultra_baseline)

            # Pi-side ultrasonic baseline update when camera empty
            # Only update baseline when camera reports empty for BASELINE_MIN_EMPTY_SEC
            if stable_count == 0:
                if empty_start_ts is None:
                    empty_start_ts = now
                if (now - empty_start_ts) >= BASELINE_MIN_EMPTY_SEC:
                    if isinstance(dist, (int, float)) and 0 < dist < ULTRA_DETECT_CM:
                        if ultra_baseline is None:
                            ultra_baseline = float(dist)
                        else:
                            # EMA update: new_baseline = alpha * new + (1-alpha) * old
                            ultra_baseline = (BASELINE_ALPHA * float(dist)
                                              + (1.0 - BASELINE_ALPHA) * ultra_baseline)
            else:
                # Reset empty timer when camera is not empty
                empty_start_ts = None

            # Ultrasonic trigger: only if significantly closer than baseline
            if (ultra_baseline is not None
                    and isinstance(dist, (int, float))
                    and dist > 0
                    and dist < ULTRA_DETECT_CM):
                # Only trigger if baseline is valid and current reading is closer than baseline - delta
                if (ultra_baseline > BASELINE_MIN_VALID_CM
                        and dist < (ultra_baseline - PRESENCE_DELTA_CM)):
                    ultra_counter += 1
                else:
                    ultra_counter = 0
            else:
                ultra_counter = 0

            # If ultrasonic condition holds for ULTRA_REQUIRED consecutive frames, mark presence
            if ultra_counter >= ULTRA_REQUIRED:
                last_presence_ts = now

            # sensor_presence is true if ultrasonic triggered recently (within 30s)
            sensor_presence = 1 if (now - last_presence_ts) < 30 else 0

            # Fuse sensors: camera contributes stable_count (each blob counts as 1),
            # ultrasonic contributes +1 if it recently triggered.
            presence_score = stable_count
            if sensor_presence:
                presence_score += 1

            # Energy-saving: drop to IDLE after a sustained absence, and announce
            # ACTIVE/IDLE transitions over MQTT so the dashboard can show the state.
            if presence_score > 0:
                last_active_ts = now
            if ENERGY_SAVING_ENABLED and sensing_active and (now - last_active_ts) >= ACTIVE_HOLD_S:
                sensing_active = False
                print("[ENERGY] No presence; sleeping -> IDLE")
            if sensing_active != prev_sensing_active:
                if mqtt_pub is not None:
                    mqtt_pub.publish_event("sensing_active" if sensing_active else "sensing_idle", {})
                prev_sensing_active = sensing_active

            # Decision: only attempt to turn fan on if Arduino reports temp >= threshold
            temp_exceeded = temp is not None and temp >= TEMP_THRESHOLD
            desired_fan = 0
            if temp_exceeded and presence_score > 0:
                desired_fan = presence_to_fan_level(presence_score)

            # Send set_fan only when level changes
            # Distinguish a successful send, a no-change, and a send failure
            note = "no_change"
            if current_fan is None or int(current_fan) != desired_fan:
                send_ok = ard.send({"cmd": "set_fan", "level": int(desired_fan)})
                print("Sent set_fan", desired_fan, "ok?", send_ok)
                note = "sent_ok" if send_ok else "send_FAILED"

            # MQTT publishing: full telemetry every cycle (for the dashboard) and
            # discrete events only on rising/falling edges (for email alerts).
            if mqtt_pub is not None:
                mqtt_pub.publish_telemetry({"ts": now, "temp": temp, "ultra_cm": dist,
                                            "baseline": round(ultra_baseline, 1) if ultra_baseline else None,
                                            "count": stable_count, "score": presence_score,
                                            "fan_level": desired_fan})
                # Temperature crossed above threshold
                if temp_exceeded and not prev_temp_exceeded:
                    mqtt_pub.publish_event("temp_exceeded", {"temp": temp, "threshold": TEMP_THRESHOLD})
                # Fan turned on / off
                fan_on = desired_fan > 0
                if fan_on and not prev_fan_on:
                    mqtt_pub.publish_event("fan_on", {"level": desired_fan, "temp": temp})
                elif not fan_on and prev_fan_on:
                    mqtt_pub.publish_event("fan_off", {"temp": temp})
                # Remember this cycle's state for next iteration's edge detection
                prev_temp_exceeded = temp_exceeded
                prev_fan_on = fan_on

            # Log telemetry row for offline analysis and tuning
            row = [
                datetime.utcnow().isoformat(),
                stable_count,
                dist,
                round(ultra_baseline, 1) if ultra_baseline is not None else None,
                presence_score,
                desired_fan,
                note,
            ]
            log_row(row)

            # Debug display: draw bounding boxes and overlay text
            if args.show:
                vis = frame.copy()
                for (x, y, w, h, a) in boxes:
                    cv2.rectangle(vis, (x, y), (x + w, y + h), (0, 255, 0), 2)
                cv2.putText(
                    vis,
                    f"Count:{stable_count} Score:{presence_score} Temp:{temp}",
                    (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                cv2.imshow('fusion', vis)
                cv2.imshow('fg', fgmask)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break

            # Maintain approximate target FPS by sleeping the remainder of the frame interval
            elapsed = time.time() - t0
            target = 1.0 / FPS
            if elapsed < target:
                time.sleep(max(0, target - elapsed))

    except KeyboardInterrupt:
        print("Stopping...")

    finally:
        # Cleanup resources: release camera, close windows, close serial, stop MQTT
        cap.release()
        cv2.destroyAllWindows()
        ard.close()
        if mqtt_pub is not None:
            mqtt_pub.disconnect()

if __name__ == '__main__':
    main()
