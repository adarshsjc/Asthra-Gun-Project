# Asthra Gun Project

Asthra is a motion-controlled gaming gun built with an ESP32-S3 and an MPU6050 gyro/accelerometer module. It allows you to play FPS games directly in your browser using physical movements to aim, and a joystick and buttons for movement and actions.

## Hardware Components
- **Microcontroller**: ESP32-S3
- **Motion Sensor**: MPU6050 (Gyroscope + Accelerometer)
- **Movement**: Analog Joystick
- **Actions**: 3x Push Buttons (Shoot, Reload, Pause)

## The Game: Operation Ironhold
This repository includes a modified version of **Operation Ironhold**, an open-source browser-based FPS game, adapted specifically to work with the Asthra Gun controller. 

**Original Game Creator**: [StarKnightt](https://github.com/StarKnightt)
**Original Game Repository**: [operation-ironhold](https://github.com/StarKnightt/operation-ironhold)

A huge thanks to the creator of Operation Ironhold for making the game open-source and providing an amazing foundation for this hardware integration!

## How it Works
1. The ESP32-S3 reads the MPU6050 sensor data (pitch/yaw) and the state of the joystick and buttons.
2. The ESP32 sends this data over a Serial connection to a Python Bridge Server.
3. The Python Bridge Server (`bridge_server.py`) opens a WebSocket server and relays the hardware data to the browser game.
4. The modified `index.html` of Operation Ironhold connects to the WebSocket, processes the real-time sensor data, and translates it into in-game aiming (mouse movement), walking (analog WASD), and shooting actions.

## Setup Instructions
1. Flash the ESP32 code located in `src/main.cpp` using PlatformIO.
2. Install the required Python dependencies: `pip install pyserial websockets`
3. Run the Python bridge server: `python python_scripts/bridge_server.py`
4. Open the `operation-ironhold/index.html` file in your browser via a local server (e.g., `http://localhost:8080`).
5. Select "GUN CONTROLLER" on the start screen.

*(Wiring diagrams and pictures coming soon!)*
