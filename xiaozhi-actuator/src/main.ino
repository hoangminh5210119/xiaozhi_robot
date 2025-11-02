

#include "HeartRateBLE.h"
#include <AccelStepper.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <Wire.h>

// ==================== MACRO & JSON KEYS ====================
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define KEY_TYPE "t"
#define KEY_DIR "d"
#define KEY_SPEED "p"
#define KEY_DURATION "ms"
#define KEY_DISTANCE "mm"
#define KEY_SLOT "i"
#define KEY_ACTION "a"
#define KEY_STATUS "s"
#define KEY_BATTERY "b"
#define KEY_CONN "c"
#define KEY_HR "h"
#define KEY_MOTOR_EN "m"
#define KEY_MOVING "v"
#define KEY_STORAGE "g"
#define KEY_ITEM_SLOT "i"
#define KEY_ITEM_STATE "o"

#define TYPE_VEHICLE "v"
#define TYPE_STORAGE "s"
#define TYPE_STATUS "t"

#define DIR_STOP 0
#define DIR_FORWARD 1
#define DIR_BACKWARD 2
#define DIR_LEFT 3
#define DIR_RIGHT 4
#define DIR_ROTATE_LEFT 5
#define DIR_ROTATE_RIGHT 6

#define ACT_CLOSE 0
#define ACT_OPEN 1

#define STATUS_OK 1
#define STATUS_ERROR -1
#define STATUS_UNKNOWN -2

// ==================== BLE SETUP ====================
HeartRateBLE hrBle;

void onData(int bpm, int battery) {
  Serial.printf("👉 [BPM=%d | Battery=%d%%]\n", bpm, battery);
}

void onConnect(bool connected) {
  Serial.printf(connected ? "🟢 BLE Connected!\n" : "🔴 BLE Disconnected!\n");
}

void bluetooth_init() {
  hrBle.begin();
  hrBle.setDataCallback(onData);
  hrBle.setConnectCallback(onConnect);
  hrBle.setAutoReconnect(true, 5);
  hrBle.connectDirect("c7:f9:08:e7:fe:1b", BLE_ADDR_TYPE_RANDOM);
}

// ==================== HARDWARE CONFIG ====================
#define I2C_SLAVE_ADDR 0x55
#define SDA_PIN 21
#define SCL_PIN 22

#define MOTOR_EN 19
#define MOTOR_FL_STEP 0
#define MOTOR_FL_DIR 4
#define MOTOR_FR_STEP 16
#define MOTOR_FR_DIR 17
#define MOTOR_BL_STEP 2
#define MOTOR_BL_DIR 15
#define MOTOR_BR_STEP 5
#define MOTOR_BR_DIR 18

#define MAX_SPEED 2000.0
#define STEPS_PER_MM 10.0

#define SERVO_MIN_US 500
#define SERVO_MAX_US 2500
#define SERVO_0 25
#define SERVO_1 33
#define SERVO_2 27
#define SERVO_3 26
#define LED_STATUS 12

// ==================== SERVO ANGLES ====================
int servoOpenAngles[4]  = {90, 80, 90, 90};
int servoCloseAngles[4] = {150, 142, 150, 155};

// ==================== GLOBAL STATE ====================
AccelStepper mFL(AccelStepper::DRIVER, MOTOR_FL_STEP, MOTOR_FL_DIR);
AccelStepper mFR(AccelStepper::DRIVER, MOTOR_FR_STEP, MOTOR_FR_DIR);
AccelStepper mBL(AccelStepper::DRIVER, MOTOR_BL_STEP, MOTOR_BL_DIR);
AccelStepper mBR(AccelStepper::DRIVER, MOTOR_BR_STEP, MOTOR_BR_DIR);

Servo servos[4];
bool storageStates[4] = {false, false, false, false};

bool isMoving = false;
bool isDistanceMode = false;
unsigned long moveStartTime = 0;
unsigned long moveDuration = 0;

String rxBuffer = "";
String responseBuffer = "";
bool responseReady = false;

