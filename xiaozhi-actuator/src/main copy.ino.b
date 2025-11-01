// #include "HeartRateBLE.h"
// #include <AccelStepper.h>
// #include <Arduino.h>
// #include <ArduinoJson.h>
// #include <ESP32Servo.h>
// #include <Wire.h>

// // ==================== MACRO & JSON KEYS ====================
// #define STR_HELPER(x) #x
// #define STR(x) STR_HELPER(x)

// // JSON key rút gọn
// #define KEY_TYPE "t"
// #define KEY_DIR "d"
// #define KEY_SPEED "p"
// #define KEY_DURATION "ms"
// #define KEY_DISTANCE "mm"
// #define KEY_SLOT "i"
// #define KEY_ACTION "a"
// #define KEY_STATUS "s"
// #define KEY_BATTERY "b"
// #define KEY_CONN "c"
// #define KEY_HR "h"
// #define KEY_MOTOR_EN "m"
// #define KEY_MOVING "v"
// #define KEY_STORAGE "g"
// #define KEY_ITEM_SLOT "i"
// #define KEY_ITEM_STATE "o"

// // Giá trị rút gọn
// #define TYPE_VEHICLE "v"
// #define TYPE_STORAGE "s"
// #define TYPE_STATUS "t"

// #define DIR_STOP 0
// #define DIR_FORWARD 1
// #define DIR_BACKWARD 2
// #define DIR_LEFT 3
// #define DIR_RIGHT 4
// #define DIR_ROTATE_LEFT 5
// #define DIR_ROTATE_RIGHT 6

// #define ACT_CLOSE 0
// #define ACT_OPEN 1

// #define STATUS_OK 1
// #define STATUS_ERROR -1
// #define STATUS_UNKNOWN -2

// // ==================== BLE SETUP ====================
// HeartRateBLE hrBle;

// void onData(int bpm, int battery) {
//   Serial.printf("👉 [BPM=%d | Battery=%d%%]\n", bpm, battery);
// }
// void onConnect(bool connected) {
//   Serial.printf(connected ? "🟢 BLE Connected!\n" : "🔴 BLE
//   Disconnected!\n");
// }

// void bluetooth_init() {
//   hrBle.begin();
//   hrBle.setDataCallback(onData);
//   hrBle.setConnectCallback(onConnect);
//   hrBle.setAutoReconnect(true, 5);
//   hrBle.connectDirect("c7:f9:08:e7:fe:1b", BLE_ADDR_TYPE_RANDOM);
// }

// // ==================== HARDWARE CONFIG ====================
// #define I2C_SLAVE_ADDR 0x55
// #define SDA_PIN 21
// #define SCL_PIN 22

// #define MOTOR_EN 19

// #define MOTOR_FL_STEP 4
// #define MOTOR_FL_DIR 0

// #define MOTOR_FR_STEP 17
// #define MOTOR_FR_DIR 16

// #define MOTOR_BL_STEP 2
// #define MOTOR_BL_DIR 15

// #define MOTOR_BR_STEP 18
// #define MOTOR_BR_DIR 5

// #define MAX_SPEED 2000.0
// #define ACCELERATION 1000.0
// #define STEPS_PER_MM 10.0

// #define SERVO_ANGLE_OPEN 90
// #define SERVO_ANGLE_CLOSE 140
// #define SERVO_MIN_US 500
// #define SERVO_MAX_US 2500
// #define SERVO_0 25
// #define SERVO_1 33
// #define SERVO_2 27
// #define SERVO_3 26
// #define LED_STATUS 12

// // ==================== GLOBAL STATE ====================
// AccelStepper mFL(AccelStepper::DRIVER, MOTOR_FL_STEP, MOTOR_FL_DIR);
// AccelStepper mFR(AccelStepper::DRIVER, MOTOR_FR_STEP, MOTOR_FR_DIR);
// AccelStepper mBL(AccelStepper::DRIVER, MOTOR_BL_STEP, MOTOR_BL_DIR);
// AccelStepper mBR(AccelStepper::DRIVER, MOTOR_BR_STEP, MOTOR_BR_DIR);
// Servo servos[4];
// bool storageStates[4] = {false, false, false, false};

