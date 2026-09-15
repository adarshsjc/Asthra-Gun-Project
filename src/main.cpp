#include <Arduino.h>
#include <Wire.h>
#include "USB.h"
#include "USBHIDMouse.h"
#include "USBHIDKeyboard.h"

// ============================================================
// ASTHRA GUN CONTROLLER
// ESP32-S3 + MPU-6250 + KY-023 + 3 Buttons
//
// USB HID version
//
// Mapping:
//   Joystick X       -> A / D
//   Joystick Y       -> W / S
//   Joystick SW      -> Cycle weapons 1 -> 2 -> 3 -> 4
//   GPIO 15 button   -> Left Mouse / Fire
//   GPIO 16 button   -> Right Mouse / ADS
//   GPIO 17 button   -> R / Reload
//   MPU gyro         -> Mouse Look
// ============================================================


// ============================================================
// USB HID
// ============================================================

USBHIDMouse Mouse;
USBHIDKeyboard Keyboard;


// ============================================================
// PIN CONFIGURATION
// ============================================================

// MPU-6250
#define SDA_PIN 8
#define SCL_PIN 9

// KY-023 joystick
#define JOY_X_PIN 1
#define JOY_Y_PIN 2
#define JOY_SW_PIN 10

// Tactile buttons
#define BUTTON_FIRE_PIN   15
#define BUTTON_ADS_PIN    16
#define BUTTON_RELOAD_PIN 17


// ============================================================
// MPU-6250 REGISTERS
// ============================================================

#define MPU_ADDR        0x68

#define WHO_AM_I        0x75
#define PWR_MGMT_1      0x6B
#define CONFIG_REG      0x1A
#define GYRO_CONFIG     0x1B
#define ACCEL_CONFIG    0x1C

#define GYRO_XOUT_H     0x43


// ============================================================
// JOYSTICK SETTINGS
// ============================================================

int joyCenterX = 2048;
int joyCenterY = 2048;

const float JOY_DEADZONE = 0.22;


// ============================================================
// GYRO SETTINGS
// ============================================================

// ±500 degrees/sec
const float GYRO_SENSITIVITY = 65.5;

// Mouse sensitivity
const float MOUSE_X_GAIN = 0.55;
const float MOUSE_Y_GAIN = 0.55;

// Change these if movement direction is reversed.
const bool INVERT_MOUSE_X = false;
const bool INVERT_MOUSE_Y = true;


// ============================================================
// GYRO CALIBRATION
// ============================================================

float gyroOffsetX = 0;
float gyroOffsetY = 0;
float gyroOffsetZ = 0;


// ============================================================
// BUTTON STATE
// ============================================================

bool previousJoySW = HIGH;

bool previousFire = HIGH;
bool previousADS = HIGH;
bool previousReload = HIGH;


// Current weapon
int currentWeapon = 1;


// ============================================================
// TIMING
// ============================================================

unsigned long lastControlTime = 0;
unsigned long lastDebugTime = 0;

const unsigned long CONTROL_INTERVAL = 5;


// ============================================================
// I2C HELPERS
// ============================================================

void writeMPURegister(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}


uint8_t readMPURegister(uint8_t reg)
{
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg);

    if (Wire.endTransmission(false) != 0)
        return 0;

    Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);

    if (Wire.available())
        return Wire.read();

    return 0;
}


bool readMPURegisters(uint8_t reg, uint8_t *buffer, uint8_t length)
{
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg);

    if (Wire.endTransmission(false) != 0)
        return false;

    uint8_t received = Wire.requestFrom(
        (uint8_t)MPU_ADDR,
        length,
        true
    );

    if (received != length)
        return false;

    for (uint8_t i = 0; i < length; i++)
    {
        buffer[i] = Wire.read();
    }

    return true;
}


// ============================================================
// INITIALIZE MPU
// ============================================================