// ==================== MOTOR CONTROL ====================
void enableMotors() { digitalWrite(MOTOR_EN, LOW); }
void disableMotors() { digitalWrite(MOTOR_EN, HIGH); }

void setupMotors() {
  pinMode(MOTOR_EN, OUTPUT);
  disableMotors();

  mFL.setPinsInverted(true);  // đảo chiều phù hợp chassis
  mBL.setPinsInverted(true);

  mFL.setMaxSpeed(MAX_SPEED);
  mFR.setMaxSpeed(MAX_SPEED);
  mBL.setMaxSpeed(MAX_SPEED);
  mBR.setMaxSpeed(MAX_SPEED);

  mFL.setAcceleration(MAX_SPEED * 2);
  mFR.setAcceleration(MAX_SPEED * 2);
  mBL.setAcceleration(MAX_SPEED * 2);
  mBR.setAcceleration(MAX_SPEED * 2);
}

void stopVehicle() {
  mFL.stop(); mFR.stop(); mBL.stop(); mBR.stop();
  disableMotors();
  isMoving = false;
  isDistanceMode = false;
  Serial.println("🛑 Vehicle stopped");
}

// ==================== MOVE BY TIME ====================
void moveVehicleByTime(int dir, int speedPercent, int duration_ms) {
  if (speedPercent <= 0 || duration_ms <= 0) {
    Serial.println("⚠️ Invalid time parameters");
    return;
  }

  float speed = map(speedPercent, 0, 100, 0, MAX_SPEED);
  enableMotors();

  switch (dir) {
    case DIR_FORWARD:
      mFL.setSpeed(speed);  mFR.setSpeed(speed);
      mBL.setSpeed(speed);  mBR.setSpeed(speed);
      break;
    case DIR_BACKWARD:
      mFL.setSpeed(-speed); mFR.setSpeed(-speed);
      mBL.setSpeed(-speed); mBR.setSpeed(-speed);
      break;
    case DIR_LEFT:
      mFL.setSpeed(-speed); mFR.setSpeed(speed);
      mBL.setSpeed(speed);  mBR.setSpeed(-speed);
      break;
    case DIR_RIGHT:
      mFL.setSpeed(speed);  mFR.setSpeed(-speed);
      mBL.setSpeed(-speed); mBR.setSpeed(speed);
      break;
    case DIR_ROTATE_LEFT:
      mFL.setSpeed(-speed); mFR.setSpeed(speed);
      mBL.setSpeed(-speed); mBR.setSpeed(speed);
      break;
    case DIR_ROTATE_RIGHT:
      mFL.setSpeed(speed);  mFR.setSpeed(-speed);
      mBL.setSpeed(speed);  mBR.setSpeed(-speed);
      break;
    default:
      Serial.println("⚠️ Invalid direction");
      disableMotors();
      return;
  }

  Serial.printf("🚗 Move dir=%d | speed=%.0f | time=%dms\n", dir, speed, duration_ms);
  isMoving = true;
  isDistanceMode = false;
  moveStartTime = millis();
  moveDuration = duration_ms;
}