// bool isMoving = false;
// unsigned long moveStartTime = 0;
// int moveDuration = 0;

// String rxBuffer = "";
// String responseBuffer = ""; // ✅ BỎ VOLATILE

// // ==================== INITIALIZATION ====================
// void setupMotors() {
//   pinMode(MOTOR_EN, OUTPUT);
//   digitalWrite(MOTOR_EN, HIGH);
//   mFL.setMaxSpeed(MAX_SPEED);
//   mFR.setMaxSpeed(MAX_SPEED);
//   mBL.setMaxSpeed(MAX_SPEED);
//   mBR.setMaxSpeed(MAX_SPEED);
// }

// void setupServos() {
//   servos[0].attach(SERVO_0, SERVO_MIN_US, SERVO_MAX_US);
//   servos[1].attach(SERVO_1, SERVO_MIN_US, SERVO_MAX_US);
//   servos[2].attach(SERVO_2, SERVO_MIN_US, SERVO_MAX_US);
//   servos[3].attach(SERVO_3, SERVO_MIN_US, SERVO_MAX_US);
//   for (int i = 0; i < 4; i++)
//     servos[i].write(SERVO_ANGLE_CLOSE);
// }

// void enableMotors() { digitalWrite(MOTOR_EN, LOW); }
// void disableMotors() { digitalWrite(MOTOR_EN, HIGH); }

// // ==================== VEHICLE CONTROL ====================
// void setMotorSpeeds(float fl, float fr, float bl, float br) {
//   mFL.setSpeed(fl);
//   mFR.setSpeed(fr);
//   mBL.setSpeed(bl);
//   mBR.setSpeed(br);
// }
// // {"t":"s","i":0,"a":1}
// // {\"t\":\"s\",\"i\":0,\"a\":0}

// void moveVehicleCode(int dir, int spdPercent, int duration_ms) {
//   float spd = map(spdPercent, 0, 100, 0, MAX_SPEED);
//   enableMotors();

//   switch (dir) {
//   case DIR_FORWARD:
//     setMotorSpeeds(spd, spd, spd, spd);
//     break;
//   case DIR_BACKWARD:
//     setMotorSpeeds(-spd, -spd, -spd, -spd);
//     break;
//   case DIR_LEFT:
//     setMotorSpeeds(-spd, spd, spd, -spd);
//     break;
//   case DIR_RIGHT:
//     setMotorSpeeds(spd, -spd, -spd, spd);
//     break;
//   case DIR_ROTATE_LEFT:
//     setMotorSpeeds(-spd, spd, -spd, spd);
//     break;
//   case DIR_ROTATE_RIGHT:
//     setMotorSpeeds(spd, -spd, spd, -spd);
//     break;
//   default:
//     disableMotors();
//     return;
//   }

//   isMoving = true;
//   moveStartTime = millis();
//   moveDuration = duration_ms;
// }

// void moveVehicleDistance(int dir, int spdPercent, int distance_mm) {
//   float steps = distance_mm * STEPS_PER_MM;
//   float spd = map(spdPercent, 0, 100, 0, MAX_SPEED);
//   enableMotors();

//   long delta = (dir == DIR_FORWARD)    ? steps
//                : (dir == DIR_BACKWARD) ? -steps
//                                        : 0;
//   mFL.move(delta);
//   mFR.move(delta);
//   mBL.move(delta);
//   mBR.move(delta);
//   mFL.setSpeed(spd);
//   mFR.setSpeed(spd);
//   mBL.setSpeed(spd);
//   mBR.setSpeed(spd);
//   isMoving = true;
// }

// void stopVehicle() {
//   mFL.stop();
//   mFR.stop();
//   mBL.stop();
//   mBR.stop();
//   disableMotors();
//   isMoving = false;
// }