bool initializeMPU()
{
    delay(100);

    uint8_t whoAmI = readMPURegister(WHO_AM_I);

    Serial.print("MPU WHO_AM_I = 0x");
    Serial.println(whoAmI, HEX);

    if (whoAmI == 0x00 || whoAmI == 0xFF)
    {
        Serial.println("ERROR: MPU not detected!");
        return false;
    }

    // Wake MPU
    writeMPURegister(PWR_MGMT_1, 0x00);
    delay(100);

    // Digital Low Pass Filter
    writeMPURegister(CONFIG_REG, 0x03);

    // Gyroscope ±500 dps
    writeMPURegister(GYRO_CONFIG, 0x08);

    // Accelerometer ±8g
    writeMPURegister(ACCEL_CONFIG, 0x10);

    delay(100);

    Serial.println("MPU initialized.");

    return true;
}


// ============================================================
// READ RAW GYRO
// ============================================================

bool readGyro(float &gx, float &gy, float &gz)
{
    uint8_t data[6];

    if (!readMPURegisters(GYRO_XOUT_H, data, 6))
    {
        return false;
    }

    int16_t rawX =
        ((int16_t)data[0] << 8) |
        data[1];

    int16_t rawY =
        ((int16_t)data[2] << 8) |
        data[3];

    int16_t rawZ =
        ((int16_t)data[4] << 8) |
        data[5];

    gx = ((float)rawX / GYRO_SENSITIVITY) - gyroOffsetX;
    gy = ((float)rawY / GYRO_SENSITIVITY) - gyroOffsetY;
    gz = ((float)rawZ / GYRO_SENSITIVITY) - gyroOffsetZ;

    return true;
}


// ============================================================
// GYRO CALIBRATION
// ============================================================

void calibrateGyro()
{
    Serial.println();
    Serial.println("=================================");
    Serial.println("GYRO CALIBRATION");
    Serial.println("Keep the controller completely still.");
    Serial.println("=================================");

    delay(1000);

    const int samples = 500;

    float sumX = 0;
    float sumY = 0;
    float sumZ = 0;

    int validSamples = 0;

    for (int i = 0; i < samples; i++)
    {
        uint8_t data[6];

        if (readMPURegisters(GYRO_XOUT_H, data, 6))
        {
            int16_t rawX =
                ((int16_t)data[0] << 8) |
                data[1];

            int16_t rawY =
                ((int16_t)data[2] << 8) |
                data[3];

            int16_t rawZ =
                ((int16_t)data[4] << 8) |
                data[5];

            sumX += (float)rawX / GYRO_SENSITIVITY;
            sumY += (float)rawY / GYRO_SENSITIVITY;
            sumZ += (float)rawZ / GYRO_SENSITIVITY;

            validSamples++;
        }

        delay(2);
    }

    if (validSamples > 0)
    {
        gyroOffsetX = sumX / validSamples;
        gyroOffsetY = sumY / validSamples;
        gyroOffsetZ = sumZ / validSamples;
    }

    Serial.println("Gyro calibration complete.");

    Serial.print("Offset X: ");
    Serial.println(gyroOffsetX);

    Serial.print("Offset Y: ");
    Serial.println(gyroOffsetY);

    Serial.print("Offset Z: ");
    Serial.println(gyroOffsetZ);

    Serial.println();
}


// ============================================================
// JOYSTICK CALIBRATION
// ============================================================

void calibrateJoystick()
{
    Serial.println("Joystick calibration...");
    Serial.println("Release the joystick.");

    delay(1000);

    long sumX = 0;
    long sumY = 0;

    const int samples = 100;

    for (int i = 0; i < samples; i++)
    {
        sumX += analogRead(JOY_X_PIN);
        sumY += analogRead(JOY_Y_PIN);

        delay(5);
    }

    joyCenterX = sumX / samples;
    joyCenterY = sumY / samples;

    Serial.print("Joystick center X: ");
    Serial.println(joyCenterX);

    Serial.print("Joystick center Y: ");
    Serial.println(joyCenterY);

    Serial.println("Joystick calibration complete.");
}


// ============================================================
// RELEASE ALL HID INPUT
// ============================================================

void releaseAllInputs()
{
    Keyboard.release('w');
    Keyboard.release('a');
    Keyboard.release('s');
    Keyboard.release('d');
    Keyboard.release('r');

    Mouse.release(MOUSE_LEFT);
    Mouse.release(MOUSE_RIGHT);
}