// ==================== MOVE BY DISTANCE ====================
void moveVehicleByDistance(int dir, int speedPercent, int distance_mm) {
  if (speedPercent <= 0 || distance_mm <= 0) {
    Serial.println("⚠️ Invalid distance parameters");
    return;
  }

  float speed = map(speedPercent, 0, 100, 0, MAX_SPEED);
  long steps = (long)(distance_mm * STEPS_PER_MM);
  enableMotors();

  mFL.setCurrentPosition(0);
  mFR.setCurrentPosition(0);
  mBL.setCurrentPosition(0);
  mBR.setCurrentPosition(0);

  switch (dir) {
    case DIR_FORWARD:
      mFL.moveTo(steps);  mFR.moveTo(steps);
      mBL.moveTo(steps);  mBR.moveTo(steps);
      break;
    case DIR_BACKWARD:
      mFL.moveTo(-steps); mFR.moveTo(-steps);
      mBL.moveTo(-steps); mBR.moveTo(-steps);
      break;
    case DIR_LEFT:
      mFL.moveTo(-steps); mFR.moveTo(steps);
      mBL.moveTo(steps);  mBR.moveTo(-steps);
      break;
    case DIR_RIGHT:
      mFL.moveTo(steps);  mFR.moveTo(-steps);
      mBL.moveTo(-steps); mBR.moveTo(steps);
      break;
    case DIR_ROTATE_LEFT:
      mFL.moveTo(-steps); mFR.moveTo(steps);
      mBL.moveTo(-steps); mBR.moveTo(steps);
      break;
    case DIR_ROTATE_RIGHT:
      mFL.moveTo(steps);  mFR.moveTo(-steps);
      mBL.moveTo(steps);  mBR.moveTo(-steps);
      break;
    default:
      Serial.println("⚠️ Invalid direction for distance move");
      disableMotors();
      return;
  }

  mFL.setMaxSpeed(speed);
  mFR.setMaxSpeed(speed);
  mBL.setMaxSpeed(speed);
  mBR.setMaxSpeed(speed);

  mFL.setAcceleration(speed * 2);
  mFR.setAcceleration(speed * 2);
  mBL.setAcceleration(speed * 2);
  mBR.setAcceleration(speed * 2);

  Serial.printf("📏 Move dir=%d | dist=%dmm (%ld steps) | speed=%.1f\n", dir, distance_mm, steps, speed);
  isMoving = true;
  isDistanceMode = true;
}

// ==================== UPDATE MOTORS ====================
void updateMotors() {
  if (!isMoving) return;

  if (isDistanceMode) {
    bool flDone = (mFL.distanceToGo() == 0);
    bool frDone = (mFR.distanceToGo() == 0);
    bool blDone = (mBL.distanceToGo() == 0);
    bool brDone = (mBR.distanceToGo() == 0);

    mFL.run();
    mFR.run();
    mBL.run();
    mBR.run();

    if (flDone && frDone && blDone && brDone) {
      stopVehicle();
      Serial.println("✅ Distance move complete");
    }
  } else {
    mFL.runSpeed();
    mFR.runSpeed();
    mBL.runSpeed();
    mBR.runSpeed();

    if (millis() - moveStartTime >= moveDuration) {
      stopVehicle();
      Serial.println("✅ Timed move complete");
    }
  }
}

// ==================== SERVO CONTROL (SMOOTH) ====================
void setupServos() {
  servos[0].attach(SERVO_0, SERVO_MIN_US, SERVO_MAX_US);
  servos[1].attach(SERVO_1, SERVO_MIN_US, SERVO_MAX_US);
  servos[2].attach(SERVO_2, SERVO_MIN_US, SERVO_MAX_US);
  servos[3].attach(SERVO_3, SERVO_MIN_US, SERVO_MAX_US);
  for (int i = 0; i < 4; i++) {
    servos[i].write(servoCloseAngles[i]);
    storageStates[i] = false;
  }
  Serial.println("🚪 All storage doors closed");
}

void moveServoSmooth(Servo &servo, int currentAngle, int targetAngle, int stepDelay = 10) {
  if (currentAngle == targetAngle) return;
  int step = (targetAngle > currentAngle) ? 1 : -1;
  for (int angle = currentAngle; angle != targetAngle; angle += step) {
    servo.write(angle);
    delay(stepDelay);
  }
  servo.write(targetAngle);
}

void controlStorageDoor(int slot, int action) {
  if (slot < 0 || slot > 3) return;
  int currentAngle = storageStates[slot] ? servoOpenAngles[slot] : servoCloseAngles[slot];
  int targetAngle  = (action == ACT_OPEN) ? servoOpenAngles[slot] : servoCloseAngles[slot];

  Serial.printf("🚪 Storage[%d] %s slowly %d°→%d°\n",
                slot, (action == ACT_OPEN) ? "OPENING" : "CLOSING", currentAngle, targetAngle);

  moveServoSmooth(servos[slot], currentAngle, targetAngle, 10);
  storageStates[slot] = (action == ACT_OPEN);

  Serial.printf("✅ Storage[%d] %s (%d°)\n",
                slot, (action == ACT_OPEN) ? "OPENED" : "CLOSED", targetAngle);
}

