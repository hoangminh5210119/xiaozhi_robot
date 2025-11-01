
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
bool isDistanceMode = false;
unsigned long moveStartTime = 0;
unsigned long moveDuration = 0;

String rxBuffer = "";
String responseBuffer = "";
bool responseReady = false; // ✅ THÊM FLAG ĐỂ KIỂM TRA

// ==================== MOTOR CONTROL ====================
void enableMotors() { digitalWrite(MOTOR_EN, LOW); }

void disableMotors() { digitalWrite(MOTOR_EN, HIGH); }

void setupMotors() {
  pinMode(MOTOR_EN, OUTPUT);
  disableMotors();

  mFL.setMaxSpeed(MAX_SPEED);
  mFR.setMaxSpeed(MAX_SPEED);
  mBL.setMaxSpeed(MAX_SPEED);
  mBR.setMaxSpeed(MAX_SPEED);
}

void setMotorSpeeds(float fl, float fr, float bl, float br) {
  mFL.setSpeed(fl);
  mFR.setSpeed(fr);
  mBL.setSpeed(bl);
  mBR.setSpeed(br);
}

void stopVehicle() {
  setMotorSpeeds(0, 0, 0, 0);

  mFL.setCurrentPosition(0);
  mFR.setCurrentPosition(0);
  mBL.setCurrentPosition(0);
  mBR.setCurrentPosition(0);

  disableMotors();
  isMoving = false;
  isDistanceMode = false;

  Serial.println("🛑 Vehicle stopped");
}

void moveVehicleByTime(int dir, int speedPercent, int duration_ms) {
  if (speedPercent <= 0 || duration_ms <= 0) {
    Serial.println("⚠️ Invalid time parameters");
    return;
  }

  float speed = map(speedPercent, 0, 100, 0, MAX_SPEED);
  enableMotors();

  switch (dir) {
  case DIR_FORWARD:
    setMotorSpeeds(speed, speed, speed, speed);
    Serial.printf("⬆️ Forward | Speed: %.0f | Time: %dms\n", speed, duration_ms);
    break;
  case DIR_BACKWARD:
    setMotorSpeeds(-speed, -speed, -speed, -speed);
    Serial.printf("⬇️ Backward | Speed: %.0f | Time: %dms\n", speed,
                  duration_ms);
    break;
  case DIR_LEFT:
    setMotorSpeeds(-speed, speed, speed, -speed);
    Serial.printf("⬅️ Left | Speed: %.0f | Time: %dms\n", speed, duration_ms);
    break;
  case DIR_RIGHT:
    setMotorSpeeds(speed, -speed, -speed, speed);
    Serial.printf("➡️ Right | Speed: %.0f | Time: %dms\n", speed, duration_ms);
    break;
  case DIR_ROTATE_LEFT:
    setMotorSpeeds(-speed, speed, -speed, speed);
    Serial.printf("↶ Rotate Left | Speed: %.0f | Time: %dms\n", speed,
                  duration_ms);
    break;
  case DIR_ROTATE_RIGHT:
    setMotorSpeeds(speed, -speed, speed, -speed);
    Serial.printf("↷ Rotate Right | Speed: %.0f | Time: %dms\n", speed,
                  duration_ms);
    break;
  default:
    Serial.println("⚠️ Invalid direction");
    disableMotors();
    return;
  }

  isMoving = true;
  isDistanceMode = false;
  moveStartTime = millis();
  moveDuration = duration_ms;
}

void moveVehicleByDistance(int dir, int speedPercent, int distance_mm) {
  if (speedPercent <= 0 || distance_mm <= 0) {
    Serial.println("⚠️ Invalid distance parameters");
    return;
  }

  float speed = map(speedPercent, 0, 100, 0, MAX_SPEED);
  long targetSteps = distance_mm * STEPS_PER_MM;

  enableMotors();

  if (dir == DIR_FORWARD) {
    mFL.move(targetSteps);
    mFR.move(targetSteps);
    mBL.move(targetSteps);
    mBR.move(targetSteps);
    setMotorSpeeds(speed, speed, speed, speed);
    Serial.printf("⬆️ Forward | Speed: %.0f | Distance: %dmm (%ld steps)\n",
                  speed, distance_mm, targetSteps);
  } else if (dir == DIR_BACKWARD) {
    mFL.move(-targetSteps);
    mFR.move(-targetSteps);
    mBL.move(-targetSteps);
    mBR.move(-targetSteps);
    setMotorSpeeds(-speed, -speed, -speed, -speed);
    Serial.printf("⬇️ Backward | Speed: %.0f | Distance: %dmm (%ld steps)\n",
                  speed, distance_mm, targetSteps);
  } else {
    Serial.println("⚠️ Distance mode only supports FORWARD/BACKWARD");
    disableMotors();
    return;
  }

  isMoving = true;
  isDistanceMode = true;
}

