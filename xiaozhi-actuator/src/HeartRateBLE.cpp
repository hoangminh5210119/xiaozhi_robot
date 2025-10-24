#include "HeartRateBLE.h"

static BLEUUID HEART_RATE_SERVICE_UUID((uint16_t)0x180D);
static BLEUUID HEART_RATE_CHAR_UUID((uint16_t)0x2A37);
static BLEUUID BATTERY_SERVICE_UUID((uint16_t)0x180F);
static BLEUUID BATTERY_CHAR_UUID((uint16_t)0x2A19);

HeartRateBLE *HeartRateBLE::_instance = nullptr;

// =======================================
// Constructor
// =======================================
HeartRateBLE::HeartRateBLE() { _instance = this; }

// =======================================
// Khởi tạo BLE
// =======================================
void HeartRateBLE::begin(const char *clientName) {
  Serial.println("[BLE] ⚙️ Khởi tạo BLE...");
  BLEDevice::init(clientName);
  BLEDevice::setMTU(517);
}

// =======================================
// Scan tất cả thiết bị BLE
// =======================================
void HeartRateBLE::scanAll(uint8_t duration) {
  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);

  Serial.printf("[SCAN] Đang quét BLE (%ds)...\n", duration);
  BLEScanResults results = scan->start(duration, false);
  int count = results.getCount();

  if (count == 0) {
    Serial.println("[SCAN] ❌ Không tìm thấy thiết bị nào.");
  } else {
    Serial.printf("[SCAN] ✅ Tìm thấy %d thiết bị:\n", count);
    for (int i = 0; i < count; i++) {
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
  scan->stop();
  scan->clearResults();
}

// =======================================
// Scan riêng thiết bị Heart Rate
// =======================================
bool HeartRateBLE::scanHeartRate(uint8_t duration) {
  _found = false;
  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);

  Serial.printf("[SCAN] 🔍 Tìm thiết bị Heart Rate (%ds)...\n", duration);
  BLEScanResults results = scan->start(duration, false);

  for (int i = 0; i < results.getCount(); i++) {
    BLEAdvertisedDevice d = results.getDevice(i);
    if (d.haveServiceUUID() &&
        d.isAdvertisingService(HEART_RATE_SERVICE_UUID)) {
      memcpy(_addrBytes, d.getAddress().getNative(), 6);
      _addrType = d.getAddressType();
      _found = true;
      Serial.printf("[SCAN] 🩺 Tìm thấy HR Device: %s\n",
                    d.getAddress().toString().c_str());
      scan->stop();
      return true;
    }
  }
  Serial.println("[SCAN] ❌ Không tìm thấy thiết bị Heart Rate.");
  scan->stop();
  scan->clearResults();
  return false;
}

// =======================================
// Connect bằng địa chỉ đã quét
// =======================================
bool HeartRateBLE::connectToDevice() {
  if (!_found) {
    Serial.println("[BLE] ❌ Không có thiết bị để kết nối.");
    return false;
  }
  BLEAddress addr(_addrBytes);
  return connectDirect(addr.toString().c_str(), _addrType);
}