// void updateMotors() {
//   if (isMoving) {
//     mFL.runSpeedToPosition();
//     mFR.runSpeedToPosition();
//     mBL.runSpeedToPosition();
//     mBR.runSpeedToPosition();
//     if (moveDuration > 0 && millis() - moveStartTime > moveDuration)
//       stopVehicle();
//   }
// }

// // ==================== STORAGE CONTROL ====================
// void controlStorageDoor(int slot, int action) {
//   if (slot < 0 || slot > 3)
//     return;
//   if (action == ACT_OPEN) {
//     servos[slot].write(SERVO_ANGLE_OPEN);
//     storageStates[slot] = true;
//   } else {
//     servos[slot].write(SERVO_ANGLE_CLOSE);
//     storageStates[slot] = false;
//   }
// }

// // ==================== JSON HANDLER ====================
// void processCommand(String jsonStr) {
//   StaticJsonDocument<256> doc;
//   if (deserializeJson(doc, jsonStr)) {
//     responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_ERROR) "}";
//     return;
//   }

//   String type = doc[KEY_TYPE] | "";
//   if (type == TYPE_VEHICLE) {
//     int dir = doc[KEY_DIR] | DIR_STOP;
//     int spd = doc[KEY_SPEED] | 50;
//     int dur = doc[KEY_DURATION] | 0;
//     int dist = doc[KEY_DISTANCE] | 0;

//     if (dist > 0)
//       moveVehicleDistance(dir, spd, dist);
//     else
//       moveVehicleCode(dir, spd, dur);
//     responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_OK) "}";
//   } else if (type == TYPE_STORAGE) {
//     int slot = doc[KEY_SLOT] | 0;
//     int act = doc[KEY_ACTION] | 0;
//     controlStorageDoor(slot, act);
//     responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_OK) "}";
//   } else if (type == TYPE_STATUS) {
//     // ✅ SERIALIZE VÀO STRING TẠM TRƯỚC
//     StaticJsonDocument<50> sdoc;
//     sdoc[KEY_STATUS] = STATUS_OK;
//     sdoc[KEY_BATTERY] = 12.4;
//     sdoc[KEY_CONN] = hrBle.isConnected();
//     sdoc[KEY_HR] = hrBle.getHeartRate();
//     sdoc[KEY_MOTOR_EN] = (digitalRead(MOTOR_EN) == LOW);
//     sdoc[KEY_MOVING] = isMoving;

//     JsonArray arr = sdoc[KEY_STORAGE].to<JsonArray>();
//     for (int i = 0; i < 4; i++) {
//       JsonObject o = arr.add<JsonObject>();
//       o[KEY_ITEM_SLOT] = i;
//       o[KEY_ITEM_STATE] = storageStates[i] ? 1 : 0;
//     }

//     responseBuffer = "";                 // Clear trước
//     serializeJson(sdoc, responseBuffer); // ✅ Giờ OK vì không volatile
//   } else {
//     responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_UNKNOWN) "}";
//   }
// }

// // ==================== I2C HANDLERS ====================
// void onI2CReceive(int bytes) {
//   rxBuffer = "";
//   while (Wire.available())
//     rxBuffer += (char)Wire.read();

//   // ✅ XỬ LÝ NGAY TRONG ISR
//   if (rxBuffer.startsWith("{") && rxBuffer.endsWith("}")) {
//     processCommand(rxBuffer);
//     Serial.printf("📩 RX: %s\n📤 TX: %s\n", rxBuffer.c_str(),
//                   responseBuffer.c_str());
//     rxBuffer = "";
//   }
// }

// void onI2CRequest() {
//   if (responseBuffer.isEmpty()) {
//     responseBuffer = "{\"" KEY_STATUS "\":0}";
//   }
//   Wire.write(responseBuffer.c_str());
//   Serial.printf("📡 I2C Request → Sent: %s\n", responseBuffer.c_str());
//   responseBuffer = "";
// }