void updateMotors() {
  if (!isMoving)
    return;

  mFL.runSpeed();
  mFR.runSpeed();
  mBL.runSpeed();
  mBR.runSpeed();

  if (isDistanceMode) {
    if (mFL.distanceToGo() == 0 && mFR.distanceToGo() == 0 &&
        mBL.distanceToGo() == 0 && mBR.distanceToGo() == 0) {
      stopVehicle();
    }
  } else {
    if (millis() - moveStartTime >= moveDuration) {
      stopVehicle();
    }
  }
}

// ==================== SERVO CONTROL ====================
void setupServos() {
  servos[0].attach(SERVO_0, SERVO_MIN_US, SERVO_MAX_US);
  servos[1].attach(SERVO_1, SERVO_MIN_US, SERVO_MAX_US);
  servos[2].attach(SERVO_2, SERVO_MIN_US, SERVO_MAX_US);
  servos[3].attach(SERVO_3, SERVO_MIN_US, SERVO_MAX_US);

  for (int i = 0; i < 4; i++) {
    servos[i].write(SERVO_ANGLE_CLOSE);
    storageStates[i] = false;
  }

  Serial.println("🚪 All storage doors closed");
}

void controlStorageDoor(int slot, int action) {
  if (slot < 0 || slot > 3) {
    Serial.printf("⚠️ Invalid slot: %d\n", slot);
    return;
  }

  if (action == ACT_OPEN) {
    servos[slot].write(SERVO_ANGLE_OPEN);
    storageStates[slot] = true;
    Serial.printf("🚪 Storage[%d] OPENED\n", slot);
  } else {
    servos[slot].write(SERVO_ANGLE_CLOSE);
    storageStates[slot] = false;
    Serial.printf("🚪 Storage[%d] CLOSED\n", slot);
  }
}

// ==================== JSON COMMAND PROCESSING ====================
void processCommand(String jsonStr) {
  StaticJsonDocument<256> doc;

  DeserializationError error = deserializeJson(doc, jsonStr);
  if (error) {
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_ERROR) "}";
    responseReady = true; // ✅ SET FLAG
    Serial.printf("❌ JSON parse error: %s\n", error.c_str());
    return;
  }

  String type = doc[KEY_TYPE] | "";
  Serial.printf("🔍 Type detected: '%s'\n", type.c_str()); // ✅ DEBUG

  // ===== VEHICLE CONTROL =====
  if (type == TYPE_VEHICLE) {
    int dir = doc[KEY_DIR] | DIR_STOP;
    int speed = doc[KEY_SPEED] | 50;
    int duration = doc[KEY_DURATION] | 0;
    int distance = doc[KEY_DISTANCE] | 0;

    if (distance > 0) {
      moveVehicleByDistance(dir, speed, distance);
    } else if (duration > 0) {
      moveVehicleByTime(dir, speed, duration);
    } else {
      stopVehicle();
    }

    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_OK) "}";
    responseReady = true; // ✅ SET FLAG
  }

  // ===== STORAGE CONTROL =====
  else if (type == TYPE_STORAGE) {
    int slot = doc[KEY_SLOT] | 0;
    int action = doc[KEY_ACTION] | ACT_CLOSE;
    controlStorageDoor(slot, action);
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_OK) "}";
    responseReady = true; // ✅ SET FLAG
  }

  // ===== STATUS QUERY =====
  // else if (type == TYPE_STATUS) {
  //   Serial.println("📊 Building status response...");  // ✅ DEBUG

  //   StaticJsonDocument<300> statusDoc;
  //   statusDoc[KEY_STATUS] = STATUS_OK;
  //   statusDoc[KEY_BATTERY] = 12.4;
  //   statusDoc[KEY_CONN] = hrBle.isConnected();
  //   statusDoc[KEY_HR] = hrBle.getHeartRate();
  //   statusDoc[KEY_MOTOR_EN] = (digitalRead(MOTOR_EN) == LOW);
  //   statusDoc[KEY_MOVING] = isMoving;

  //   JsonArray storageArray = statusDoc[KEY_STORAGE].to<JsonArray>();
  //   for (int i = 0; i < 4; i++) {
  //     JsonObject item = storageArray.add<JsonObject>();
  //     item[KEY_ITEM_SLOT] = i;
  //     item[KEY_ITEM_STATE] = storageStates[i] ? 1 : 0;
  //   }

  //   responseBuffer = "";
  //   serializeJson(statusDoc, responseBuffer);
  //   responseReady = true;  // ✅ SET FLAG
  //   Serial.printf("✅ Status built: %s\n", responseBuffer.c_str());  // ✅
  //   DEBUG
  // }

  // ===== STATUS QUERY =====
  else if (type == TYPE_STATUS) {
    Serial.println("📊 Building status response...");

    StaticJsonDocument<300> statusDoc;
    statusDoc[KEY_STATUS] = STATUS_OK;
    statusDoc[KEY_BATTERY] = 12.4;

    // ✅ DÙNG SỐ THAY VÌ BOOLEAN → Giảm size
    statusDoc[KEY_CONN] = hrBle.isConnected() ? 1 : 0; // true → 1
    statusDoc[KEY_HR] = hrBle.getHeartRate();
    statusDoc[KEY_MOTOR_EN] =
        (digitalRead(MOTOR_EN) == LOW) ? 1 : 0; // false → 0
    statusDoc[KEY_MOVING] = isMoving ? 1 : 0;

    JsonArray storageArray = statusDoc[KEY_STORAGE].to<JsonArray>();
    for (int i = 0; i < 4; i++) {
      JsonObject item = storageArray.add<JsonObject>();
      item[KEY_ITEM_SLOT] = i;
      item[KEY_ITEM_STATE] = storageStates[i] ? 1 : 0;
    }

    responseBuffer = "";
    serializeJson(statusDoc, responseBuffer); // Không dùng prettyPrint
    responseReady = true;

    Serial.printf("✅ Status built (%d bytes): %s\n", responseBuffer.length(),
                  responseBuffer.c_str());
  }

  // ===== UNKNOWN COMMAND =====
  else {
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_UNKNOWN) "}";
    responseReady = true; // ✅ SET FLAG
    Serial.printf("⚠️ Unknown type: '%s'\n", type.c_str());
  }
}

