import json
import math
import os
import sys
import time
from websocket import create_connection

# --- Configuration ---
SIGNALK_SERVER = "localhost"
SIGNALK_PORT = 3000
TOKEN_FILE = "sk_token.txt"
UPDATE_INTERVAL = 1.0  # Seconds between updates

WS_URL = f"ws://{SIGNALK_SERVER}:{SIGNALK_PORT}/signalk/v1/stream?subscribe=none"

def get_token():
    """Reads the authentication token saved from your access request."""
    if os.path.exists(TOKEN_FILE):
        with open(TOKEN_FILE, "r") as f:
            return f.read().strip()
    print(f"[Error] Token file '{TOKEN_FILE}' not found. Run your access request script first.", flush=True)
    sys.exit(1)

def generate_dummy_data(step: int) -> list:
    """
    Generates dynamic dummy values using standard Signal K SI units:
      - Speed: m/s
      - Angles: Radians
      - Frequency (RPM): Hz
      - Temperature: Kelvin
    """
    t = step * 0.1

    sog_ms = 4.85 + 2.85 * math.sin(t * 0.5)                   # Speed over ground (m/s)
    heading_rad = (t * 0.2) % (2 * math.pi)                   # Magnetic Heading (rad)
    awa_rad = math.radians(90 + 80 * math.sin(t * 0.3))       # Apparent Wind Angle (rad)
    aws_ms = 7.2 + 3.1 * math.cos(t * 0.4)                    # Apparent Wind Speed (m/s)
    rpm_hz = (2100 + 900 * math.sin(t * 0.2)) / 60.0          # Engine Revolutions (Hz)
    eng_temp_k = 273.15 + (82.5 + 7.5 * math.sin(t * 0.1))    # Engine Temp (Kelvin)
    alt_temp_k = 273.15 + (60.0 + 10.0 * math.cos(t * 0.15))   # Alternator Temp (Kelvin)

    return [
        {"path": "navigation.speedOverGround", "value": round(sog_ms, 3)},
        {"path": "navigation.headingMagnetic", "value": round(heading_rad, 4)},
        {"path": "environment.wind.angleApparent", "value": round(awa_rad, 4)},
        {"path": "environment.wind.speedApparent", "value": round(aws_ms, 3)},
        {"path": "propulsion.engine.revolutions", "value": round(rpm_hz, 3)},
        {"path": "propulsion.engine.temperature", "value": round(eng_temp_k, 2)},
        {"path": "electrical.alternators.alternator.temperature", "value": round(alt_temp_k, 2)},
    ]

def build_delta_payload(values: list) -> dict:
    """Formats values into a Signal K WebSocket Delta format."""
    return {
        "context": "vessels.self",
        "updates": [
            {
                "source": {
                    "label": "python-mfd-simulator",
                    "type": "simulator"
                },
                "values": values
            }
        ]
    }

def main():
    token = get_token()
    headers = [f"Authorization: Bearer {token}"]

    print(f"Connecting to Signal K WebSocket at {WS_URL}...", flush=True)
    try:
        ws = create_connection(WS_URL, header=headers)
        print("WebSocket connected successfully! Streaming dummy data...\n", flush=True)
    except Exception as e:
        print(f"[Connection Error] Failed to connect: {e}", flush=True)
        sys.exit(1)

    step = 0
    try:
        while True:
            updates = generate_dummy_data(step)
            delta_msg = build_delta_payload(updates)
            
            ws.send(json.dumps(delta_msg))

            print(f"[Step {step:04d}] Pushed updates to Signal K:", flush=True)
            for item in updates:
                print(f"  └─ {item['path']}: {item['value']}", flush=True)
            print("-" * 45, flush=True)

            step += 1
            time.sleep(UPDATE_INTERVAL)

    except KeyboardInterrupt:
        print("\nSimulation stopped by user.", flush=True)
    finally:
        if 'ws' in locals():
            ws.close()

if __name__ == "__main__":
    main()

    