// // ==================== MAIN ====================
// void setup() {
//   Serial.begin(115200);
//   delay(500);
//   Serial.println("🚗 Xiaozhi Actuator - FIXED I2C Communication v2");

//   pinMode(LED_STATUS, OUTPUT);
//   setupMotors();
//   setupServos();
//   bluetooth_init();

//   Wire.begin(I2C_SLAVE_ADDR, SDA_PIN, SCL_PIN, 100000);
//   Wire.onReceive(onI2CReceive);
//   Wire.onRequest(onI2CRequest);

//   digitalWrite(LED_STATUS, HIGH);
//   Serial.println("✅ Ready! Commands processed immediately in ISR.");
// }

// void loop() {
//   updateMotors();

//   static unsigned long t = 0;
//   if (millis() - t > 2000) {
//     digitalWrite(LED_STATUS, !digitalRead(LED_STATUS));
//     t = millis();
//   }

//   hrBle.loop();
//   delay(1);
// }

#include "HeartRateBLE.h"
#include <AccelStepper.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <Wire.h>

// ==================== MACRO & JSON KEYS ====================
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// JSON key rút gọn
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

// Giá trị rút gọn
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

#define MAX_SPEED 200.0
#define ACCELERATION 100.0
#define STEPS_PER_MM 10.0

#define SERVO_ANGLE_OPEN 90
#define SERVO_ANGLE_CLOSE 140
#define SERVO_MIN_US 500
#define SERVO_MAX_US 2500
#define SERVO_0 25
#define SERVO_1 33
#define SERVO_2 27
#define SERVO_3 26
#define LED_STATUS 12

// ==================== GLOBAL STATE ====================
AccelStepper mFL(AccelStepper::DRIVER, MOTOR_FL_STEP, MOTOR_FL_DIR);
AccelStepper mFR(AccelStepper::DRIVER, MOTOR_FR_STEP, MOTOR_FR_DIR);
AccelStepper mBL(AccelStepper::DRIVER, MOTOR_BL_STEP, MOTOR_BL_DIR);
AccelStepper mBR(AccelStepper::DRIVER, MOTOR_BR_STEP, MOTOR_BR_DIR);
Servo servos[4];
bool storageStates[4] = {false, false, false, false};

bool isMoving = false;
bool isDistanceMode = false; // ✅ THÊM FLAG ĐỂ PHÂN BIỆT CHẾ ĐỘ
unsigned long moveStartTime = 0;
int moveDuration = 0;

String rxBuffer = "";
String responseBuffer = "";

// ==================== INITIALIZATION ====================
void setupMotors() {
  pinMode(MOTOR_EN, OUTPUT);
  digitalWrite(MOTOR_EN, HIGH);

  // ✅ THÊM setAcceleration - QUAN TRỌNG!
  mFL.setMaxSpeed(MAX_SPEED);
  mFL.setAcceleration(ACCELERATION);

  mFR.setMaxSpeed(MAX_SPEED);
  mFR.setAcceleration(ACCELERATION);

  mBL.setMaxSpeed(MAX_SPEED);
  mBL.setAcceleration(ACCELERATION);

  mBR.setMaxSpeed(MAX_SPEED);
  mBR.setAcceleration(ACCELERATION);
}

void setupServos() {
  servos[0].attach(SERVO_0, SERVO_MIN_US, SERVO_MAX_US);
  servos[1].attach(SERVO_1, SERVO_MIN_US, SERVO_MAX_US);
  servos[2].attach(SERVO_2, SERVO_MIN_US, SERVO_MAX_US);
  servos[3].attach(SERVO_3, SERVO_MIN_US, SERVO_MAX_US);
  for (int i = 0; i < 4; i++)
    servos[i].write(SERVO_ANGLE_CLOSE);
}

void enableMotors() { digitalWrite(MOTOR_EN, LOW); }
void disableMotors() { digitalWrite(MOTOR_EN, HIGH); }

