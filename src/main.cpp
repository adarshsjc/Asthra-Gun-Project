#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ─── PIN DEFINITIONS ───────────────────────────────────────────────
#define MPU_SDA        8
#define MPU_SCL        9
#define JOYSTICK_VRX   1
#define JOYSTICK_VRY   2
#define JOYSTICK_SW    10
#define SHOOT_BUTTON   15
#define RELOAD_BUTTON  16
#define PAUSE_BUTTON   17

// ─── TUNING ────────────────────────────────────────────────────────
#define SAMPLE_RATE_HZ   50
#define SAMPLE_PERIOD_MS (1000 / SAMPLE_RATE_HZ)  // 20 ms
#define COMP_ALPHA       0.98f   // complementary filter: trust gyro 98%, accel 2%
#define CALIBRATION_MS   2000    // 2 seconds of startup calibration
#define GYRO_DEADZONE    0.02f   // rad/s — ignore noise below this

Adafruit_MPU6050 mpu;

// ─── STATE ─────────────────────────────────────────────────────────
float yaw   = 0.0f;   // degrees, integrated from gyro Z
float pitch = 0.0f;   // degrees, fused from gyro X + accel tilt

// Calibration offsets (average gyro bias at rest)
float gyroBiasX = 0.0f;
float gyroBiasY = 0.0f;
float gyroBiasZ = 0.0f;

bool calibrated = false;
unsigned long calibStart = 0;
int calibSamples = 0;
float calibSumX = 0.0f, calibSumY = 0.0f, calibSumZ = 0.0f;

unsigned long lastTime = 0;

// Button edge detection
int lastShoot = 0;
int lastReload = 0;
int lastScope = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Configure button pins with internal pull-ups
    pinMode(SHOOT_BUTTON, INPUT_PULLUP);
    pinMode(RELOAD_BUTTON, INPUT_PULLUP);
    pinMode(PAUSE_BUTTON, INPUT_PULLUP);
    pinMode(JOYSTICK_SW, INPUT_PULLUP);

    // Initialize I2C for MPU6050
    Wire.begin(MPU_SDA, MPU_SCL);
    if (!mpu.begin()) {
        Serial.println("{\"E\":\"MPU6050 not found!\"}");
        while (1) { delay(1000); }
    }

    // Configure MPU6050 for responsive, low-noise operation
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

    Serial.println("{\"E\":\"Calibrating... hold still\"}");
    calibStart = millis();
    lastTime = micros();
}

void loop() {
    unsigned long now = micros();
    float dt = (now - lastTime) / 1000000.0f;  // seconds
    lastTime = now;

    // Clamp dt to avoid spikes
    if (dt <= 0.0f || dt > 0.1f) dt = (float)SAMPLE_PERIOD_MS / 1000.0f;

    // Read sensor data
    sensors_event_t accel, gyro, temp;
    mpu.getEvent(&accel, &gyro, &temp);

    float gx = gyro.gyro.x;   // rad/s
    float gy = gyro.gyro.y;   // rad/s
    float gz = gyro.gyro.z;   // rad/s

    // ─── CALIBRATION PHASE ─────────────────────────────────────────
    if (!calibrated) {
        calibSumX += gx;
        calibSumY += gy;
        calibSumZ += gz;
        calibSamples++;

        if (millis() - calibStart >= CALIBRATION_MS) {
            gyroBiasX = calibSumX / calibSamples;
            gyroBiasY = calibSumY / calibSamples;
            gyroBiasZ = calibSumZ / calibSamples;
            calibrated = true;
            yaw = 0.0f;
            pitch = 0.0f;
            Serial.println("{\"E\":\"Calibration done\"}");
        }
        delay(SAMPLE_PERIOD_MS);
        return;
    }

    // ─── REMOVE BIAS ───────────────────────────────────────────────
    gx -= gyroBiasX;
    gy -= gyroBiasY;
    gz -= gyroBiasZ;

    // ─── DEADZONE FILTER ───────────────────────────────────────────
    if (fabsf(gx) < GYRO_DEADZONE) gx = 0.0f;
    if (fabsf(gy) < GYRO_DEADZONE) gy = 0.0f;
    if (fabsf(gz) < GYRO_DEADZONE) gz = 0.0f;

    // ─── PITCH: COMPLEMENTARY FILTER (gyro + accel) ────────────────
    // Accelerometer-derived pitch (tilt angle)
    float accelPitch = atan2f(-accel.acceleration.x,
                              sqrtf(accel.acceleration.y * accel.acceleration.y +
                                    accel.acceleration.z * accel.acceleration.z));
    float accelPitchDeg = accelPitch * 57.2958f;  // RAD_TO_DEG

    // Gyro-integrated pitch
    float gyroPitchDelta = gy * dt * 57.2958f;  // GyroY is pitch rotation

    // Complementary filter: high-pass gyro + low-pass accel
    pitch = COMP_ALPHA * (pitch + gyroPitchDelta) + (1.0f - COMP_ALPHA) * accelPitchDeg;

    // ─── YAW: GYRO-ONLY INTEGRATION ───────────────────────────────
    // (Accelerometer cannot measure yaw — no magnetometer on MPU6050)
    float gyroYawDelta = gz * dt * 57.2958f;  // GyroZ is yaw rotation
    yaw += gyroYawDelta;

    // ─── READ BUTTONS ──────────────────────────────────────────────
    int shoot  = (digitalRead(SHOOT_BUTTON)  == LOW) ? 1 : 0;
    int reload = (digitalRead(RELOAD_BUTTON) == LOW) ? 1 : 0;
    int scope  = (digitalRead(JOYSTICK_SW)   == LOW) ? 1 : 0;
    int pauseBtn = (digitalRead(PAUSE_BUTTON) == LOW) ? 1 : 0;

    // ─── READ JOYSTICK ANALOG ──────────────────────────────────────
    int joyX = analogRead(JOYSTICK_VRX);
    int joyY = analogRead(JOYSTICK_VRY);

    // ─── SEND JSON ─────────────────────────────────────────────────
    // Compact keys: Y=Yaw, P=Pitch, S=Shoot, R=Reload, SC=Scope,
    //               JX=JoystickX, JY=JoystickY, GX/GY/GZ=raw gyro rates
    Serial.print("{\"Y\":");
    Serial.print(yaw, 2);
    Serial.print(",\"P\":");
    Serial.print(pitch, 2);
    Serial.print(",\"GX\":");
    Serial.print(gx, 3);
    Serial.print(",\"GY\":");
    Serial.print(gy, 3);
    Serial.print(",\"GZ\":");
    Serial.print(gz, 3);
    Serial.print(",\"S\":");
    Serial.print(shoot);
    Serial.print(",\"R\":");
    Serial.print(reload);
    Serial.print(",\"SC\":");
    Serial.print(scope);
    Serial.print(",\"JX\":");
    Serial.print(joyX);
    Serial.print(",\"JY\":");
    Serial.print(joyY);
    Serial.print(",\"PA\":");
    Serial.print(pauseBtn);
    Serial.println("}");

    delay(SAMPLE_PERIOD_MS);
}
