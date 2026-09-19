"""
Operation Ironhold - ESP32 Gun Controller Bridge Server
========================================================
Serves the game on HTTP (port 8080) and bridges ESP32 serial data
to the browser via WebSocket (port 8765).

Usage:
    pip install pyserial websockets
    python bridge_server.py

Then open http://localhost:8080 in your browser.
"""

import asyncio
import json
import os
import sys
import http.server
import threading
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)

try:
    import websockets
except ImportError:
    print("ERROR: websockets not installed. Run: pip install websockets")
    sys.exit(1)

# --- CONFIGURATION ---
SERIAL_BAUD   = 115200
HTTP_PORT     = 8080
WS_PORT       = 8765
FALLBACK_PORT = "COM5"

# Path to the game HTML file
GAME_DIR = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "operation-ironhold"
))

# --- SERIAL PORT DETECTION ---
def find_esp32_port():
    """Auto-detect ESP32-S3 serial port."""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = (port.description or "").lower()
        hwid = (port.hwid or "").lower()
        if any(kw in desc for kw in ["esp32", "cp210", "ch340", "usb serial", "usb-serial", "jtag"]):
            print(f"  [AUTO] Found ESP32 on {port.device}: {port.description}")
            return port.device
        if "303a" in hwid:
            print(f"  [AUTO] Found Espressif device on {port.device}: {port.description}")
            return port.device
    return None

# --- GLOBAL STATE ---
CLIENTS = set()
serial_connected = False
serial_port_name = "N/A"
event_loop = None

# --- HTTP SERVER ---
class GameHTTPHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=GAME_DIR, **kwargs)

    def log_message(self, format, *args):
        pass  # suppress HTTP logs

    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Cache-Control', 'no-cache')
        super().end_headers()

def start_http_server():
    server = http.server.HTTPServer(("0.0.0.0", HTTP_PORT), GameHTTPHandler)
    print(f"  [HTTP] Game served at http://localhost:{HTTP_PORT}")
    server.serve_forever()

# --- BROADCAST ---
async def broadcast(message):
    """Send message to all connected WebSocket clients."""
    to_remove = set()
    for ws in list(CLIENTS):
        try:
            await ws.send(message)
        except Exception:
            to_remove.add(ws)
    for ws in to_remove:
        CLIENTS.discard(ws)

# --- SERIAL READER ---
def serial_reader():
    """Read serial data from ESP32 in a background thread."""
    global serial_connected, serial_port_name

    port = find_esp32_port()
    if not port:
        print(f"  [SERIAL] No ESP32 detected. Trying fallback port {FALLBACK_PORT}...")
        port = FALLBACK_PORT

    serial_port_name = port

    while True:
        try:
            print(f"  [SERIAL] Connecting to {port}...")
            ser = serial.Serial(port, SERIAL_BAUD, timeout=1)
            time.sleep(2)
            serial_connected = True
            print(f"  [SERIAL] Connected to {port}")

            while True:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if not line:
                        continue

                    if line.startswith("{") and line.endswith("}"):
                        try:
                            data = json.loads(line)

                            if "E" in data:
                                print(f"  [ESP32] {data['E']}")
                                continue

                            # Broadcast to all WebSocket clients
                            if CLIENTS and event_loop:
                                msg = json.dumps(data)
                                asyncio.run_coroutine_threadsafe(broadcast(msg), event_loop)
                        except json.JSONDecodeError:
                            pass

                except serial.SerialException:
                    serial_connected = False
                    print(f"  [SERIAL] Connection lost. Reconnecting...")
                    ser.close()
                    break
                except Exception:
                    pass

        except serial.SerialException:
            serial_connected = False
            print(f"  [SERIAL] Cannot open {port}. Retrying in 3s...")
            time.sleep(3)
        except Exception as e:
            serial_connected = False
            print(f"  [SERIAL] Error: {e}. Retrying in 3s...")
            time.sleep(3)

# --- WEBSOCKET HANDLER ---
async def ws_handler(websocket):
    """Handle a WebSocket connection from the browser."""
    CLIENTS.add(websocket)
    print(f"  [WS] Browser connected ({len(CLIENTS)} active)")

    # Send connection status immediately
    try:
        await websocket.send(json.dumps({
            "type": "status",
            "serial": serial_connected,
            "port": serial_port_name
        }))
    except Exception:
        pass

    try:
        async for message in websocket:
            try:
                cmd = json.loads(message)
                if cmd.get("type") == "ping":
                    await websocket.send(json.dumps({
                        "type": "pong",
                        "serial": serial_connected,
                        "port": serial_port_name
                    }))
            except json.JSONDecodeError:
                pass
    except Exception:
        pass
    finally:
        CLIENTS.discard(websocket)
        print(f"  [WS] Browser disconnected ({len(CLIENTS)} active)")

async def periodic_status():
    """Send periodic status updates to connected clients."""
    while True:
        await asyncio.sleep(2)
        if CLIENTS:
            msg = json.dumps({
                "type": "status",
                "serial": serial_connected,
                "port": serial_port_name
            })
            await broadcast(msg)

async def main():
    """Start the WebSocket server and serial reader."""
    global event_loop

    print("=" * 52)
    print("   OPERATION IRONHOLD - Gun Controller Bridge")
    print("=" * 52)
    print()

    # Verify game directory exists
    game_html = os.path.join(GAME_DIR, "index.html")
    if not os.path.exists(game_html):
        print(f"  [ERROR] Game not found at: {game_html}")
        print(f"  [ERROR] Expected game directory: {GAME_DIR}")
        sys.exit(1)

    print(f"  [GAME] Found at: {GAME_DIR}")

    # Start HTTP server in background thread
    http_thread = threading.Thread(target=start_http_server, daemon=True)
    http_thread.start()

    # Get the event loop for the serial reader to use
    event_loop = asyncio.get_running_loop()

    # Start serial reader in background thread
    serial_thread = threading.Thread(target=serial_reader, daemon=True)
    serial_thread.start()

    # Start periodic status broadcaster
    asyncio.ensure_future(periodic_status())

    # Start WebSocket server
    print(f"  [WS] WebSocket server on ws://localhost:{WS_PORT}")
    print()
    print("  +----------------------------------------------+")
    print(f"  |  Open: http://localhost:{HTTP_PORT}               |")
    print("  |  Select 'GUN CONTROLLER' on the start screen |")
    print("  +----------------------------------------------+")
    print()

    async with websockets.serve(ws_handler, "0.0.0.0", WS_PORT):
        await asyncio.Future()  # Run forever

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n  [EXIT] Bridge server stopped.")
