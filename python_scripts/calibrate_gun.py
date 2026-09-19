"""
ESP32 Gun Calibration Tool
===========================
Guides you through moving the gun to calibration positions.
Captures real data to find the correct axis mapping and scale.

Run this BEFORE playing, then it saves a calibration config.
"""
import serial
import serial.tools.list_ports
import json, time, math, sys

# --- Find Port ---
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
try:
    ser = serial.Serial(port, 115200, timeout=2)
except Exception as e:
    print(f"ERROR: {e}")
    sys.exit(1)

time.sleep(2)

def read_sample(n=10):
    """Read n samples and return the average."""
    samples = []
    while len(samples) < n:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line.startswith("{") and line.endswith("}"):
            try:
                d = json.loads(line)
                if "Y" in d and "P" in d:
                    samples.append(d)
            except:
                pass
    Y  = sum(s["Y"]  for s in samples) / n
    P  = sum(s["P"]  for s in samples) / n
    GX = sum(s["GX"] for s in samples) / n
    GY = sum(s["GY"] for s in samples) / n
    GZ = sum(s["GZ"] for s in samples) / n
    JX = sum(s["JX"] for s in samples) / n
    JY = sum(s["JY"] for s in samples) / n
    return {"Y": Y, "P": P, "GX": GX, "GY": GY, "GZ": GZ, "JX": JX, "JY": JY}

def wait_for_enter(msg):
    input(f"\n  >> {msg}\n     Press ENTER when ready...")

def capture(label, n=20):
    print(f"  Capturing '{label}'...", end="", flush=True)
    s = read_sample(n)
    print(f" Y={s['Y']:.2f} P={s['P']:.2f}")
    return s

print()
print("=" * 60)
print("  ESP32 GUN CALIBRATION")
print("=" * 60)
print()
print("  This tool will ask you to point the gun at different")
print("  positions on screen. Hold STILL at each position.")
print()

# ── STEP 1: CENTER ──────────────────────────────────────────
wait_for_enter("Point the gun at the CENTER of your screen. Hold STILL.")
center = capture("CENTER")

# ── STEP 2: LEFT ────────────────────────────────────────────
wait_for_enter("Rotate gun to point at the LEFT EDGE of the screen. Hold STILL.")
left = capture("LEFT")

# ── STEP 3: RIGHT ───────────────────────────────────────────
wait_for_enter("Rotate gun to point at the RIGHT EDGE of the screen. Hold STILL.")
right = capture("RIGHT")

# ── STEP 4: UP ──────────────────────────────────────────────
wait_for_enter("Tilt gun to point at the TOP EDGE of the screen. Hold STILL.")
top = capture("TOP")

# ── STEP 5: DOWN ────────────────────────────────────────────
wait_for_enter("Tilt gun to point at the BOTTOM EDGE of the screen. Hold STILL.")
bottom = capture("BOTTOM")

ser.close()

# ── ANALYSIS ────────────────────────────────────────────────
print()
print("=" * 60)
print("  ANALYSIS")
print("=" * 60)

# Yaw axis: which angle changes most between left and right?
lr_dY  = (right["Y"] - left["Y"])
lr_dP  = (right["P"] - left["P"])

# Pitch axis: which angle changes most between top and bottom?
tb_dY  = (bottom["Y"] - top["Y"])
tb_dP  = (bottom["P"] - top["P"])

print()
print(f"  Left→Right movement:  dY={lr_dY:+.1f}°  dP={lr_dP:+.1f}°")
print(f"  Top→Bottom movement:  dY={tb_dY:+.1f}°  dP={tb_dP:+.1f}°")
print()

# Determine which sensor axis maps to which screen axis
if abs(lr_dY) >= abs(lr_dP):
    yaw_source   = "Y"
    yaw_sign     = -1 if lr_dY > 0 else 1   # gun right should = look right
else:
    yaw_source   = "P"
    yaw_sign     = -1 if lr_dP > 0 else 1

if abs(tb_dP) >= abs(tb_dY):
    pitch_source = "P"
    pitch_sign   =  1 if tb_dP > 0 else -1  # gun down should = look down
else:
    pitch_source = "Y"
    pitch_sign   =  1 if tb_dY > 0 else -1

