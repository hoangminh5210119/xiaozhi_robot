#define __version__ "1.24"

#define TINY_GSM_MODEM_SIM7600

#include "FS.h"
#include <WiFi.h>

#include "I2Cdev.h"
#include "MPU6050.h"
#include "OneButton.h"
#include <Arduino.h>

#include <TinyGPSPlus.h>
#include <Wire.h>

#include "ESP32Servo.h"

#include <AsyncTCP.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <Wire.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <SPI.h>
#include <TinyGsmClient.h>
#include <Wire.h>

#include <Arduino.h>
#include <BLEAddress.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>

#include <PubSubClient.h>
#include <TinyGsmClient.h>

#include <Update.h>

#include "ArduinoJson.h"

#define debug_println(x) Serial.println(x)
#define debug_print(x) Serial.print(x)

#define SerialMon Serial

// MQTT details
const char *broker = "broker.emqx.io";

String config_topic = "";
String info_topic = "";
String update_topic = "";
String debug_topic = "";

const char apn[] = "v-internet";
const char gprsUser[] = "";
const char gprsPass[] = "";

String my_ota_server = "";
String my_ota_file = "";
int my_ota_port = 80;

int count_network_false = 0;

bool isOTA = false;

void send_sms(String phone_number, String message);

// volatile bool fallDetected = false;
volatile bool mpu6050InterruptDetected = false;
// const unsigned long timeCheckFall = 5000; // 5s chờ phản hồi từ Serial

// MPU6050
#define MPU6050_ADDR 0x68
#define PWR_MGMT_1 0x6B
#define ACCEL_CONFIG 0x1C
#define MOT_THR 0x1F
#define MOT_DUR 0x20
#define INT_ENABLE 0x38
#define INT_STATUS 0x3A
#define INTERRUPT_PIN 6

// ------------ Biến trạng thái ------------
enum State {
  STATE_IDLE,
  STATE_IMPACT_FOUND,
  STATE_CHECK_POST,
  STATE_FALL_WAIT
};

// Thời gian kiểm tra ngã
unsigned long lastTimeCheckFall = 0;
unsigned long timeCheckFall = 20000;

State fallState = STATE_IDLE;
bool fallDetected = false;    // Cờ đánh dấu "xác định đã ngã"
unsigned long impactTime = 0; // Lưu thời điểm phát hiện spike
unsigned long fallTime = 0;   // Lưu thời điểm vừa kết luận ngã

MPU6050 mpu;

struct LocalTime {
  int hour;
  int minute;
  int second;
};

static BLEUUID batteryServiceUUID((uint16_t)0x180F);
static BLEUUID batteryLevelCharUUID((uint16_t)0x2A19);

static BLEUUID heartRateServiceUUID((uint16_t)0x180D);
static BLEUUID heartRateMeasurementCharUUID((uint16_t)0x2A37);

static const char *TARGET_NAME = "My BLE Device";

// Biến toàn cục, giữ đối tượng quét
static BLEAdvertisedDevice deviceToConnect;
static BLEClient *pClient = nullptr;
static BLERemoteCharacteristic *pBatteryChar = nullptr;
static BLERemoteCharacteristic *pHeartRateChar = nullptr;

static bool connected = false;

// // Heart rate
int beatAvg = -1;

static void
heartRateNotifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic,
                        uint8_t *pData, size_t length, bool isNotify) {
  // //Serial.println("[heartRateNotifyCallback] Gọi khi có notify Heart Rate");
  if (length < 2) {
    // //Serial.println("  Dữ liệu nhịp tim quá ngắn, bỏ qua");
    return;
  }
  uint8_t flags = pData[0];
  uint8_t heartRateValue = pData[1];
  beatAvg = heartRateValue;
  // Serial.printf("  Heart Rate: %d bpm (Flags=0x%02X)\n", heartRateValue,
  // flags);
}

// Callback BLEClient (connect/disconnect)
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) override {
    connected = true;
    // Serial.println("[MyClientCallback] onConnect => connected = true");
  }
  void onDisconnect(BLEClient *pclient) override {
    connected = false;
    // Serial.println("[MyClientCallback] onDisconnect => connected = false");
  }
};