// ==================== JSON COMMAND ====================
void processCommand(String jsonStr) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, jsonStr)) {
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_ERROR) "}";
    responseReady = true;
    return;
  }

  String type = doc[KEY_TYPE] | "";

  if (type == TYPE_VEHICLE) {
    int dir = doc[KEY_DIR] | DIR_STOP;
    int speed = doc[KEY_SPEED] | 50;
    int duration = doc[KEY_DURATION] | 0;
    int distance = doc[KEY_DISTANCE] | 0;
    if (distance > 0) moveVehicleByDistance(dir, speed, distance);
    else if (duration > 0) moveVehicleByTime(dir, speed, duration);
    else stopVehicle();
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_OK) "}";
  }

  else if (type == TYPE_STORAGE) {
    int slot = doc[KEY_SLOT] | 0;
    int action = doc[KEY_ACTION] | ACT_CLOSE;
    controlStorageDoor(slot, action);
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_OK) "}";
  }

  else if (type == TYPE_STATUS) {
    StaticJsonDocument<300> s;
    s[KEY_STATUS] = STATUS_OK;
    s[KEY_BATTERY] = 12.4;
    s[KEY_CONN] = hrBle.isConnected() ? 1 : 0;
    s[KEY_HR] = hrBle.getHeartRate();
    s[KEY_MOTOR_EN] = (digitalRead(MOTOR_EN) == LOW) ? 1 : 0;
    s[KEY_MOVING] = isMoving ? 1 : 0;

    JsonArray arr = s[KEY_STORAGE].to<JsonArray>();
    for (int i = 0; i < 4; i++) {
      JsonObject item = arr.add<JsonObject>();
      item[KEY_ITEM_SLOT] = i;
      item[KEY_ITEM_STATE] = storageStates[i] ? 1 : 0;
    }
    responseBuffer = "";
    serializeJson(s, responseBuffer);
  }

  else responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_UNKNOWN) "}";
  responseReady = true;
}

// ==================== I2C HANDLERS ====================
void onI2CReceive(int bytes) {
  rxBuffer = "";
  while (Wire.available()) rxBuffer += (char)Wire.read();
  if (rxBuffer.startsWith("{") && rxBuffer.endsWith("}")) {
    responseReady = false;
    processCommand(rxBuffer);
  } else {
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_ERROR) "}";
    responseReady = true;
  }
}

void onI2CRequest() {
  if (!responseReady || responseBuffer.isEmpty())
    responseBuffer = "{\"" KEY_STATUS "\":0}";
  Wire.write(responseBuffer.c_str());
  responseReady = false;
}

// ==================== TASKS ====================
void TaskHeartRateBluetooth(void *parameter) {
  bluetooth_init();
  for (;;) { hrBle.loop(); vTaskDelay(10 / portTICK_PERIOD_MS); }
}

void TaskMotorUpdate(void *parameter) {
  for (;;) { updateMotors(); vTaskDelay(1 / portTICK_PERIOD_MS); }
}

// ==================== MAIN ====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n🚗 Xiaozhi Actuator v4.4 | Distance & Servo Smooth Control");

  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, LOW);

  setupMotors();
  setupServos();

  Wire.begin(I2C_SLAVE_ADDR, SDA_PIN, SCL_PIN, 100000);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  xTaskCreatePinnedToCore(TaskHeartRateBluetooth, "HeartRateBluetooth", 4096*5, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskMotorUpdate, "MotorUpdate", 4096*5, NULL, 1, NULL, 0);

  digitalWrite(LED_STATUS, HIGH);
  Serial.println("✅ System ready!");
  Serial.println("📍 I2C Address: 0x55");
}

void loop() {}
