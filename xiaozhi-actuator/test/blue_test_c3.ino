#include <Arduino.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <HardwareSerial.h>
// =======================================
// BLE UUID
// =======================================
static BLEUUID heartRateServiceUUID((uint16_t)0x180D);
static BLEUUID heartRateMeasurementCharUUID((uint16_t)0x2A37);
static BLEUUID batteryServiceUUID((uint16_t)0x180F);
static BLEUUID batteryLevelCharUUID((uint16_t)0x2A19);

// =======================================
// Biến toàn cục
// =======================================
static BLEClient *pClient = nullptr;
static BLERemoteCharacteristic *pHeartRateChar = nullptr;
static BLERemoteCharacteristic *pBatteryChar = nullptr;
static bool connected = false;
int heartRateValue = -1;
int batteryLevel = -1;

static String targetAddressStr = "";
static esp_ble_addr_type_t targetAddressType;
static bool foundDevice = false;

// =======================================
// Callback
// =======================================
void heartRateNotifyCallback(BLERemoteCharacteristic *, uint8_t *data,
                             size_t len, bool) {
  if (len < 2)
    return;
  heartRateValue = data[1];
  Serial.printf("❤️ Heart Rate: %d bpm\n", heartRateValue);
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *) override {
    connected = true;
    Serial.println("[BLE] ✅ Connected");
  }
  void onDisconnect(BLEClient *) override {
    connected = false;
    Serial.println("[BLE] ❌ Disconnected");
  }
};

// =======================================
// Quét tất cả thiết bị
// =======================================
void scanDevices(uint8_t duration_sec = 5) {
  BLEScan *pScan = BLEDevice::getScan();
  pScan->setActiveScan(true);
  Serial.printf("[SCAN] Đang quét thiết bị BLE (%ds)...\n", duration_sec);

  BLEScanResults results = pScan->start(duration_sec, false);

  if (results.getCount() == 0) {
    Serial.println("[SCAN] ❌ Không tìm thấy thiết bị nào.");
  } else {
    Serial.printf("[SCAN] ✅ Tìm thấy %d thiết bị:\n", results.getCount());
    for (int i = 0; i < results.getCount(); i++) {
      BLEAdvertisedDevice d = results.getDevice(i);
      Serial.printf("  • %s | RSSI: %d", d.getAddress().toString().c_str(),
                    d.getRSSI());
      if (d.haveName())
        Serial.printf(" | Name: %s", d.getName().c_str());
      if (d.haveServiceUUID())
        Serial.printf(" | Có UUID dịch vụ");
      Serial.println();
    }
  }

  // STOP và CLEAR hoàn toàn
  pScan->stop();
  pScan->clearResults();
  delay(100);
}

// =======================================
// Tìm thiết bị nhịp tim
// =======================================
bool scanHeartRateDevice(uint8_t duration_sec = 10) {
  foundDevice = false;
  BLEScan *pScan = BLEDevice::getScan();
  pScan->setActiveScan(true);

  Serial.printf("[SCAN] Tìm thiết bị Heart Rate (%ds)...\n", duration_sec);
  BLEScanResults results = pScan->start(duration_sec, false);

  // Ưu tiên 1: Tìm theo Service UUID
  for (int i = 0; i < results.getCount(); i++) {
    BLEAdvertisedDevice d = results.getDevice(i);
    if (d.haveServiceUUID() && d.isAdvertisingService(heartRateServiceUUID)) {
      Serial.printf("[SCAN] 🩺 Tìm thấy (Service UUID): %s\n",
                    d.getAddress().toString().c_str());

      targetAddressStr = String(d.getAddress().toString().c_str());
      targetAddressType = d.getAddressType();
      foundDevice = true;

      // STOP scan trước khi return!
      pScan->stop();
      delay(200); // Đợi scan stop hoàn toàn
      pScan->clearResults();

      return true;
    }
  }

  // Ưu tiên 2: Tìm theo tên
  for (int i = 0; i < results.getCount(); i++) {
    BLEAdvertisedDevice d = results.getDevice(i);
    if (d.haveName()) {
      String name = String(d.getName().c_str());
      name.toUpperCase();
      if (name.indexOf("HR") >= 0 || name.indexOf("HEART") >= 0) {
        Serial.printf("[SCAN] 🩺 Tìm thấy (Tên): %s - %s\n",
                      d.getName().c_str(), d.getAddress().toString().c_str());

        targetAddressStr = String(d.getAddress().toString().c_str());
        targetAddressType = d.getAddressType();
        foundDevice = true;

        pScan->stop();
        delay(200);
        pScan->clearResults();

        return true;
      }
    }
  }

  pScan->stop();
  pScan->clearResults();
  Serial.println("[SCAN] ❌ Không tìm thấy thiết bị nhịp tim.");
  return false;
}