// Kết nối, đọc Battery Level, đăng ký notify nhịp tim
bool connectToDevice() {
  // Serial.println("[connectToDevice] Bắt đầu kết nối...");

  // Tạo BLEClient
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  // Lấy BLEAddress (là địa chỉ MAC 6 byte)
  // BLEAddress address = BLEAddress("c2:f4:3f:a0:34:63"); // cũ
  BLEAddress address = BLEAddress("e2:03:b2:52:c1:3f"); // mới

  // Nhiều thiết bị BLE y tế dùng random address => BLE_ADDR_TYPE_RANDOM
  bool ok =
      pClient->connect(address, (esp_ble_addr_type_t)BLE_ADDR_TYPE_RANDOM);
  // Nếu thiết bị dùng public address, đổi BLE_ADDR_TYPE_PUBLIC

  if (!ok) {
    // Serial.println("[connectToDevice] pClient->connect(...) => FAILED");
    return false;
  }
  // Serial.println("[connectToDevice] Kết nối thành công => Tìm service & "
  //   "characteristic...");

  // ====== Tìm Battery Service (0x180F) ======
  BLERemoteService *pBatteryService = pClient->getService(batteryServiceUUID);
  if (pBatteryService) {
    // Serial.println("  Found Battery Service => Tìm Battery Level Char...");
    pBatteryChar = pBatteryService->getCharacteristic(batteryLevelCharUUID);
    if (pBatteryChar) {
      // Serial.println("    Đọc Battery Level...");
      // String batData = pBatteryChar->readValue();
      // if (batData != "") {
      //   uint8_t batteryLevel = (uint8_t)batData[0];
      //   // Serial.printf("    Battery Level: %d%%\n", batteryLevel);
      // } else {
      //   // Serial.println("    Không đọc được Battery Level");
      // }
    } else {
      // Serial.println("    Battery Level Char 0x2A19 không tìm thấy");
    }
  } else {
    // Serial.println("  Battery Service (0x180F) không tìm thấy");
  }

  // ====== Tìm Heart Rate Service (0x180D) ======
  BLERemoteService *pHRService = pClient->getService(heartRateServiceUUID);
  if (pHRService) {
    // Serial.println(
    //    "  Found Heart Rate Service => Tìm Heart Rate Char (0x2A37)...");
    pHeartRateChar =
        pHRService->getCharacteristic(heartRateMeasurementCharUUID);
    if (pHeartRateChar) {
      if (pHeartRateChar->canNotify()) {
        pHeartRateChar->registerForNotify(heartRateNotifyCallback);
      } else {
      }
    } else {
    }
  } else {
  }
  return true;
}

#define SCREEN_ADDRESS 0x3C // 0x3D cho 128x64, 0x3C cho 128x32
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SH1106G display =
    Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int servoPin = 2;
const int button_pin = 4;
const int led_pin = 1;
const int rx_gps_pin = 3;
const int tx_gps_pin = -1;

Servo myservo;

// Nút bấm
OneButton button(button_pin, true);

// Cấu trúc dữ liệu người dùng
struct UserData {
  String phone_number;
  int heartrate_threshold;
  double latitude;
  double longitude;
};
UserData user_data;

#define turn_off_servo_degree 100
#define turn_on_servo_degree 25

bool _isOn = false;

#define turn_off_servo()                                                       \
  _isOn = false;                                                               \
  myservo.write(turn_off_servo_degree);

#define turn_on_servo()                                                        \
  _isOn = true;                                                                \
  myservo.write(turn_on_servo_degree)

#include <ArduinoHttpClient.h>
#include <HardwareSerial.h>

HardwareSerial MySerial(1);

#define sim_serial MySerial
TinyGPSPlus gps;

TinyGsm modem(sim_serial);
TinyGsmClient client(modem);
PubSubClient mqtt(client);

SoftwareSerial gps_serial(rx_gps_pin, tx_gps_pin); // RX, TX

// Thời gian hiển thị
unsigned long oldtime_display = 0;
unsigned long time_dislay = 1000;
bool force_display = false;

bool isSOS = false;

// Vị trí
double latitude = 0.0, longitude = 0.0;
String phone_number = "";
String sms_message = "";
bool isDebug = false;

int heartrate_threshold = 100;
String device_status = "";
bool isSaveNumber = false;
bool isServoOn = false;

bool force_call = false;
bool fall_call = false;
// Web Server
// AsyncWebServer server(80);