# Compute deg-per-screen-width/height  (approximate - assumes ~90 deg FOV)
# Screen is ~90 deg wide, so we want to map that range to mouse pixels
# Tune sensitivity: pixels per degree of angle
lr_range = max(abs(lr_dY), abs(lr_dP))
tb_range = max(abs(tb_dP), abs(tb_dY))

# Use screen resolution estimate (or just let user tune after)
SCREEN_W = 1920
SCREEN_H = 1080
sens_h = (SCREEN_W / 2) / lr_range if lr_range > 1 else 10.0
sens_v = (SCREEN_H / 2) / tb_range if tb_range > 1 else 7.5
# Cap to reasonable values
sens_h = max(3.0, min(30.0, sens_h))
sens_v = max(3.0, min(30.0, sens_v))

center_Y = center["Y"]
center_P = center["P"]
joy_cx   = center["JX"]
joy_cy   = center["JY"]

print(f"  Yaw  axis  → '{yaw_source}' (sign: {'normal' if yaw_sign>0 else 'inverted'})")
print(f"  Pitch axis → '{pitch_source}' (sign: {'normal' if pitch_sign>0 else 'inverted'})")
print(f"  H sensitivity: {sens_h:.2f} px/deg")
print(f"  V sensitivity: {sens_v:.2f} px/deg")
print(f"  Center: Y={center_Y:.2f} P={center_P:.2f}")
print(f"  Joystick center: JX={joy_cx:.0f} JY={joy_cy:.0f}")
print()

# ── SAVE CONFIG ─────────────────────────────────────────────
config = {
    "yaw_source":   yaw_source,
    "yaw_sign":     yaw_sign,
    "pitch_source": pitch_source,
    "pitch_sign":   pitch_sign,
    "sens_slow":    round(sens_h * 0.6, 2),
    "sens_fast":    round(sens_h * 1.2, 2),
    "sens_v":       round(sens_v * 0.8, 2),
    "angle_deadzone": 0.08,
    "accel_thresh": 2.0,
    "smoothing":    0.45,
    "center_Y":     round(center_Y, 2),
    "center_P":     round(center_P, 2),
    "joy_center_X": round(joy_cx, 0),
    "joy_center_Y": round(joy_cy, 0),
    "lr_range_deg": round(lr_range, 2),
    "tb_range_deg": round(tb_range, 2)
}

config_path = "python_scripts/gun_calibration.json"
with open(config_path, "w") as f:
    json.dump(config, f, indent=2)

print(f"  Saved to {config_path}")
print()

# ── PRINT JS SNIPPET ────────────────────────────────────────
yaw_field   = "d.Y" if yaw_source == "Y" else "d.P"
pitch_field = "d.P" if pitch_source == "P" else "d.Y"

print("=" * 60)
print("  COPY THIS INTO index.html (replace the GUN config block):")
print("=" * 60)
print(f"""
  // ── CALIBRATED VALUES ──────────────────────────────────────
  ANGLE_DEADZONE:   {config['angle_deadzone']},
  SMOOTHING:        {config['smoothing']},
  SENS_SLOW:        {config['sens_slow']},
  SENS_FAST:        {config['sens_fast']},
  ACCEL_THRESH_DEG: {config['accel_thresh']},
  JOY_DEADZONE:     0.15,
  CENTER_THRESH:    3.0,
  RECENTER_HOLD:    3.0,
  // Axis config from calibration
  YAW_FIELD:   '{yaw_source}',   // use d.{yaw_source} for left/right
  YAW_SIGN:    {yaw_sign},        // {'+1 normal' if yaw_sign>0 else '-1 inverted'}
  PITCH_FIELD: '{pitch_source}', // use d.{pitch_source} for up/down
  PITCH_SIGN:  {pitch_sign},      // {'+1 normal' if pitch_sign>0 else '-1 inverted'}
  REF_Y:       {center_Y:.2f},    // calibrated center yaw
  REF_P:       {center_P:.2f},    // calibrated center pitch
""")

print("  Also replace the aiming delta lines with:")
print(f"""
    const rawYaw   = {yaw_sign} * (d.{yaw_source} - GUN.lastY_src);
    const rawPitch = {pitch_sign} * (d.{pitch_source} - GUN.lastP_src);
""")
print("  Done! Run the bridge_server.py to play.")
