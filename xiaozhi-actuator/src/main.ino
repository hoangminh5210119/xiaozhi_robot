

#include "HeartRateBLE.h"
#include <AccelStepper.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <PS2X_lib.h> //for v1.6
#include <Wire.h>

#define PS2_DAT 23 // 14
#define PS2_CMD 14 // 15
#define PS2_SEL 13 // 16
#define PS2_CLK 12 // 17

#define pressures false
// #define rumble      true
#define rumble false

PS2X ps2x; // create PS2 Controller Class

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

#define MAX_SPEED 3000.0
#define STEPS_PER_MM 10.0

#define SERVO_MIN_US 500
#define SERVO_MAX_US 2500
#define SERVO_0 25
#define SERVO_1 33
#define SERVO_2 27
#define SERVO_3 26
#define LED_STATUS 12

// ==================== SERVO ANGLES ====================
int servoOpenAngles[4] = {90, 80, 90, 90};
int servoCloseAngles[4] = {150, 142, 150, 155};

// ==================== GLOBAL STATE ====================
AccelStepper mFL(AccelStepper::DRIVER, MOTOR_FL_STEP, MOTOR_FL_DIR);
AccelStepper mFR(AccelStepper::DRIVER, MOTOR_FR_STEP, MOTOR_FR_DIR);
AccelStepper mBL(AccelStepper::DRIVER, MOTOR_BL_STEP, MOTOR_BL_DIR);
AccelStepper mBR(AccelStepper::DRIVER, MOTOR_BR_STEP, MOTOR_BR_DIR);

Servo servos[4];
bool storageStates[4] = {false, false, false, false};

// Flags toggled by miscellaneous buttons (bits meaning assigned below)
uint8_t ps2_flags = 0;
// Flag bits mapping
// bit0: L1, bit1: R1, bit2: L2, bit3: R2, bit4: SELECT, bit5: START

// ==================== PS2 CONTROL FLAGS ====================
// Movement direction flags (set by loop, read by TaskMotorUpdate)
volatile int ps2_movement_dir = DIR_STOP;  // DIR_STOP, DIR_FORWARD, DIR_BACKWARD, DIR_LEFT, DIR_RIGHT

bool isMoving = false;
bool isDistanceMode = false;
bool isPS2ControlMode = false; // true = PS2 continuous control, false = I2C timed control
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

  mFL.setPinsInverted(true); // đảo chiều phù hợp chassis
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
  mFL.stop();
  mFR.stop();
  mBL.stop();
  mBR.stop();
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
    mFL.setSpeed(speed);
    mFR.setSpeed(speed);
    mBL.setSpeed(speed);
    mBR.setSpeed(speed);
    break;
  case DIR_BACKWARD:
    mFL.setSpeed(-speed);
    mFR.setSpeed(-speed);
    mBL.setSpeed(-speed);
    mBR.setSpeed(-speed);
    break;
  case DIR_LEFT:
    mFL.setSpeed(-speed);
    mFR.setSpeed(speed);
    mBL.setSpeed(speed);
    mBR.setSpeed(-speed);
    break;
  case DIR_RIGHT:
    mFL.setSpeed(speed);
    mFR.setSpeed(-speed);
    mBL.setSpeed(-speed);
    mBR.setSpeed(speed);
    break;
  case DIR_ROTATE_LEFT:
    mFL.setSpeed(-speed);
    mFR.setSpeed(speed);
    mBL.setSpeed(-speed);
    mBR.setSpeed(speed);
    break;
  case DIR_ROTATE_RIGHT:
    mFL.setSpeed(speed);
    mFR.setSpeed(-speed);
    mBL.setSpeed(speed);
    mBR.setSpeed(-speed);
    break;
  default:
    Serial.println("⚠️ Invalid direction");
    disableMotors();
    return;
  }

  Serial.printf("🚗 Move dir=%d | speed=%.0f | time=%dms\n", dir, speed,
                duration_ms);
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
    mFL.moveTo(steps);
    mFR.moveTo(steps);
    mBL.moveTo(steps);
    mBR.moveTo(steps);
    break;
  case DIR_BACKWARD:
    mFL.moveTo(-steps);
    mFR.moveTo(-steps);
    mBL.moveTo(-steps);
    mBR.moveTo(-steps);
    break;
  case DIR_LEFT:
    mFL.moveTo(-steps);
    mFR.moveTo(steps);
    mBL.moveTo(steps);
    mBR.moveTo(-steps);
    break;
  case DIR_RIGHT:
    mFL.moveTo(steps);
    mFR.moveTo(-steps);
    mBL.moveTo(-steps);
    mBR.moveTo(steps);
    break;
  case DIR_ROTATE_LEFT:
    mFL.moveTo(-steps);
    mFR.moveTo(steps);
    mBL.moveTo(-steps);
    mBR.moveTo(steps);
    break;
  case DIR_ROTATE_RIGHT:
    mFL.moveTo(steps);
    mFR.moveTo(-steps);
    mBL.moveTo(steps);
    mBR.moveTo(-steps);
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

  Serial.printf("📏 Move dir=%d | dist=%dmm (%ld steps) | speed=%.1f\n", dir,
                distance_mm, steps, speed);
  isMoving = true;
  isDistanceMode = true;
}