void mqttCallback(char *topic, byte *payload, unsigned int len) {
  SerialMon.print("Message arrived [");
  SerialMon.print(topic);
  SerialMon.print("]: ");
  SerialMon.write(payload, len);
  SerialMon.println();
  String payload_str = String((char *)payload);
  //  check if topic config_topic
  // data {"phone":"0354545185","threshold":"146"}
  if (String(topic) == config_topic) {
    SerialMon.println("Payload: " + payload_str);
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload_str);
    if (error) {
      SerialMon.print(F("deserializeJson() failed: "));
      SerialMon.println(error.f_str());
      return;
    }
    if (doc.containsKey("debug")) {
      isDebug = doc["debug"].as<bool>();
    }
    // check if json has phone and threshold
    if (!doc.containsKey("phone") || !doc.containsKey("threshold")) {
      SerialMon.println("Invalid JSON format");
      return;
    }
    phone_number = doc["phone"].as<String>();
    heartrate_threshold = doc["threshold"].as<int>();
    SerialMon.print("Phone number: ");
    SerialMon.println(phone_number);
    SerialMon.print("Threshold: ");
    SerialMon.println(heartrate_threshold);

    isSaveNumber = true;
  } else if (String(topic) == String(update_topic)) {
    SerialMon.println("Applying OTA config...");
    SerialMon.println("Payload: " + payload_str);
    applyOtaConfig(payload_str);
    isOTA = true;
  }
}

// parse & apply JSON like {"server":"...","port":123,"path":"/x.bin"}
void applyOtaConfig(const String &jsonLine) {
  DynamicJsonDocument doc(1024);
  auto err = deserializeJson(doc, jsonLine);
  if (err) {
    SerialMon.print("JSON parse error: ");
    SerialMon.println(err.c_str());
    return;
  }
  if (doc.containsKey("server"))
    my_ota_server = doc["server"].as<const char *>();
  if (doc.containsKey("port"))
    my_ota_port = doc["port"].as<int>();
  if (doc.containsKey("path"))
    my_ota_file = doc["path"].as<const char *>();
  SerialMon.println("OTA config updated:");
  SerialMon.print("  server: ");
  SerialMon.println(my_ota_server);
  SerialMon.print("  port:   ");
  SerialMon.println(my_ota_port);
  SerialMon.print("  path:   ");
  SerialMon.println(my_ota_file);
}

boolean mqttConnect() {

  SerialMon.print("Connecting to ");
  SerialMon.print(broker);

  String random_clientId = "GsmClientTest";
  random_clientId += String(random(0xffff), HEX);
  SerialMon.print("Client ID: ");
  SerialMon.println(random_clientId);

  boolean status = mqtt.connect(random_clientId.c_str());

  if (status == false) {
    SerialMon.println(" fail");
    count_network_false++;
    SerialMon.print(" count_network_false: ");
    SerialMon.println(count_network_false);
    return false;
  }
  SerialMon.println(" success");
  mqtt.subscribe(config_topic.c_str());
  mqtt.subscribe(update_topic.c_str());

  SerialMon.print("Subscribed to topics: ");
  SerialMon.print(config_topic);
  SerialMon.print(", ");
  SerialMon.println(update_topic);

  SerialMon.println("MQTT connected");
  // mqtt.publish(info_topic.c_str(), "Hello from ESP32");
  return mqtt.connected();
}

uint32_t lastReconnectAttempt = 0;
bool modem_mqtt_handler() {
  // Make sure we're still registered on the network
  if (!modem.isNetworkConnected()) {
    SerialMon.println("Network disconnected");
    if (!modem.waitForNetwork(10000L, true)) {
      SerialMon.println(" fail");
      delay(5000);
      return false;
    }
    if (modem.isNetworkConnected()) {
      SerialMon.println("Network re-connected");
    }

    // and make sure GPRS/EPS is still connected
    if (!modem.isGprsConnected()) {
      SerialMon.println("GPRS disconnected!");
      SerialMon.print(F("Connecting to "));
      SerialMon.print(apn);
      if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
        SerialMon.println(" fail");
        delay(5000);
        return false;
      }
      if (modem.isGprsConnected()) {
        SerialMon.println("GPRS reconnected");
      }
    }
  }

  if (!mqtt.connected()) {
    uint32_t t = millis();
    if (t - lastReconnectAttempt > 5000L) {
      lastReconnectAttempt = t;
      if (mqttConnect()) {
        lastReconnectAttempt = 0;
      }
    }

    if (count_network_false > 5) {
      setup_modem();
      count_network_false = 0;
    }
    delay(100);
    return false;
  }

  mqtt.loop();
  return true;
}

String get_mac_address() {
  String mac = "";
  String macAddress = WiFi.macAddress();
  for (int i = 0; i < macAddress.length(); i++) {
    if (macAddress[i] != ':') {
      mac += macAddress[i];
    }
  }
  mac.toUpperCase();

  // get last 6 characters
  String last6 = mac.substring(mac.length() - 6);

  return last6;
}

