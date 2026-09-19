"""
Interactive ESP32 Gun Test
===========================
Move the gun and press buttons - this will show you exactly what data comes in.
Helps diagnose which gyro axis corresponds to which movement.
"""
import serial
import serial.tools.list_ports
import time

# Find ESP32
ports = serial.tools.list_ports.comports()
port = None
for p in ports:
    desc = (p.description or "").lower()
    hwid = (p.hwid or "").lower()
    if any(kw in desc for kw in ["esp32", "cp210", "ch340", "usb serial", "jtag"]) or "303a" in hwid:
        port = p.device
        break
if not port:
    port = "COM5"

print(f"Connecting to {port}...")
ser = serial.Serial(port, 115200, timeout=2)
time.sleep(2)
print(f"Connected!\n")

import json

print("=" * 70)
print("  INTERACTIVE GUN TEST")
print("  Move the gun and press buttons to see the data change")
print("=" * 70)
print()
print("  Instructions:")
print("  1) Hold gun STILL for 3 sec (baseline)")
print("  2) Rotate gun LEFT/RIGHT slowly")
print("  3) Tilt gun UP/DOWN slowly")
print("  4) Press each button one by one")
print()
print("  Format: Y=yaw  P=pitch  GX/GY/GZ=gyro rates  S=shoot R=reload SC=scope")
print("  " + "-" * 66)

count = 0
while True:
    try:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if not line or not line.startswith("{"):
            continue
        
        d = json.loads(line)
        if "E" in d:
            print(f"  [ESP32] {d['E']}")
            continue
        
        # Highlight non-zero gyro values
        gx = d.get("GX", 0)
        gy = d.get("GY", 0)
        gz = d.get("GZ", 0)
        
        # Highlight buttons
        shoot = d.get("S", 0)
        reload = d.get("R", 0)
        scope = d.get("SC", 0)
        
        # Color coding: show which values are active
        gyro_parts = []
        gyro_parts.append(f"GX:{gx:+7.3f}" + ("*" if abs(gx) > 0.05 else " "))
        gyro_parts.append(f"GY:{gy:+7.3f}" + ("*" if abs(gy) > 0.05 else " "))
        gyro_parts.append(f"GZ:{gz:+7.3f}" + ("*" if abs(gz) > 0.05 else " "))
        
        btn_parts = []
        if shoot: btn_parts.append("SHOOT!")
        if reload: btn_parts.append("RELOAD!")
        if scope: btn_parts.append("SCOPE!")
        btn_str = " ".join(btn_parts) if btn_parts else ""
        
        jx = d.get("JX", 2048)
        jy = d.get("JY", 2048)
        joy_str = ""
        if abs(jx - 2048) > 200 or abs(jy - 2048) > 200:
            joy_str = f"JOY({jx},{jy})"
        
        count += 1
        if count % 5 == 0:  # Print every 5th reading to avoid spam
            print(f"  [{count:04d}] {' | '.join(gyro_parts)}  Y:{d.get('Y',0):+7.1f} P:{d.get('P',0):+7.1f}  {btn_str} {joy_str}")
    
    except json.JSONDecodeError:
        pass
    except KeyboardInterrupt:
        print("\n  Done!")
        break

ser.close()
