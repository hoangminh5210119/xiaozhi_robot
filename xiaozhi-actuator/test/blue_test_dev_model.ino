#include <Arduino.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>

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

// LƯU ĐỊA CHỈ DƯỚI DẠNG RAW BYTES - QUAN TRỌNG CHO ESP32 CLASSIC!
static uint8_t targetAddrBytes[6] = {0};
static esp_ble_addr_type_t targetAddressType = BLE_ADDR_TYPE_PUBLIC;
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
  pScan->setInterval(100);
  pScan->setWindow(99);
  
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

  pScan->stop();
  delay(100);
  pScan->clearResults();
}

// =======================================
// Tìm thiết bị nhịp tim
// =======================================
bool scanHeartRateDevice(uint8_t duration_sec = 10) {
  foundDevice = false;
  BLEScan *pScan = BLEDevice::getScan();
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);

  Serial.printf("[SCAN] Tìm thiết bị Heart Rate (%ds)...\n", duration_sec);
  BLEScanResults results = pScan->start(duration_sec, false);

  // Ưu tiên 1: Tìm theo Service UUID
  for (int i = 0; i < results.getCount(); i++) {
    BLEAdvertisedDevice d = results.getDevice(i);
    if (d.haveServiceUUID() && d.isAdvertisingService(heartRateServiceUUID)) {
      Serial.printf("[SCAN] 🩺 Tìm thấy (Service UUID): %s\n",
                    d.getAddress().toString().c_str());

      // SAO CHÉP ĐỊA CHỈ DƯỚI DẠNG BYTES
      esp_bd_addr_t *addr = d.getAddress().getNative();
      memcpy(targetAddrBytes, addr, 6);
      targetAddressType = d.getAddressType();
      foundDevice = true;

      pScan->stop();
      delay(200);
      // KHÔNG CLEAR ngay! Đợi sau khi connect
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

        esp_bd_addr_t *addr = d.getAddress().getNative();
        memcpy(targetAddrBytes, addr, 6);
        targetAddressType = d.getAddressType();
        foundDevice = true;

        pScan->stop();
        delay(200);
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
  if (!foundDevice) {
    Serial.println("[BLE] ❌ Không có địa chỉ thiết bị");
    BLEDevice::getScan()->clearResults();
    return false;
  }

  // Tạo BLEAddress từ raw bytes - ĐÚNG CÁCH!
  BLEAddress addr(targetAddrBytes);
  
  Serial.printf("[BLE] Đang kết nối tới %s...\n", addr.toString().c_str());

  // Cleanup client cũ
  if (pClient != nullptr) {
    if (pClient->isConnected()) {
      pClient->disconnect();
    }
    delete pClient;
    pClient = nullptr;
    delay(200);
  }

  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  pClient->setMTU(517); // Tăng MTU

  // Connect với timeout
  bool ok = pClient->connect(addr, targetAddressType);
  
  // Giờ mới clear scan results
  BLEDevice::getScan()->clearResults();
  
  if (!ok) {
    Serial.println("[BLE] ❌ Kết nối thất bại");
    delete pClient;
    pClient = nullptr;
    return false;
  }

  Serial.println("[BLE] ✅ Kết nối thành công!");
  delay(1000); // Đợi services discovery

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
  Serial.println("=== ESP32 BLE Heart Rate (ESP32 Classic) ===");

  // Khởi tạo BLE với config cho ESP32 classic
  BLEDevice::init("ESP32_HeartClient");
  
  // Không dùng max power cho ESP32 classic - có thể gây crash
  // BLEDevice::setPower(ESP_PWR_LVL_P7);

  scanDevices(5);
  delay(500);

  if (scanHeartRateDevice(10)) {
    delay(500);
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