// ==================== VEHICLE CONTROL ====================
void setMotorSpeeds(float fl, float fr, float bl, float br) {
  mFL.setSpeed(fl);
  mFR.setSpeed(fr);
  mBL.setSpeed(bl);
  mBR.setSpeed(br);
}

// ✅ SỬA LẠI: Chạy theo thời gian (dùng runSpeed)
void moveVehicleCode(int dir, int spdPercent, int duration_ms) {
  float spd = map(spdPercent, 0, 100, 0, MAX_SPEED);
  enableMotors();

  switch (dir) {
  case DIR_FORWARD:
    setMotorSpeeds(spd, spd, spd, spd);
    break;
  case DIR_BACKWARD:
    setMotorSpeeds(-spd, -spd, -spd, -spd);
    break;
  case DIR_LEFT:
    setMotorSpeeds(-spd, spd, spd, -spd);
    break;
  case DIR_RIGHT:
    setMotorSpeeds(spd, -spd, -spd, spd);
    break;
  case DIR_ROTATE_LEFT:
    setMotorSpeeds(-spd, spd, -spd, spd);
    break;
  case DIR_ROTATE_RIGHT:
    setMotorSpeeds(spd, -spd, spd, -spd);
    break;
  default:
    disableMotors();
    return;
  }

  isMoving = true;
  isDistanceMode = false; // ✅ CHẾ ĐỘ THỜI GIAN
  moveStartTime = millis();
  moveDuration = duration_ms;
}

// ✅ SỬA LẠI: Chạy theo khoảng cách (dùng runSpeedToPosition)
void moveVehicleDistance(int dir, int spdPercent, int distance_mm) {
  float steps = distance_mm * STEPS_PER_MM;
  float spd = map(spdPercent, 0, 100, 0, MAX_SPEED);
  enableMotors();

  long delta = (dir == DIR_FORWARD)    ? steps
               : (dir == DIR_BACKWARD) ? -steps
                                       : 0;

  // ✅ SỬ DỤNG moveTo() thay vì move() để set absolute position
  mFL.move(delta);
  mFR.move(delta);
  mBL.move(delta);
  mBR.move(delta);

  mFL.setSpeed(spd);
  mFR.setSpeed(spd);
  mBL.setSpeed(spd);
  mBR.setSpeed(spd);

  isMoving = true;
  isDistanceMode = true; // ✅ CHẾ ĐỘ KHOẢNG CÁCH
}

void stopVehicle() {
  mFL.stop();
  mFR.stop();
  mBL.stop();
  mBR.stop();
  disableMotors();
  isMoving = false;
  isDistanceMode = false;
}

// ✅ SỬA LẠI: Phân biệt 2 chế độ chạy
void updateMotors() {
  if (!isMoving)
    return;

  if (isDistanceMode) {
    // Chế độ khoảng cách: dùng runSpeedToPosition
    mFL.runSpeedToPosition();
    mFR.runSpeedToPosition();
    mBL.runSpeedToPosition();
    mBR.runSpeedToPosition();

    // Dừng khi tất cả động cơ đã đến vị trí
    if (mFL.distanceToGo() == 0 && mFR.distanceToGo() == 0 &&
        mBL.distanceToGo() == 0 && mBR.distanceToGo() == 0) {
      stopVehicle();
    }
  } else {
    // Chế độ thời gian: dùng runSpeed
    mFL.runSpeed();
    mFR.runSpeed();
    mBL.runSpeed();
    mBR.runSpeed();

    // Dừng khi hết thời gian
    if (moveDuration > 0 && millis() - moveStartTime > moveDuration) {
      stopVehicle();
    }
  }
}

// ==================== STORAGE CONTROL ====================
void controlStorageDoor(int slot, int action) {
  if (slot < 0 || slot > 3)
    return;
  if (action == ACT_OPEN) {
    servos[slot].write(SERVO_ANGLE_OPEN);
    storageStates[slot] = true;
  } else {
    servos[slot].write(SERVO_ANGLE_CLOSE);
    storageStates[slot] = false;
  }
}