// =======================================
// Kết nối
// =======================================
bool connectToDevice() {
  if (!foundDevice || targetAddressStr.isEmpty()) {
    Serial.println("[BLE] ❌ Không có địa chỉ thiết bị");
    return false;
  }

  Serial.printf("[BLE] Đang kết nối tới %s...\n", targetAddressStr.c_str());

  // Cleanup client cũ
  if (pClient != nullptr) {
    if (pClient->isConnected()) {
      pClient->disconnect();
    }
    delete pClient;
    pClient = nullptr;
    delay(100);
  }

  // Tạo BLEAddress từ string
  BLEAddress addr(targetAddressStr.c_str());

  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  // Connect
  bool ok = pClient->connect(addr, targetAddressType);
  if (!ok) {
    Serial.println("[BLE] ❌ Kết nối thất bại");
    return false;
  }

  Serial.println("[BLE] ✅ Kết nối thành công, đang lấy services...");
  delay(1000); // Đợi services sẵn sàng

  // Đọc pin
  BLERemoteService *pBatterySrv = pClient->getService(batteryServiceUUID);
  if (pBatterySrv) {
    pBatteryChar = pBatterySrv->getCharacteristic(batteryLevelCharUUID);
    if (pBatteryChar && pBatteryChar->canRead()) {
      std::string v = pBatteryChar->readValue();
      if (!v.empty()) {
        batteryLevel = (uint8_t)v[0];
        Serial.printf("🔋 Battery: %d%%\n", batteryLevel);
      }
    }
  }

  // Đăng ký notify nhịp tim
  BLERemoteService *pHRSrv = pClient->getService(heartRateServiceUUID);
  if (pHRSrv) {
    pHeartRateChar = pHRSrv->getCharacteristic(heartRateMeasurementCharUUID);
    if (pHeartRateChar && pHeartRateChar->canNotify()) {
      pHeartRateChar->registerForNotify(heartRateNotifyCallback);
      Serial.println("[BLE] ✅ Đã bật Heart Rate Notify!");
    } else {
      Serial.println("[BLE] ⚠️ Characteristic không hỗ trợ notify");
    }
  } else {
    Serial.println("[BLE] ⚠️ Không tìm thấy dịch vụ Heart Rate!");
  }

  return true;
}

// =======================================
// Setup / Loop
// =======================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== ESP32 BLE Heart Rate (Scan + Connect) ===");

  BLEDevice::init("ESP32_HeartClient");
  // BLEDevice::setPower(ESP_PWR_LVL_P7);

  scanDevices(5);
  delay(500);

  if (scanHeartRateDevice(10)) {
    delay(500); // Đợi sau scan
    connectToDevice();
  }
}

void loop() {
  if (!connected) {
    static unsigned long last = 0;
    if (millis() - last > 15000) {
      last = millis();
      Serial.println("\n[BLE] Re-scan vì mất kết nối...");
      if (scanHeartRateDevice(10)) {
        delay(500);
        connectToDevice();
      }
    }
  } else {
    Serial.printf("❤️ BPM: %d | 🔋 %d%%\n", heartRateValue, batteryLevel);
  }
  delay(3000);
}