// ==================== UPDATE MOTORS ====================
void updateMotors() {
  // Check if PS2 flag requests movement change
  static int last_ps2_dir = DIR_STOP;
  int current_ps2_dir = ps2_movement_dir;
  
  if (current_ps2_dir != last_ps2_dir) {
    if (current_ps2_dir == DIR_STOP) {
      stopVehicle();
      isPS2ControlMode = false;
    } else {
      // PS2 continuous control mode - no timeout check
      isPS2ControlMode = true;
      isDistanceMode = false;
      
      float speed = MAX_SPEED;
      enableMotors();

      switch (current_ps2_dir) {
      case DIR_FORWARD:
        mFL.setSpeed(speed);
        mFR.setSpeed(speed);
        mBL.setSpeed(speed);
        mBR.setSpeed(speed);
        break;
      case DIR_BACKWARD:
        mFL.setSpeed(-speed);
        mFR.setSpeed(-speed);
        mBL.setSpeed(-speed);
        mBR.setSpeed(-speed);
        break;
      case DIR_LEFT:
        mFL.setSpeed(-speed);
        mFR.setSpeed(speed);
        mBL.setSpeed(speed);
        mBR.setSpeed(-speed);
        break;
      case DIR_RIGHT:
        mFL.setSpeed(speed);
        mFR.setSpeed(-speed);
        mBL.setSpeed(-speed);
        mBR.setSpeed(speed);
        break;
      case DIR_ROTATE_LEFT:
        mFL.setSpeed(-speed);
        mFR.setSpeed(speed);
        mBL.setSpeed(-speed);
        mBR.setSpeed(speed);
        break;
      case DIR_ROTATE_RIGHT:
        mFL.setSpeed(speed);
        mFR.setSpeed(-speed);
        mBL.setSpeed(speed);
        mBR.setSpeed(-speed);
        break;
      }
      
      isMoving = true;
      Serial.printf("🎮 PS2 Control: dir=%d | speed=%.0f (continuous)\n", current_ps2_dir, speed);
    }
    last_ps2_dir = current_ps2_dir;
  }

  // Continue running motors if moving
  if (!isMoving)
    return;

  if (isDistanceMode) {
    // Distance-based movement (from I2C command)
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
  } else if (isPS2ControlMode) {
    // PS2 continuous mode - run indefinitely until flag changes
    mFL.runSpeed();
    mFR.runSpeed();
    mBL.runSpeed();
    mBR.runSpeed();
  } else {
    // Timed movement (from I2C command)
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

void moveServoSmooth(Servo &servo, int currentAngle, int targetAngle,
                     int stepDelay = 10) {
  if (currentAngle == targetAngle)
    return;
  int step = (targetAngle > currentAngle) ? 1 : -1;
  for (int angle = currentAngle; angle != targetAngle; angle += step) {
    servo.write(angle);
    delay(stepDelay);
  }
  servo.write(targetAngle);
}

void controlStorageDoor(int slot, int action) {
  if (slot < 0 || slot > 3)
    return;
  int currentAngle =
      storageStates[slot] ? servoOpenAngles[slot] : servoCloseAngles[slot];
  int targetAngle =
      (action == ACT_OPEN) ? servoOpenAngles[slot] : servoCloseAngles[slot];

  Serial.printf("🚪 Storage[%d] %s slowly %d°→%d°\n", slot,
                (action == ACT_OPEN) ? "OPENING" : "CLOSING", currentAngle,
                targetAngle);

  moveServoSmooth(servos[slot], currentAngle, targetAngle, 10);
  storageStates[slot] = (action == ACT_OPEN);

  Serial.printf("✅ Storage[%d] %s (%d°)\n", slot,
                (action == ACT_OPEN) ? "OPENED" : "CLOSED", targetAngle);
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
    if (distance > 0)
      moveVehicleByDistance(dir, speed, distance);
    else if (duration > 0)
      moveVehicleByTime(dir, speed, duration);
    else
      stopVehicle();
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_OK) "}";
  }

  else if (type == TYPE_STORAGE) {
    int slot = doc[KEY_SLOT] | 0;
    int action = doc[KEY_ACTION] | ACT_CLOSE;
    controlStorageDoor(slot, action);
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_OK) "}";
  }

  else if (type == TYPE_STATUS) {
    StaticJsonDocument<32> s;
    // Trạng thái chính
    s[KEY_CONN] = hrBle.isConnected() ? 1 : 0;
    s[KEY_HR] = hrBle.getHeartRate();

    // Thêm mảng trạng thái 4 ngăn: [0,0,1,1]
    JsonArray arr = s.createNestedArray(KEY_STORAGE);
    for (int i = 0; i < 4; i++) {
      arr.add(storageStates[i] ? 1 : 0);
    }

    // Xuất JSON ra buffer và serial
    responseBuffer = "";
    serializeJson(s, responseBuffer);
    serializeJson(s, Serial);
    Serial.println();

  } else
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_UNKNOWN) "}";
  responseReady = true;
}