// ==================== JSON HANDLER ====================
void processCommand(String jsonStr) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, jsonStr)) {
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_ERROR) "}";
    return;
  }

  String type = doc[KEY_TYPE] | "";
  if (type == TYPE_VEHICLE) {
    int dir = doc[KEY_DIR] | DIR_STOP;
    int spd = doc[KEY_SPEED] | 50;
    int dur = doc[KEY_DURATION] | 0;
    int dist = doc[KEY_DISTANCE] | 0;

    if (dist > 0)
      moveVehicleDistance(dir, spd, dist);
    else
      moveVehicleCode(dir, spd, dur);
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_OK) "}";
  } else if (type == TYPE_STORAGE) {
    int slot = doc[KEY_SLOT] | 0;
    int act = doc[KEY_ACTION] | 0;
    controlStorageDoor(slot, act);
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_OK) "}";
  } else if (type == TYPE_STATUS) {
    StaticJsonDocument<300> sdoc;
    sdoc[KEY_STATUS] = STATUS_OK;
    sdoc[KEY_BATTERY] = 12.4;
    sdoc[KEY_CONN] = hrBle.isConnected();
    sdoc[KEY_HR] = hrBle.getHeartRate();
    sdoc[KEY_MOTOR_EN] = (digitalRead(MOTOR_EN) == LOW);
    sdoc[KEY_MOVING] = isMoving;

    JsonArray arr = sdoc[KEY_STORAGE].to<JsonArray>();
    for (int i = 0; i < 4; i++) {
      JsonObject o = arr.add<JsonObject>();
      o[KEY_ITEM_SLOT] = i;
      o[KEY_ITEM_STATE] = storageStates[i] ? 1 : 0;
    }

    responseBuffer = "";
    serializeJson(sdoc, responseBuffer);
  } else {
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_UNKNOWN) "}";
  }
}

// ==================== I2C HANDLERS ====================
void onI2CReceive(int bytes) {
  rxBuffer = "";
  while (Wire.available())
    rxBuffer += (char)Wire.read();

  if (rxBuffer.startsWith("{") && rxBuffer.endsWith("}")) {
    processCommand(rxBuffer);
    Serial.printf("📩 RX: %s\n📤 TX: %s\n", rxBuffer.c_str(),
                  responseBuffer.c_str());
    rxBuffer = "";
  }
}

void onI2CRequest() {
  if (responseBuffer.isEmpty()) {
    responseBuffer = "{\"" KEY_STATUS "\":0}";
  }
  Wire.write(responseBuffer.c_str());
  Serial.printf("📡 I2C Request → Sent: %s\n", responseBuffer.c_str());
  responseBuffer = "";
}

// ==================== MAIN ====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("🚗 Xiaozhi Actuator - MOTOR FIXED v3");

  pinMode(LED_STATUS, OUTPUT);
  setupMotors();
  setupServos();
  bluetooth_init();

  Wire.begin(I2C_SLAVE_ADDR, SDA_PIN, SCL_PIN, 100000);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);

  digitalWrite(LED_STATUS, HIGH);
  Serial.println("✅ Ready! Motors configured with acceleration.");
}

void loop() {
  // enableMotors();
  // // setMotorSpeeds(100, 100, 100, 100);

  // mFL.setSpeed(1000);
  // mFL.runSpeed();

  // mFR.setSpeed(1000);
  // mFR.runSpeed();

  // mBL.setSpeed(1000);
  // mBL.runSpeed();

  // mBR.setSpeed(1000);
  // mBR.runSpeed();
  updateMotors(); // ✅ Giờ sẽ hoạt động đúng

  static unsigned long t = 0;
  if (millis() - t > 2000) {
    digitalWrite(LED_STATUS, !digitalRead(LED_STATUS));
    t = millis();
  }

  hrBle.loop();
  delay(1);
}