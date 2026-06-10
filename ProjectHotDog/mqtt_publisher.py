import json
import time
import threading
 
import paho.mqtt.client as mqtt
 
 
# MqttPublisher manages a resilient broker connection and provides simple
# publish helpers. Resilience features relevant to fault tolerance:
#  - automatic background reconnection via loop_start()
#  - Last Will (LWT): if the Pi dies, broker publishes 'offline' on hotdog/status
#  - publishing never blocks or crashes the control loop (errors are caught)
class MqttPublisher:
    def __init__(self, host="localhost", port=1883,
                 base_topic="hotdog", client_id="hotdog-pi"):
        self.host = host
        self.port = port
        self.base_topic = base_topic
        self.connected = False
 
        # paho-mqtt 2.x requires an explicit callback API version
        self.client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=client_id,
            clean_session=True)
 
        # connection state callbacks
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
 
        # Last Will: broker publishes this if the Pi disconnects ungracefully.
        # retain=True so any later subscriber immediately sees the last status.
        self.status_topic = f"{base_topic}/status"
        self.client.will_set(self.status_topic,
                             payload=json.dumps({"status": "offline"}),
                             qos=1, retain=True)
 
    # CONNECTION LIFECYCLE
    def connect(self):
        # Start connecting and run the network loop in the background.
        # Returns True if the attempt was dispatched (connection confirmed in _on_connect).
        try:
            self.client.connect(self.host, self.port, keepalive=30)
            self.client.loop_start()   # background network thread
            return True
        except Exception as e:
            print("[MQTT] Initial connect failed:", e)
            return False
 
    def disconnect(self):
        # Publish an explicit 'offline' status, then disconnect cleanly.
        try:
            self.client.publish(self.status_topic,
                               json.dumps({"status": "offline"}),
                               qos=1, retain=True)
            self.client.loop_stop()
            self.client.disconnect()
        except Exception as e:
            print("[MQTT] Disconnect error:", e)
 
    def _on_connect(self, client, userdata, flags, reason_code, properties):
        # Fired when the broker accepts the connection
        if reason_code == 0:
            self.connected = True
            print("[MQTT] Connected to broker")
            # announce monitoring node online (retained)
            client.publish(self.status_topic,
                           json.dumps({"status": "online"}),
                           qos=1, retain=True)
        else:
            self.connected = False
            print("[MQTT] Connect failed, reason code:", reason_code)
 
    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        # Fired on disconnect; paho auto-reconnects via loop_start
        self.connected = False
        print("[MQTT] Disconnected (reason code:", reason_code, ") - will retry")
 
    # PUBLISHING HELPERS
    def publish_telemetry(self, telemetry: dict):
        # Full telemetry to hotdog/telemetry for the dashboard.
        # QoS 0 (fire and forget): telemetry is high-frequency, a dropped sample is harmless.
        return self._safe_publish(f"{self.base_topic}/telemetry", telemetry, qos=0, retain=False)
 
    def publish_event(self, event_type: str, detail: dict = None):
        # Discrete event to hotdog/events; triggers email in Node-RED.
        # QoS 1 (at least once) so important events are not lost.
        # event_type examples: 'temp_exceeded', 'fan_on', 'fan_off'
        payload = {"event": event_type, "ts": time.time()}
        if detail:
            payload.update(detail)
        return self._safe_publish(f"{self.base_topic}/events", payload, qos=1, retain=False)
 
    def _safe_publish(self, topic, payload_dict, qos, retain):
        # Serialise to JSON and publish, swallowing errors so a broker problem
        # can never crash the main control loop. Returns True on success.
        try:
            msg = json.dumps(payload_dict)
            result = self.client.publish(topic, msg, qos=qos, retain=retain)
            return result.rc == mqtt.MQTT_ERR_SUCCESS
        except Exception as e:
            print("[MQTT] Publish error on", topic, ":", e)
            return False
 
 
# STANDALONE SELF-TEST
# Run `python3 mqtt_publisher.py` with a broker on localhost to verify publishing.
# Subscribe in another terminal with: mosquitto_sub -t 'hotdog/#' -v
if __name__ == '__main__':
    pub = MqttPublisher()
    pub.connect()
    time.sleep(1)  # allow async connection to establish
    print("Publishing test telemetry and events...")
    pub.publish_telemetry({"temp": 30.1, "ultra_cm": 40, "count": 1, "fan_level": 2})
    pub.publish_event("temp_exceeded", {"temp": 30.1, "threshold": 28})
    pub.publish_event("fan_on", {"level": 2})
    time.sleep(1)
    pub.disconnect()
    print("Done.")