// =======================================
// Connect trực tiếp bằng địa chỉ (không cần quét)
// =======================================
bool HeartRateBLE::connectDirect(const char *addrStr,
                                 esp_ble_addr_type_t addrType) {
  Serial.printf("[BLE] 🔗 Kết nối trực tiếp tới %s (%s)...\n", addrStr,
                addrType == BLE_ADDR_TYPE_RANDOM ? "RANDOM" : "PUBLIC");

  // ✅ Lưu lại địa chỉ vào _addrBytes để phục vụ reconnect
  BLEAddress addr(addrStr);
  memcpy(_addrBytes, addr.getNative(), 6);
  _addrType = addrType;

  if (_client && _client->isConnected()) {
    _client->disconnect();
    delete _client;
    delay(100);
  }

  _client = BLEDevice::createClient();
  _client->setClientCallbacks(new ClientCallback());
  _client->setMTU(517);

  bool ok = false;
  try {
    ok = _client->connect(addr, addrType);
  } catch (...) {
    Serial.println("[BLE] ❌ Ngoại lệ khi connect()");
  }

  if (!ok || !_client->isConnected()) {
    Serial.println("[BLE] ❌ Kết nối thất bại!");
    if (_connCb)
      _connCb(false);
    return false;
  }

  _connected = true;
  _retryCount = 0;
  Serial.println("[BLE] ✅ Kết nối thành công!");
  if (_connCb)
    _connCb(true);

  delay(1000); // Cho BLE ổn định

  BLERemoteService *batSrv = _client->getService(BATTERY_SERVICE_UUID);
  if (batSrv) {
    _batChar = batSrv->getCharacteristic(BATTERY_CHAR_UUID);
    if (_batChar && _batChar->canRead()) {
      std::string v = _batChar->readValue();
      if (!v.empty()) {
        _battery = (uint8_t)v[0];
        Serial.printf("🔋 Battery: %d%%\n", _battery);
      }
    }
  }

  BLERemoteService *hrSrv = _client->getService(HEART_RATE_SERVICE_UUID);
  if (hrSrv) {
    _hrChar = hrSrv->getCharacteristic(HEART_RATE_CHAR_UUID);
    if (_hrChar && _hrChar->canNotify()) {
      _hrChar->registerForNotify(onHeartRateNotify);
      Serial.println("[BLE] 🩺 Notify Heart Rate đã bật!");
    }
  }
  return true;
}

// =======================================
// Notify callback
// =======================================
void HeartRateBLE::onHeartRateNotify(BLERemoteCharacteristic *, uint8_t *data,
                                     size_t len, bool) {
  if (len < 2)
    return;
  int bpm = data[1];
  if (_instance) {
    _instance->_heartRate = bpm;
    Serial.printf("❤️ HR: %d bpm\n", bpm);
    if (_instance->_dataCb)
      _instance->_dataCb(bpm, _instance->_battery);
  }
}

// =======================================
// BLE client callback
// =======================================
void HeartRateBLE::ClientCallback::onConnect(BLEClient *) {
  Serial.println("[BLE] ✅ Connected");
  if (_instance) {
    _instance->_connected = true;
    _instance->_retryCount = 0;
    if (_instance->_connCb)
      _instance->_connCb(true);
  }
}

void HeartRateBLE::ClientCallback::onDisconnect(BLEClient *) {
  Serial.println("[BLE] ❌ Disconnected");
  if (_instance) {
    _instance->_connected = false;
    if (_instance->_connCb)
      _instance->_connCb(false);
  }
}

// =======================================
// Tự reconnect trong loop
// =======================================
void HeartRateBLE::loop() {
  if (!_connected && _autoReconnect && _retryCount < _maxRetry) {
    // Kiểm tra địa chỉ hợp lệ trước khi reconnect
    bool validAddr = false;
    for (int i = 0; i < 6; i++) {
      if (_addrBytes[i] != 0x00) {
        validAddr = true;
        break;
      }
    }

    if (!validAddr) {
      Serial.println("[BLE] ⚠️ Không có địa chỉ hợp lệ để reconnect, bỏ qua.");
      return;
    }

    _retryCount++;
    Serial.printf("[BLE] 🔄 Reconnect attempt %d/%d...\n", _retryCount,
                  _maxRetry);
    BLEAddress addr(_addrBytes);
    connectDirect(addr.toString().c_str(), _addrType);
  }
  delay(2000);
}

// =======================================
// Callback setter
// =======================================
void HeartRateBLE::setDataCallback(DataCallback cb) { _dataCb = cb; }
void HeartRateBLE::setConnectCallback(ConnectCallback cb) { _connCb = cb; }
void HeartRateBLE::setAutoReconnect(bool enable, uint8_t maxRetry) {
  _autoReconnect = enable;
  _maxRetry = maxRetry;
}
