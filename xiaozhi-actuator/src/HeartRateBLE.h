#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>

class HeartRateBLE {
public:
  using DataCallback = void (*)(int bpm, int battery);
  using ConnectCallback = void (*)(bool connected);

  HeartRateBLE();

  void begin(const char *clientName = "ESP32_HeartClient");

  // ===== Quét thiết bị =====
  void scanAll(uint8_t duration = 5);
  bool scanHeartRate(uint8_t duration = 10);

  // ===== Kết nối =====
  bool connectToDevice();
  bool connectDirect(const char *addrStr, esp_ble_addr_type_t addrType = BLE_ADDR_TYPE_PUBLIC);

  // ===== Thiết lập =====
  void setDataCallback(DataCallback cb);
  void setConnectCallback(ConnectCallback cb);
  
  void setAutoReconnect(bool enable, uint8_t maxRetry = 3);

  // ===== Vòng lặp =====
  void loop();

  bool isConnected() const { return _connected; }
  int getHeartRate() const { return _heartRate; }
  int getBattery() const { return _battery; }

private:
  static void onHeartRateNotify(BLERemoteCharacteristic *pChar, uint8_t *data, size_t len, bool isNotify);

  class ClientCallback : public BLEClientCallbacks {
  public:
    void onConnect(BLEClient *pclient) override;
    void onDisconnect(BLEClient *pclient) override;
    virtual ~ClientCallback() = default;
  };

  static HeartRateBLE *_instance;

  BLEClient *_client = nullptr;
  BLERemoteCharacteristic *_hrChar = nullptr;
  BLERemoteCharacteristic *_batChar = nullptr;

  bool _connected = false;
  bool _found = false;
  bool _autoReconnect = false;
  uint8_t _maxRetry = 3;
  uint8_t _retryCount = 0;

  int _heartRate = -1;
  int _battery = -1;
  uint8_t _addrBytes[6] = {0};
  esp_ble_addr_type_t _addrType = BLE_ADDR_TYPE_PUBLIC;

  DataCallback _dataCb = nullptr;
  ConnectCallback _connCb = nullptr;
};
