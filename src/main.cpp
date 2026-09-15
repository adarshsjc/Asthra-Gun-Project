#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// --- Pin Definitions ---
#define I2C_SDA 8
#define I2C_SCL 9

#define JOY_X 1
#define JOY_Y 2
#define JOY_SW 10

// --- NEW COMPLETELY SAFE PINS ---
#define BTN_1 15
#define BTN_2 16
#define BTN_3 17

Adafruit_MPU6050 mpu;

void setup() {
  // Start serial communication at 115200 baud
  Serial.begin(115200);
  delay(2000); 

  Serial.println("Initializing System...");

  // --- Initialize Buttons ---
  pinMode(JOY_SW, INPUT_PULLUP);
  pinMode(BTN_1, INPUT_PULLUP);
  pinMode(BTN_2, INPUT_PULLUP);
  pinMode(BTN_3, INPUT_PULLUP);

  // --- Initialize I2C on Custom Pins ---
  Wire.begin(I2C_SDA, I2C_SCL);

  // --- Initialize MPU-6050 ---
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip! Check I2C wiring.");
    while (1) { delay(10); } 
  }
  Serial.println("MPU6050 Found!");

  // Configure sensor settings
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
}

void loop() {
  // --- 1. Read MPU-6050 ---
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // --- 2. Read Joystick Analog Values ---
  int joyX = analogRead(JOY_X);
  int joyY = analogRead(JOY_Y);

  // --- 3. Read Buttons ---
  bool joySwPressed = (digitalRead(JOY_SW) == LOW);
  bool btn1Pressed  = (digitalRead(BTN_1) == LOW);
  bool btn2Pressed  = (digitalRead(BTN_2) == LOW);
  bool btn3Pressed  = (digitalRead(BTN_3) == LOW);

  // --- 4. Print Data to Serial Monitor ---
  Serial.print("Accel [X: "); Serial.print(a.acceleration.x, 2);
  Serial.print(" Y: "); Serial.print(a.acceleration.y, 2);
  Serial.print(" Z: "); Serial.print(a.acceleration.z, 2);
  Serial.print("] | ");

  Serial.print("Joy [X: "); Serial.print(joyX);
  Serial.print(" Y: "); Serial.print(joyY);
  Serial.print("] | ");

  // Display 'X' if pressed, '-' if unpressed
  Serial.print("Btns [Joy 1 2 3]: ");
  Serial.print(joySwPressed ? "X " : "- ");
  Serial.print(btn1Pressed ? "X " : "- ");
  Serial.print(btn2Pressed ? "X " : "- ");
  Serial.println(btn3Pressed ? "X" : "-");

  delay(100); 
}