void create_topic() {
  String mac = get_mac_address();
  config_topic = "device/" + mac + "/config";
  info_topic = "device/" + mac + "/data";
  update_topic = "device/" + mac + "/update";
  debug_topic = "device/" + mac + "/debug";

  SerialMon.print("Config topic: ");
  SerialMon.println(config_topic);
  SerialMon.print("Info topic: ");
  SerialMon.println(info_topic);
  SerialMon.print("Update topic: ");
  SerialMon.println(update_topic);
  SerialMon.print("debug topic: ");
  SerialMon.println(debug_topic);
}

const int TIMEZONE_OFFSET = 7; // UTC+7
LocalTime getLocalTimeFunc() {
  LocalTime localTime;

  // Lấy thời gian UTC từ GPS
  int utcHour = gps.time.hour();
  int utcMinute = gps.time.minute();
  int utcSecond = gps.time.second();

  // Tính toán thời gian địa phương
  int localHour = utcHour + TIMEZONE_OFFSET;

  if (localHour >= 24) {
    localHour -= 24;
  }
  if (localHour < 0) {
    localHour += 24;
  }

  localTime.hour = localHour;
  localTime.minute = utcMinute;
  localTime.second = utcSecond;

  return localTime;
}

// Hàm xử lý yêu cầu không tìm thấy
// void notFound(AsyncWebServerRequest *request) {
//   request->send(404, "text/plain", "Not found");
// }

// Hàm thêm trạng thái
void append_status(String new_status) {
  // device_status += new_status;
}

void append_status_ln(String new_status) {
  // device_status += new_status + "\n";
  // // Kiểm tra nếu trạng thái quá dài thì loại bỏ dòng đầu tiên
  // if (device_status.length() > 100) {
  //   int index = device_status.indexOf("\n");
  //   device_status = device_status.substring(index + 1);
  // }
}

// Hàm gọi điện
void call_phone(String phone_number) { modem.callNumber(phone_number); }

void interrupt_call() { modem.callHangup(); }

// Hàm lưu dữ liệu vào EEPROM
void saveDataToEEPROM() {
  if (isSaveNumber) {
    int addr = 0;
    user_data.phone_number = phone_number;
    user_data.heartrate_threshold = heartrate_threshold;
    user_data.latitude = latitude;
    user_data.longitude = longitude;
    EEPROM.put(addr, user_data);
    EEPROM.commit();
    isSaveNumber = false;
  }
}

// Hàm đọc dữ liệu từ EEPROM
void readDataFromEEPROM() {
  int addr = 0;
  EEPROM.get(addr, user_data);
  phone_number = user_data.phone_number;
  heartrate_threshold = user_data.heartrate_threshold;
  latitude = user_data.latitude;
  longitude = user_data.longitude;

  Serial.print("phone_number: ");
  Serial.println(phone_number);
  Serial.print("latitude: ");
  Serial.println(latitude);
  Serial.print("longitude: ");
  Serial.println(longitude);
}

// Hàm kiểm tra ngã và gọi điện nếu phát hiện

void checkCallFall() {
  if (fallDetected) {
    int countdown = (timeCheckFall) / 1000 - ((millis() - fallTime) / 1000);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("     PHAT HIEN NGA");
    display.setCursor(0, 20);
    display.setTextSize(1);
    display.println("    NHAN NUT DE HUY");
    display.setCursor(0, 40);
    display.setTextSize(1);
    display.print("THOI GIAN CON LAI: ");
    display.println(countdown);
    display.display();
  }
}

unsigned long oldtime_call = 0;
unsigned long time_call = 10000;
bool isCall = false;
bool isSendSMS = false;

unsigned long oldtime_check_heartrate = 0;
unsigned long time_check_heartrate = 5000;
// Hàm kiểm tra nhịp tim cao
void checkHighHeartrate() {
  if (beatAvg > heartrate_threshold && beatAvg > 0) {
    if (millis() - oldtime_check_heartrate > time_check_heartrate) {
      isCall = true;
      turn_on_servo();
      isServoOn = true;
      oldtime_check_heartrate = millis();
    }
  } else {
    oldtime_check_heartrate = millis();
  }
}

unsigned long oldtime_sos = 0;
unsigned long time_sos = 200;
void led_blink_sos() {
  if (isSOS) {
    if (millis() - oldtime_sos > time_sos) {
      digitalWrite(led_pin, !digitalRead(led_pin));
      oldtime_sos = millis();
    }
  } else {
    digitalWrite(led_pin, LOW);
  }
}

void setup_bluetooth() {
  BLEDevice::init("ESP32C3_Client222");
  connectToDevice();
}

// task restart handler
TaskHandle_t TaskRestartHandler = NULL;
TaskHandle_t TaskCallHandler = NULL;
TaskHandle_t TaskMPUHandler = NULL;