// Build response buffer in the required flat format: <conn>|<hr>|<servoStates>|<flags>
void updateResponseBuffer() {
  int conn = hrBle.isConnected() ? 1 : 0;
  int hr = hrBle.getHeartRate();

  // servoStates as 4 chars '0'/'1'
  String servosStr = "";
  for (int i = 0; i < 4; i++) {
    servosStr += (storageStates[i] ? '1' : '0');
  }

  String resp = String(conn) + "|" + String(hr) + "|" + servosStr + "|" + String(ps2_flags);
  responseBuffer = resp;
  responseReady = true;
}

// ==================== I2C HANDLERS ====================
void onI2CReceive(int bytes) {
  rxBuffer = "";
  while (Wire.available())
    rxBuffer += (char)Wire.read();
  if (rxBuffer.startsWith("{") && rxBuffer.endsWith("}")) {
    responseReady = false;
    processCommand(rxBuffer);
  } else {
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_ERROR) "}";
    responseReady = true;
  }
}

// void onI2CRequest() {
//   if (!responseReady || responseBuffer.isEmpty())
//     responseBuffer = "{\"" KEY_STATUS "\":0}";
//   Wire.write(responseBuffer.c_str());
//   responseReady = false;
// }

void onI2CRequest() {
  // Nếu chưa có phản hồi thì gửi trạng thái mặc định
  if (!responseReady || responseBuffer.isEmpty())
    responseBuffer = "{\"" KEY_STATUS "\":0}";

  const char *resp = responseBuffer.c_str();
  int len = responseBuffer.length();

  // Arduino core chỉ gửi tối đa 32 byte/lần → phải chia gói
  int sent = 0;
  while (sent < len) {
    int chunk = min(32, len - sent);
    Wire.write((const uint8_t *)(resp + sent), chunk);
    sent += chunk;
  }

  responseReady = false;
}

