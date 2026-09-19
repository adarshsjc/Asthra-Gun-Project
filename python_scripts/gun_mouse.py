import serial
import json
import pyautogui
import time

SERIAL_PORT = 'COM5'
BAUD_RATE = 115200

# --- TUNINGS INSPIRED BY YOUNES-MAKHCHAN REPO ---
DEADZONE = 0.05
SMOOTHING_ALPHA = 0.2   
MIN_SENSITIVITY = 10.0  
MAX_SENSITIVITY = 40.0  
ACCEL_THRESHOLD = 1.5   

pyautogui.PAUSE = 0 
smoothed_x = 0
smoothed_y = 0

def main():
    global smoothed_x, smoothed_y
    print(f"Connecting to {SERIAL_PORT}...")
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)
        print("Connected! Advanced Air Mouse is ACTIVE.")
    except Exception:
        print("Error connecting. Ensure Serial Monitor is CLOSED.")
        return

    last_shoot = 0

    while True:
        try:
            line = ser.readline().decode('utf-8').strip()
            if line.startswith("{") and line.endswith("}"):
                data = json.loads(line)
                
                # --- 1. AXIS MAPPING (SWAPPED TO FIX ROTATION) ---
                # GyroY now controls Left/Right (Yaw)
                # GyroZ now controls Up/Down (Pitch)
                raw_yaw = -data.get("GyroY", 0)  
                raw_pitch = data.get("GyroZ", 0) 
                
                shoot = data.get("Shoot", 0)

                # 2. Deadzone Filter
                if abs(raw_yaw) < DEADZONE: raw_yaw = 0
                if abs(raw_pitch) < DEADZONE: raw_pitch = 0

                # 3. Adaptive Sensitivity
                sens_x = MAX_SENSITIVITY if abs(raw_yaw) > ACCEL_THRESHOLD else MIN_SENSITIVITY
                sens_y = MAX_SENSITIVITY if abs(raw_pitch) > ACCEL_THRESHOLD else MIN_SENSITIVITY

                target_x = raw_yaw * sens_x
                target_y = raw_pitch * sens_y

                # 4. Smoothing (Exponential Moving Average)
                smoothed_x = (SMOOTHING_ALPHA * target_x) + ((1 - SMOOTHING_ALPHA) * smoothed_x)
                smoothed_y = (SMOOTHING_ALPHA * target_y) + ((1 - SMOOTHING_ALPHA) * smoothed_y)

                # Move Mouse
                if abs(smoothed_x) > 0.1 or abs(smoothed_y) > 0.1:
                    pyautogui.move(smoothed_x, smoothed_y, _pause=False)
                
                if shoot == 1 and last_shoot == 0:
                    pyautogui.click()
                last_shoot = shoot

        except KeyboardInterrupt:
            break
        except Exception:
            pass

if __name__ == "__main__":
    main()