// ============================================================
// JOYSTICK MOVEMENT
// ============================================================

void handleJoystick()
{
    int x = analogRead(JOY_X_PIN);
    int y = analogRead(JOY_Y_PIN);

    float normalizedX =
        ((float)x - joyCenterX) / 2048.0;

    float normalizedY =
        ((float)y - joyCenterY) / 2048.0;


    // --------------------------
    // X AXIS
    // --------------------------

    if (normalizedX > JOY_DEADZONE)
    {
        Keyboard.press('d');
        Keyboard.release('a');
    }
    else if (normalizedX < -JOY_DEADZONE)
    {
        Keyboard.press('a');
        Keyboard.release('d');
    }
    else
    {
        Keyboard.release('a');
        Keyboard.release('d');
    }


    // --------------------------
    // Y AXIS
    // --------------------------

    // Push forward -> W
    // Push backward -> S

    if (normalizedY > JOY_DEADZONE)
    {
        Keyboard.press('s');
        Keyboard.release('w');
    }
    else if (normalizedY < -JOY_DEADZONE)
    {
        Keyboard.press('w');
        Keyboard.release('s');
    }
    else
    {
        Keyboard.release('w');
        Keyboard.release('s');
    }
}


// ============================================================
// JOYSTICK BUTTON = WEAPON CYCLE
// ============================================================

void handleWeaponSwitch()
{
    bool current = digitalRead(JOY_SW_PIN);

    // Button is active LOW
    if (previousJoySW == HIGH && current == LOW)
    {
        currentWeapon++;

        if (currentWeapon > 4)
        {
            currentWeapon = 1;
        }

        Keyboard.press('0' + currentWeapon);

        delay(30);

        Keyboard.release('0' + currentWeapon);

        Serial.print("Weapon switched to: ");
        Serial.println(currentWeapon);
    }

    previousJoySW = current;
}


// ============================================================
// FIRE BUTTON
// GPIO 15
// ============================================================

void handleFire()
{
    bool current = digitalRead(BUTTON_FIRE_PIN);

    if (current == LOW)
    {
        if (previousFire == HIGH)
        {
            Serial.println("FIRE ON");
        }

        Mouse.press(MOUSE_LEFT);
    }
    else
    {
        if (previousFire == LOW)
        {
            Serial.println("FIRE OFF");
        }

        Mouse.release(MOUSE_LEFT);
    }

    previousFire = current;
}


// ============================================================
// ADS BUTTON
// GPIO 16
// ============================================================

void handleADS()
{
    bool current = digitalRead(BUTTON_ADS_PIN);

    if (current == LOW)
    {
        if (previousADS == HIGH)
        {
            Serial.println("ADS ON");
        }

        Mouse.press(MOUSE_RIGHT);
    }
    else
    {
        if (previousADS == LOW)
        {
            Serial.println("ADS OFF");
        }

        Mouse.release(MOUSE_RIGHT);
    }

    previousADS = current;
}


// ============================================================
// RELOAD BUTTON
// GPIO 17
// ============================================================

void handleReload()
{
    bool current = digitalRead(BUTTON_RELOAD_PIN);

    // Press R once when button is pressed
    if (previousReload == HIGH && current == LOW)
    {
        Serial.println("RELOAD");

        Keyboard.press('r');

        delay(30);

        Keyboard.release('r');
    }

    previousReload = current;
}


// ============================================================
// MPU -> MOUSE LOOK
// ============================================================

void handleGyro()
{
    float gx;
    float gy;
    float gz;

    if (!readGyro(gx, gy, gz))
    {
        return;
    }


    // ----------------------------------------
    // Axis mapping
    //
    // Z gyro -> horizontal mouse movement
    // X gyro -> vertical mouse movement
    // ----------------------------------------

    float mouseX = gz * MOUSE_X_GAIN;
    float mouseY = gx * MOUSE_Y_GAIN;


    if (INVERT_MOUSE_X)
    {
        mouseX = -mouseX;
    }

    if (INVERT_MOUSE_Y)
    {
        mouseY = -mouseY;
    }


    // Ignore tiny gyro noise
    if (fabs(mouseX) < 0.4)
    {
        mouseX = 0;
    }

    if (fabs(mouseY) < 0.4)
    {
        mouseY = 0;
    }


    // Clamp to HID movement range
    mouseX = constrain(mouseX, -127, 127);
    mouseY = constrain(mouseY, -127, 127);


    if (mouseX != 0 || mouseY != 0)
    {
        Mouse.move(
            (int8_t)mouseX,
            (int8_t)mouseY,
            0
        );
    }
}