void TaskMPU6050(void *pvParameters) {
  while (1) {
    read_mpu();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void TaskButton(void *pvParameters) {
  while (1) {
    button.tick();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void MainTask(void *pvParameters) {
  while (1) {
    checkCallFall();
    checkHighHeartrate();
    // display_lcd();
    read_mpu();
    led_blink_sos();
    if (isServoOn) {
      turn_on_servo();
    } else {
      turn_off_servo();
    }
    button.tick();

    // SerialMon.println("MainTask");
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void TaskCall(void *pvParameters) {
  // int cnt = 0;
  while (1) {
    // display_lcd();
    led_blink_sos();
    if (isServoOn) {
      turn_on_servo();
    } else {
      turn_off_servo();
    }
    button.tick();
    // SerialMon.println("TaskCall");
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void TaskRestart(void *pvParameters) {
  while (1) {
    setup_bluetooth();
    vTaskDelete(NULL);
  }
}

void setup_modem() {
  SerialMon.println("Initializing modem...");

  // modem.init();

  String modemInfo = modem.getModemInfo();
  SerialMon.print("Modem Info: ");
  SerialMon.println(modemInfo);

  SerialMon.print("Waiting for network...");
  if (!modem.waitForNetwork()) {
    SerialMon.println(" fail");
    return;
  }

  SerialMon.println(" success");

  if (modem.isNetworkConnected()) {
    SerialMon.println("Network connected");
  }

  // GPRS connection parameters are usually set after network registration
  SerialMon.print(F("Connecting to "));
  SerialMon.print(apn);
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    SerialMon.println(" fail");
    return;
  }
  SerialMon.println(" success");

  if (modem.isGprsConnected()) {
    SerialMon.println("GPRS connected");
  }

  // MQTT Broker setup
  mqtt.setServer(broker, 1883);
  mqtt.setCallback(mqttCallback);
}

int read_task = 0;
unsigned long lasttime_change_function = 0;
unsigned long time_change_function = 10000;

int cnt = 0;

unsigned long lasttime_send_mqtt = 0;
unsigned long time_send_mqtt = 10000;

void send_mqtt() {
  if (mqtt.connected() && millis() - lasttime_send_mqtt > time_send_mqtt ||
      fallDetected) {
    lasttime_send_mqtt = millis();

    String mpu_state = fallDetected ? "Té ngã" : "Bình thường";
    String json = "{";
    json += "\"phone\":\"" + phone_number + "\",";
    json += "\"lat\":" + String(latitude, 6) + ",";
    json += "\"lon\":" + String(longitude, 6) + ",";
    json += "\"threshold\":" + String(heartrate_threshold) + ",";
    json += "\"heartRate\":" + String(beatAvg) + ",";
    json += "\"isDebug\":" + String(isDebug) + ",";
    json += "\"mpu\":\"" + mpu_state + "\"";
    json += "}";

    SerialMon.print("topic: ");
    SerialMon.println(info_topic);
    SerialMon.println("Sending MQTT: " + json);
    mqtt.publish(info_topic.c_str(), json.c_str());
  }
}

void otaUpdate() {
  // Khởi tạo HttpClient với server và port OTA
  HttpClient http(client, my_ota_server.c_str(), my_ota_port);

  // Bắt đầu gửi yêu cầu GET tới file firmware
  http.beginRequest();
  int err = http.get(my_ota_file.c_str());
  if (err != 0) {
    SerialMon.println(F("Failed to connect"));
    return;
  }

  // Kết thúc yêu cầu và đợi nhận header phản hồi
  http.endRequest();
  int status = http.responseStatusCode();
  SerialMon.print(F("Response status code: "));
  SerialMon.println(status);

  if (status < 0) {
    SerialMon.println("Error: " + String(status));
    return;
  }

  // Lấy kích thước file firmware từ header
  int contentLength = http.contentLength();
  SerialMon.print(F("Firmware size: "));
  SerialMon.println(contentLength);

  if (contentLength <= 0) {
    SerialMon.println(F("Invalid content length."));
    return;
  }

  // Khởi tạo quá trình OTA với kích thước firmware
  if (!Update.begin(contentLength)) {
    SerialMon.println(F("Not enough space to begin OTA update."));
    return;
  }

  SerialMon.println(F("Begin OTA update..."));

  // Sử dụng client (là TinyGsmClient, kế thừa từ Stream) để đọc dữ liệu
  // firmware
  size_t written = Update.writeStream(client);
  SerialMon.print(F("Written bytes: "));
  SerialMon.println(written);

  if (written != contentLength) {
    SerialMon.println(F("Firmware update incomplete."));
  }

  // Kết thúc quá trình cập nhật và kiểm tra kết quả
  if (Update.end()) {
    if (Update.isFinished()) {
      SerialMon.println(F("OTA update finished successfully. Rebooting..."));
      ESP.restart();
    } else {
      SerialMon.println(F("OTA update not finished properly."));
    }
  } else {
    SerialMon.print(F("Error during OTA update: "));
    SerialMon.println(Update.getError());
  }
}

void printProgress(size_t progress, const size_t &size) {
  static int last_progress = -1;
  if (size > 0) {
    progress = (progress * 100) / size;
    progress = (progress > 100 ? 100 : progress); // 0-100
    if (progress != last_progress) {
      Serial.printf("Progress: %d%%\n", progress);
      display.clearDisplay();
      display.setCursor(0, 0);
      display.setTextSize(1);
      display.println("Firmware Updating ...");
      display.drawRect(10, 20, 108, 10, SH110X_WHITE);
      display.fillRect(10, 20, progress, 10, SH110X_WHITE);
      display.setCursor(20, 40);
      display.setTextSize(1);
      display.print("Progress: ");
      display.print(progress);
      display.print("%");
      display.display();
      last_progress = progress;
    }
  }
}

void setup() {
  Serial.begin(115200);
  gps_serial.begin(9600);
  sim_serial.begin(115200, SERIAL_8N1, 20, 21);
  myservo.attach(servoPin);
  turn_off_servo();
  pinMode(led_pin, OUTPUT);

  debug_println("Wait...");

  create_topic();

  Wire.begin();
  setup_lcd();
  EEPROM.begin(512);
  display.clearDisplay();
  display.setCursor(25, 5);
  display.setTextSize(1);
  display.println("PHAO THONG MINH");
  display.setCursor(30, 25);
  display.setTextSize(1);
  display.print("Version ");
  display.println(__version__);

  display.display();
  delay(2000);

  display.clearDisplay();
  display.setCursor(30, 5);
  display.setTextSize(1);
  display.println("DEO CAM BIEN");

  display.setCursor(30, 25);
  display.setTextSize(1);
  display.println("KHOI TAO SIM");
  display.display();

  readDataFromEEPROM();

  setup_mpu6050();

  setup_server();

  button.attachClick([]() {
    SerialMon.println("Button clicked");
    if (fallDetected) {
      fallDetected = false;
    } else {
      isServoOn = !isServoOn;
    }
  });

  button.attachDoubleClick([]() {
    isSOS = !isSOS;

    SerialMon.println("SOS: " + String(isSOS));
  });

  button.attachLongPressStart([]() {
    isCall = true;
    SerialMon.println("Long press");
  });

  xTaskCreatePinnedToCore(TaskRestart, "TaskRestart", 10000, NULL, 1,
                          &TaskRestartHandler, 0);
  xTaskCreatePinnedToCore(TaskCall, "TaskCall", 10000, NULL, 1,
                          &TaskCallHandler, 0);

  // button task

  Update.onProgress(printProgress);
  setup_modem();

  xTaskCreatePinnedToCore(MainTask, "MainTask", 10000, NULL, 2, NULL, 0);

  debug_println("Setup done");
}

void loop() {
  if (!isCall) {
    modem_mqtt_handler();
    send_mqtt();
  }

  display_lcd();
  // led_blink_sos();
  if (isServoOn) {
    turn_on_servo();
  } else {
    turn_off_servo();
  }
  button.tick();

  if (isCall) {
    isSOS = true;
    print_send_sms();
    modem.init();
    modem.callHangup();
    SerialMon.println("Send SMS...");
    isSendSMS = true;
    sms_message = "Khan cap. Toi can giup do, ";
    String url = "https://www.google.com/maps/search/?api=1&query=" +
                 String(latitude, 6) + "," + String(longitude, 6);
    sms_message += url;
    send_sms(phone_number, sms_message);
    delay(2000);

    isSendSMS = false;
    SerialMon.println("Calling...");
    print_call();
    call_phone(phone_number);
    delay(15000);
    isSOS = false;
    isSendSMS = false;
    isCall = false;
  }

  if (isOTA) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("Updating firmware...");
    display.display();
    otaUpdate();
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("Updating DONE");
    display.display();
    delay(2000);
    // Sau khi cập nhật xong, khởi động lại
    ESP.restart();
    isOTA = false;
  }

  // ElegantOTA.loop();

  button.tick();
  read_gps();
  saveDataToEEPROM();
}

// Hàm hiển thị phản hồi trên OLED
void displayResponse(String title, String response) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println(title);
  display.println("----------------");
  display.println(response);
  display.display();
  delay(2000); // Hiển thị trong 2 giây
}

void send_sms(String phone_number, String message) {
  modem.sendSMS(phone_number, message);
}

// Hàm thiết lập màn hình OLED
void setup_lcd() {
  if (!display.begin(SCREEN_ADDRESS, true)) {
    for (int i = 0; i < 10; i++) {
      digitalWrite(13, HIGH);
      delay(100);
      digitalWrite(13, LOW);
      delay(100);
    }
  }

  display.setTextColor(SH110X_WHITE);
  // Kích thước văn bản
  display.setTextSize(2);
}

// Hàm hiển thị thông tin cuộc gọi
void print_call() {
  display.clearDisplay();
  display.setCursor(20, 5);
  display.setTextSize(2);
  display.println("  CALL");
  display.setTextSize(2);
  display.setCursor(0, 30);
  display.print(phone_number);
  display.display();
}

void print_send_sms() {
  display.clearDisplay();
  display.setCursor(20, 5);
  display.setTextSize(2);
  display.println("SEND SMS");
  display.setTextSize(2);
  display.setCursor(0, 30);
  display.print(phone_number);
  display.display();
}

// Hàm hiển thị thông tin ngã
void print_fall() {
  display.clearDisplay();
  display.setCursor(20, 0);
  display.setTextSize(2);
  display.println("  FALL");
  display.setTextSize(2);
  display.setCursor(13, 35);
  display.print("DETECTED");
  display.display();
}

// Hàm hiển thị thông tin lên màn hình OLED
void display_lcd() {
  if ((millis() - oldtime_display > time_dislay || force_display) &&
      !fallDetected) {

    if (isCall && !isSendSMS) {
      print_call();
    } else if (isCall && isSendSMS) {
      print_send_sms();
    } else {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.setTextSize(1);
      String mac_address = get_mac_address();
      display.println(mac_address);
      display.setTextSize(2);
      display.setCursor(15, 20);
      if (beatAvg > 0) {
        display.print(beatAvg);
      } else {
        display.print("X");
      }
      display.setTextSize(1);
      display.setCursor(0, 50);
      display.print(heartrate_threshold);
      display.print(" bpm");

      // Số điện thoại
      display.setTextSize(1);
      display.setCursor(50, 0);
      display.println(phone_number);

      if (gps.time.isValid()) {
        LocalTime localTime = getLocalTimeFunc();
        display.setCursor(50, 12);
        display.print("Time:");
        if (localTime.hour < 10)
          display.print("0");
        display.print(localTime.hour);
        display.print(":");
        if (localTime.minute < 10)
          display.print("0");
        display.print(localTime.minute);
        display.print(":");
        if (localTime.second < 10)
          display.print("0");
        display.print(localTime.second);
      } else {
        display.setCursor(50, 12);
        display.println("TIME: CHUA CO");
      }

      // Latitude và Longitude
      display.setCursor(50, 24);
      display.print("LA:");
      display.println(latitude, 6);
      display.setCursor(50, 36);
      display.print("LO:");
      display.println(longitude, 6);
      display.setCursor(50, 50);
      // display.print("MQTT:");
      display.println(mqtt.connected() ? "Da ket noi" : "Mat ket noi");
      display.display();
    }

    force_display = false;

    oldtime_display = millis();
  }
}

void onOTAStart() {}
unsigned long ota_progress_millis = 0;
void onOTAProgress(size_t current, size_t final) {
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
  }
}

void onOTAEnd(bool success) {
  if (success) {
  } else {
  }
}

void setup_server() {

  // ElegantOTA.begin(&server); // Start ElegantOTA
  // // ElegantOTA callbacks
  // ElegantOTA.onStart(onOTAStart);
  // ElegantOTA.onProgress(onOTAProgress);
  // ElegantOTA.onEnd(onOTAEnd);

  // server.onNotFound(notFound);
  // server.begin();
}

// Thời gian đọc GPS
unsigned long oldtime_read_gps = 0;
unsigned long time_read_gps = 20000;
unsigned long oldtime_in_read_gps = 0;
unsigned long time_in_read_gps = 2000;
bool isInReadGPS = false;

// Thời gian cập nhật GPS
unsigned long oldtime_update_gps = 0;
unsigned long time_update_gps = 10000;

String gps_data = "";

void add_new_debug(String new_debug) {
  gps_data += new_debug;

  if (gps_data.length() > 300) {
    // SerialMon.print(gps_data);
    if (mqtt.connected() && isDebug) {
      mqtt.publish(debug_topic.c_str(), gps_data.c_str());
    } else {
      if (!isDebug) {
        SerialMon.println("not debug");
      } else {
        SerialMon.println("MQTT not connected");
      }
    }
    gps_data = "";
  }
}

// Hàm đọc GPS
void read_gps() {
  while (gps_serial.available() > 0) {
    char c = gps_serial.read();
    // gps_serial.write(c);
    add_new_debug(String(c));

    if (gps.encode(c)) {
      if (gps.location.isValid()) {
        latitude = gps.location.lat();
        longitude = gps.location.lng();
        user_data.latitude = latitude;
        user_data.longitude = longitude;
        if (millis() - oldtime_update_gps > time_update_gps) {
          isSaveNumber = true;
          oldtime_update_gps = millis();
        }
      }
    }
  }
}

// Hàm đọc gia tốc tổng
float readAccel() {
  int16_t ax, ay, az, gx, gy, gz;
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
  // Nếu MPU6050 để ±2g => 1g ~ 16384 LSB
  float Axf = ax / 16384.0f;
  float Ayf = ay / 16384.0f;
  float Azf = az / 16384.0f;
  return sqrt(Axf * Axf + Ayf * Ayf + Azf * Azf);
}

// Hàm đo stdDev trong khoảng timeWindow ms
float measureStdDev(unsigned long timeWindow) {
  unsigned long startTime = millis();
  float sum = 0.0f, sumSq = 0.0f;
  int count = 0;

  while (millis() - startTime < timeWindow) {
    float val = readAccel();
    sum += val;
    sumSq += val * val;
    count++;
    delay(10); // Lấy mẫu ~100Hz
  }

  if (count == 0)
    return 0.0f; // tránh chia 0
  float mean = sum / count;
  float variance = (sumSq / count) - (mean * mean);
  return sqrt(variance);
}

// Thời gian đọc MPU6050
unsigned long last_read_mpu = 0;
unsigned long time_read_mpu = 500;

// Hàm đọc MPU6050
void read_mpu() {
  if (mpu6050InterruptDetected) {
    byte intStatus = readMPU6050(INT_STATUS);
    if (intStatus & 0x40) { // Phát hiện ngã
      // Serial.println("Fall Detected!");
      fallDetected = true;
      print_fall();
      lastTimeCheckFall = millis();
      fallTime = millis();
    }

    // fallDetected = true;
  }

  if (fallDetected) {
    if (millis() - lastTimeCheckFall > timeCheckFall) {
      fallDetected = false;
      lastTimeCheckFall = millis();
      isCall = true;
      isSOS = true;
    }
  }
}

// Hàm thiết lập MPU6050
void setup_mpu6050() {
  writeMPU6050(PWR_MGMT_1, 0x00);   // Thức dậy MPU6050
  writeMPU6050(ACCEL_CONFIG, 0x10); // Cài đặt độ nhạy gia tốc +/- 8g
  writeMPU6050(MOT_THR, 10);        // Thiết lập ngưỡng chuyển động
  writeMPU6050(MOT_DUR, 10);        // Thiết lập thời gian chuyển động
  writeMPU6050(INT_ENABLE, 0x40);   // Bật ngắt phát hiện chuyển động

  // Gán ngắt
  pinMode(INTERRUPT_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), handleInterrupt,
                  RISING);
}

void scan_i2c() {
  byte error, address;
  int nDevices;
  append_status_ln("Scanning...");

  nDevices = 0;
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      append_status_ln("I2C device found at address 0x");
      if (address < 16) {
        append_status("0");
      }
      append_status(String(address, HEX));
      append_status_ln("  !");

      nDevices++;
    } else if (error == 4) {
      // Serial.print("Unknown error at address 0x");
      append_status_ln("Unknown error at address 0x");
      if (address < 16) {
        // Serial.print("0");
        append_status("0");
      }
      // Serial.println(address, HEX);
      append_status_ln(String(address, HEX));
    }
  }
  if (nDevices == 0) {
    // Serial.println("No I2C devices found\n");
    append_status_ln("No I2C devices found");
  } else {
    // Serial.println("done\n");
    append_status_ln("done");
  }
  delay(5000);
}

// Hàm viết dữ liệu vào MPU6050
void writeMPU6050(byte reg, byte data) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

// Hàm đọc dữ liệu từ MPU6050
byte readMPU6050(byte reg) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(MPU6050_ADDR, 1);
  return Wire.read();
}

// Hàm xử lý ngắt
void IRAM_ATTR handleInterrupt() {
  mpu6050InterruptDetected = true;
  fallTime = millis();
}