// ==================== I2C COMMUNICATION ====================
void onI2CReceive(int bytes) {
  rxBuffer = "";
  while (Wire.available()) {
    rxBuffer += (char)Wire.read();
  }

  Serial.printf("📩 RX (%d bytes): '%s'\n", bytes,
                rxBuffer.c_str()); // ✅ THÊM LENGTH

  if (rxBuffer.startsWith("{") && rxBuffer.endsWith("}")) {
    responseReady = false; // ✅ RESET FLAG
    processCommand(rxBuffer);
    Serial.printf("📤 Response ready: %s\n", responseBuffer.c_str());
  } else {
    Serial.printf("⚠️ Invalid JSON format: '%s'\n", rxBuffer.c_str());
    responseBuffer = "{\"" KEY_STATUS "\":" STR(STATUS_ERROR) "}";
    responseReady = true;
  }

  rxBuffer = "";
}
void onI2CRequest() {
  if (!responseReady || responseBuffer.isEmpty()) {
    Serial.println("⚠️ Response not ready, sending default");
    responseBuffer = "{\"" KEY_STATUS "\":0}";
  }

  size_t written =
      Wire.write(responseBuffer.c_str()); // ✅ Lấy số byte thực tế gửi

  Serial.printf("📡 I2C Request → Buffer: %d bytes, Written: %d bytes\n",
                responseBuffer.length(), written);

  if (written < responseBuffer.length()) {
    Serial.printf("⚠️ WARNING: Buffer overflow! Only sent %d/%d bytes\n",
                  written, responseBuffer.length());
  }

  responseReady = false;
}
// void onI2CRequest() {

//   // delay(100);
//   // ✅ KIỂM TRA FLAG TRƯỚC
//   if (!responseReady || responseBuffer.isEmpty()) {
//     Serial.println("⚠️ Response not ready, sending default");
//     responseBuffer = "{\"" KEY_STATUS "\":0}";
//   }
//   // delay(100);

//   Wire.write(responseBuffer.c_str());
//   Serial.printf("📡 I2C Request → Sent (%d bytes): %s\n",
//                 responseBuffer.length(), responseBuffer.c_str());

//   // ✅ KHÔNG CLEAR NGAY, CHỜ REQUEST XONG
//   // responseBuffer = "";
//   responseReady = false;
// }

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

// ==================== MAIN SETUP & LOOP ====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  🚗 Xiaozhi Actuator - v4.1 DEBUG  ║");
  Serial.println("╚════════════════════════════════════════╝");

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
  Serial.println("🎯 Waiting for commands...\n");
}

void loop() {
  // updateMotors();
  // hrBle.loop();

  // static unsigned long lastBlink = 0;
  // if (millis() - lastBlink > 2000) {
  //   digitalWrite(LED_STATUS, !digitalRead(LED_STATUS));
  //   lastBlink = millis();
  // }

  // delay(1);
}