// ==================== TASKS ====================
void TaskHeartRateBluetooth(void *parameter) {
  bluetooth_init();
  for (;;) {
    hrBle.loop();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void TaskMotorUpdate(void *parameter) {
  for (;;) {
    updateMotors();
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

// ==================== MAIN ====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(
      "\n🚗 Xiaozhi Actuator v4.5 | PS2 Control with Task Architecture");

  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, LOW);

  setupMotors();
  setupServos();

  Wire.begin(I2C_SLAVE_ADDR, SDA_PIN, SCL_PIN, 100000);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  xTaskCreatePinnedToCore(TaskHeartRateBluetooth, "HeartRateBluetooth",
                          4096 * 5, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskMotorUpdate, "MotorUpdate", 4096 * 5, NULL, 1,
                          NULL, 0);

  digitalWrite(LED_STATUS, HIGH);
  Serial.println("✅ System ready!");
  Serial.println("📍 I2C Address: 0x55");

  ps2x.config_gamepad(PS2_CLK, PS2_CMD, PS2_SEL, PS2_DAT, pressures, rumble);
  // Initialize response buffer for master
  updateResponseBuffer();
}

byte vibrate = 0;

void loop() {
  ps2x.read_gamepad(false, vibrate); // read controller

  // 1) Set movement direction flag based on D-pad
  if (ps2x.Button(PSB_PAD_UP)) {
    ps2_movement_dir = DIR_FORWARD;
  } else if (ps2x.Button(PSB_PAD_DOWN)) {
    ps2_movement_dir = DIR_BACKWARD;
  } else if (ps2x.Button(PSB_PAD_LEFT)) {
    ps2_movement_dir = DIR_LEFT;
  } else if (ps2x.Button(PSB_PAD_RIGHT)) {
    ps2_movement_dir = DIR_RIGHT;
  } else {
    ps2_movement_dir = DIR_STOP;
  }

  // 2) Control servos directly on single press
  if (ps2x.ButtonPressed(PSB_CROSS)) {
    int slot = 0;
    controlStorageDoor(slot, storageStates[slot] ? ACT_CLOSE : ACT_OPEN);
    updateResponseBuffer();
  }
  if (ps2x.ButtonPressed(PSB_SQUARE)) {
    int slot = 1;
    controlStorageDoor(slot, storageStates[slot] ? ACT_CLOSE : ACT_OPEN);
    updateResponseBuffer();
  }
  if (ps2x.ButtonPressed(PSB_TRIANGLE)) {
    int slot = 2;
    controlStorageDoor(slot, storageStates[slot] ? ACT_CLOSE : ACT_OPEN);
    updateResponseBuffer();
  }
  if (ps2x.ButtonPressed(PSB_CIRCLE)) {
    int slot = 3;
    controlStorageDoor(slot, storageStates[slot] ? ACT_CLOSE : ACT_OPEN);
    updateResponseBuffer();
  }

  // 3) Other buttons toggle flags (flip bit on press)
  if (ps2x.ButtonPressed(PSB_L1)) {
    ps2_flags ^= (1 << 0);
    updateResponseBuffer();
  }
  if (ps2x.ButtonPressed(PSB_R1)) {
    ps2_flags ^= (1 << 1);
    updateResponseBuffer();
  }
  if (ps2x.ButtonPressed(PSB_L2)) {
    ps2_flags ^= (1 << 2);
    updateResponseBuffer();
  }
  if (ps2x.ButtonPressed(PSB_R2)) {
    ps2_flags ^= (1 << 3);
    updateResponseBuffer();
  }
  if (ps2x.ButtonPressed(PSB_SELECT)) {
    ps2_flags ^= (1 << 4);
    updateResponseBuffer();
  }
  if (ps2x.ButtonPressed(PSB_START)) {
    ps2_flags ^= (1 << 5);
    updateResponseBuffer();
  }

  // optional: use an analog value for vibration feedback
  vibrate = ps2x.Analog(PSAB_CROSS);
  
  delay(10); // Small delay to avoid overwhelming the controller
}