// ============================================================
// DEBUG OUTPUT
// ============================================================

void printDebug()
{
    int joyX = analogRead(JOY_X_PIN);
    int joyY = analogRead(JOY_Y_PIN);

    Serial.print("Joy X: ");
    Serial.print(joyX);

    Serial.print(" | Joy Y: ");
    Serial.print(joyY);

    Serial.print(" | SW: ");
    Serial.print(digitalRead(JOY_SW_PIN));

    Serial.print(" | Fire: ");
    Serial.print(digitalRead(BUTTON_FIRE_PIN));

    Serial.print(" | ADS: ");
    Serial.print(digitalRead(BUTTON_ADS_PIN));

    Serial.print(" | Reload: ");
    Serial.print(digitalRead(BUTTON_RELOAD_PIN));

    Serial.print(" | Weapon: ");
    Serial.println(currentWeapon);
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("==========================================");
    Serial.println("       ASTHRA GUN CONTROLLER");
    Serial.println("       USB HID INITIALIZATION");
    Serial.println("==========================================");


    // ----------------------------------------
    // GPIO
    // ----------------------------------------

    pinMode(JOY_SW_PIN, INPUT_PULLUP);

    pinMode(BUTTON_FIRE_PIN, INPUT_PULLUP);
    pinMode(BUTTON_ADS_PIN, INPUT_PULLUP);
    pinMode(BUTTON_RELOAD_PIN, INPUT_PULLUP);


    // ----------------------------------------
    // Analog joystick
    // ----------------------------------------

    analogReadResolution(12);


    // ----------------------------------------
    // I2C
    // ----------------------------------------

    Wire.begin(SDA_PIN, SCL_PIN);

    Wire.setClock(400000);


    // ----------------------------------------
    // MPU
    // ----------------------------------------

    if (!initializeMPU())
    {
        Serial.println("WARNING: MPU initialization failed.");
        Serial.println("Mouse look will not work.");
    }


    // ----------------------------------------
    // Calibration
    // ----------------------------------------

    calibrateJoystick();

    calibrateGyro();


    // ----------------------------------------
    // USB HID
    // ----------------------------------------

    USB.begin();

    Mouse.begin();
    Keyboard.begin();

    delay(1000);


    // ----------------------------------------
    // Safety
    // ----------------------------------------

    releaseAllInputs();


    Serial.println();
    Serial.println("==========================================");
    Serial.println("USB HID READY");
    Serial.println("==========================================");

    Serial.println("Keyboard: ACTIVE");
    Serial.println("Mouse: ACTIVE");

    Serial.println();
    Serial.println("Controls:");
    Serial.println("Joystick X -> A/D");
    Serial.println("Joystick Y -> W/S");
    Serial.println("Joystick SW -> Weapon 1-4");
    Serial.println("GPIO15 -> FIRE");
    Serial.println("GPIO16 -> ADS");
    Serial.println("GPIO17 -> RELOAD");
    Serial.println("MPU Gyro -> MOUSE LOOK");

    Serial.println();
    Serial.println("DO NOT MOVE THE CONTROLLER DURING STARTUP.");
    Serial.println();
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
    unsigned long now = millis();


    if (now - lastControlTime >= CONTROL_INTERVAL)
    {
        lastControlTime = now;


        // Movement
        handleJoystick();


        // Weapon switch
        handleWeaponSwitch();


        // Fire
        handleFire();


        // ADS
        handleADS();


        // Reload
        handleReload();


        // Mouse look
        handleGyro();
    }


    // Debug once per second
    if (now - lastDebugTime >= 1000)
    {
        lastDebugTime = now;

        printDebug();
    }


    delay(1);
}
