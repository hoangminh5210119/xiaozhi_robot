# I2C Command Bridge - Usage Guide

## Giới thiệu

`I2CCommandBridge` là thư viện giao tiếp I2C giữa ESP32 chính (Xiaozhi) và ESP32 phụ (Actuator) sử dụng protocol JSON ngắn gọn.

## Protocol

### Command Format (Compact JSON)

**Vehicle Control (Time-based):**
```json
{"t":"v","d":1,"p":50,"ms":2000}  // Forward 50% speed for 2000ms
{"t":"v","d":2,"p":60,"ms":1000}  // Backward 60% speed for 1000ms
{"t":"v","d":3,"p":40,"ms":1500}  // Left 40% speed for 1500ms
{"t":"v","d":4,"p":40,"ms":1500}  // Right 40% speed for 1500ms
{"t":"v","d":5,"p":50,"ms":2000}  // Rotate left 50% for 2000ms
{"t":"v","d":6,"p":50,"ms":2000}  // Rotate right 50% for 2000ms
{"t":"v","d":0}                   // Stop
```

**Vehicle Control (Distance-based):**
```json
{"t":"v","d":1,"p":50,"mm":500}   // Forward 500mm at 50% speed
{"t":"v","d":2,"p":40,"mm":300}   // Backward 300mm at 40% speed
```

**Storage Control:**
```json
{"t":"s","i":0,"a":1}             // Open slot 0
{"t":"s","i":0,"a":0}             // Close slot 0
{"t":"s","i":1,"a":1}             // Open slot 1
{"t":"s","i":2,"a":0}             // Close slot 2
```

**Status Request:**
```json
{"t":"t"}                         // Get status
```

### Response Format

```json
{"s":1}                           // Status: OK
{"s":-1}                          // Status: Error
```

## Cách sử dụng

### 1. Khởi tạo

```cpp
#include "I2CCommandBridge.h"

I2CCommandBridge bridge;

// Option 1: Tạo I2C bus riêng
if (!bridge.Init()) {
    ESP_LOGE(TAG, "Failed to init I2C bridge");
    return;
}

// Option 2: Sử dụng I2C bus có sẵn (shared với display)
i2c_master_bus_handle_t existing_bus = ...; // from display init
if (!bridge.InitWithExistingBus(existing_bus)) {
    ESP_LOGE(TAG, "Failed to init I2C bridge with existing bus");
    return;
}
```

### 2. Điều khiển xe (Vehicle Control)

```cpp
// Di chuyển tiến 50% tốc độ trong 2 giây
std::string resp = bridge.VehicleMoveTime(DIR_FORWARD, 50, 2000);

// Di chuyển lùi 500mm với tốc độ 40%
std::string resp = bridge.VehicleMoveDistance(DIR_BACKWARD, 40, 500);

// Rẽ trái 60% trong 1 giây
std::string resp = bridge.VehicleMoveTime(DIR_LEFT, 60, 1000);

// Quay phải tại chỗ 50% trong 2 giây
std::string resp = bridge.VehicleMoveTime(DIR_ROTATE_RIGHT, 50, 2000);

// Dừng xe
std::string resp = bridge.VehicleStop();
```

### 3. Điều khiển tủ đồ (Storage Control)

```cpp
// Mở ô số 0
std::string resp = bridge.StorageOpen(0);

// Đóng ô số 1
std::string resp = bridge.StorageClose(1);

// Điều khiển trực tiếp
std::string resp = bridge.StorageControl(2, ACT_OPEN);  // Mở ô 2
std::string resp = bridge.StorageControl(3, ACT_CLOSE); // Đóng ô 3
```

### 4. Lấy trạng thái

```cpp
std::string status = bridge.GetStatus();
// Parse JSON response to get status info
```

### 5. Kiểm tra slave online

```cpp
if (bridge.IsSlaveOnline()) {
    ESP_LOGI(TAG, "Slave is online");
} else {
    ESP_LOGW(TAG, "Slave is offline");
}
```

### 6. Đăng ký callback để nhận status tự động

```cpp
// Hàm callback sẽ được gọi mỗi khi nhận status
void onStatusReceived(const ActuatorStatus& status, void* user_data) {
    ESP_LOGI(TAG, "📊 Status Update:");
    ESP_LOGI(TAG, "  Battery: %.1fV", status.battery);
    ESP_LOGI(TAG, "  Heart Rate: %d bpm", status.heart_rate);
    ESP_LOGI(TAG, "  BLE Connected: %s", status.ble_connected ? "Yes" : "No");
    ESP_LOGI(TAG, "  Motor Enabled: %s", status.motor_enabled ? "Yes" : "No");
    ESP_LOGI(TAG, "  Moving: %s", status.is_moving ? "Yes" : "No");
    
    // In trạng thái storage
    for (int i = 0; i < 4; i++) {
        ESP_LOGI(TAG, "  Storage[%d]: %s", 
                 status.storage[i].slot, 
                 status.storage[i].is_open ? "OPEN" : "CLOSED");
    }
    
    // User data example (optional)
    if (user_data) {
        int* counter = (int*)user_data;
        (*counter)++;
        ESP_LOGI(TAG, "  Status received count: %d", *counter);
    }
}

// Đăng ký callback
int status_count = 0;
bridge.SetStatusCallback(onStatusReceived, &status_count);

// Bắt đầu polling status mỗi 2 giây
bridge.StartStatusPolling(2000);

// Sau này có thể dừng polling
// bridge.StopStatusPolling();
```

### 7. Kiểm tra polling status

```cpp
if (bridge.IsPollingActive()) {
    ESP_LOGI(TAG, "Status polling is active");
} else {
    ESP_LOGI(TAG, "Status polling is not active");
}
```

## Direction Constants

```cpp
#define DIR_STOP            0    // Dừng
#define DIR_FORWARD         1    // Tiến
#define DIR_BACKWARD        2    // Lùi
#define DIR_LEFT            3    // Rẽ trái
#define DIR_RIGHT           4    // Rẽ phải
#define DIR_ROTATE_LEFT     5    // Quay trái tại chỗ
#define DIR_ROTATE_RIGHT    6    // Quay phải tại chỗ
```

## Action Constants

```cpp
#define ACT_CLOSE       0    // Đóng
#define ACT_OPEN        1    // Mở
```

## Legacy Compatibility

Để tương thích với code cũ, thư viện vẫn hỗ trợ API cũ:

```cpp
// Old API (vẫn hoạt động)
bridge.SendVehicleCommand("forward", 50, 2000);
bridge.SendStorageCommand(0, "open");

// New API (recommended)
bridge.VehicleMoveTime(DIR_FORWARD, 50, 2000);
bridge.StorageOpen(0);
```

## Error Handling

```cpp
std::string resp = bridge.VehicleMoveTime(DIR_FORWARD, 50, 2000);

// Parse response to check for errors
if (resp.find("error") != std::string::npos) {
    ESP_LOGE(TAG, "Command failed: %s", resp.c_str());
    
    if (resp.find("slave_offline") != std::string::npos) {
        ESP_LOGW(TAG, "Slave is offline, check connection");
    }
}
```

## Example: Complete Program

```cpp
#include "I2CCommandBridge.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

I2CCommandBridge bridge;
int status_update_count = 0;

// Callback function để nhận status
void onStatusUpdate(const ActuatorStatus& status, void* user_data) {
    int* count = (int*)user_data;
    (*count)++;
    
    ESP_LOGI("APP", "📊 Status Update #%d", *count);
    ESP_LOGI("APP", "  🔋 Battery: %.1fV", status.battery);
    ESP_LOGI("APP", "  ❤️  Heart Rate: %d bpm", status.heart_rate);
    ESP_LOGI("APP", "  📡 BLE: %s", status.ble_connected ? "Connected" : "Disconnected");
    ESP_LOGI("APP", "  🚗 Moving: %s", status.is_moving ? "Yes" : "No");
    
    // Kiểm tra trạng thái storage
    for (int i = 0; i < 4; i++) {
        if (status.storage[i].is_open) {
            ESP_LOGI("APP", "  📦 Slot %d: OPEN", i);
        }
    }
}

void app_main() {
    // Init I2C bridge
    if (!bridge.Init()) {
        ESP_LOGE("APP", "Failed to init bridge");
        return;
    }
    
    // Đăng ký callback để nhận status mỗi 1 giây
    bridge.SetStatusCallback(onStatusUpdate, &status_update_count);
    bridge.StartStatusPolling(1000);
    
    // Check if slave is online
    if (!bridge.IsSlaveOnline()) {
        ESP_LOGW("APP", "Slave is offline!");
        return;
    }
    
    // Demo sequence
    ESP_LOGI("APP", "Starting demo sequence...");
    
    // 1. Move forward 500mm
    ESP_LOGI("APP", "Step 1: Moving forward 500mm");
    bridge.VehicleMoveDistance(DIR_FORWARD, 50, 500);
    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait 5s
    
    // 2. Open storage slot 0
    ESP_LOGI("APP", "Step 2: Opening storage slot 0");
    bridge.StorageOpen(0);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 3. Wait 3 seconds
    ESP_LOGI("APP", "Step 3: Waiting 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 4. Close storage slot 0
    ESP_LOGI("APP", "Step 4: Closing storage slot 0");
    bridge.StorageClose(0);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 5. Rotate right
    ESP_LOGI("APP", "Step 5: Rotating right 90 degrees");
    bridge.VehicleMoveTime(DIR_ROTATE_RIGHT, 50, 1000);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 6. Move backward
    ESP_LOGI("APP", "Step 6: Moving backward 300mm");
    bridge.VehicleMoveDistance(DIR_BACKWARD, 40, 300);
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // 7. Stop
    ESP_LOGI("APP", "Step 7: Stopping vehicle");
    bridge.VehicleStop();
    
    ESP_LOGI("APP", "Demo complete! Total status updates: %d", status_update_count);
    
    // Keep running to receive status updates
    ESP_LOGI("APP", "Monitoring status...");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    
    // Cleanup (không bao giờ đến đây trong ví dụ này)
    bridge.StopStatusPolling();
    bridge.Deinit();
}
```

## Notes

- Protocol sử dụng JSON ngắn gọn với các key 1-2 ký tự để tiết kiệm bandwidth
- Luôn kiểm tra `IsSlaveOnline()` trước khi gửi lệnh để tránh timeout
- Timeout mặc định là 100ms cho mỗi lệnh I2C
- Response được sanitize để loại bỏ ký tự không hợp lệ
- Có thể sử dụng chung I2C bus với display (recommended)
- **Status callback chạy trong FreeRTOS task riêng**, không block main thread
- Interval polling có thể điều chỉnh từ 100ms đến vài giây tùy nhu cầu
- Callback function phải xử lý nhanh, không được block lâu

## Status Response Format

Status JSON từ actuator có format:
```json
{
  "s": 1,           // status: 1=OK, -1=ERROR
  "b": 12.4,        // battery voltage
  "c": 1,           // BLE connected: 1=yes, 0=no
  "h": 75,          // heart rate (bpm)
  "m": 1,           // motor enabled: 1=yes, 0=no
  "v": 0,           // moving: 1=yes, 0=no
  "g": [            // storage array
    {"i":0, "o":0}, // slot 0: closed
    {"i":1, "o":1}, // slot 1: open
    {"i":2, "o":0}, // slot 2: closed
    {"i":3, "o":0}  // slot 3: closed
  ]
}
```

## ActuatorStatus Struct

```cpp
struct ActuatorStatus {
    int status;           // STATUS_OK (1) or STATUS_ERROR (-1)
    float battery;        // Điện áp pin (V)
    bool ble_connected;   // BLE kết nối với đồng hồ
    int heart_rate;       // Nhịp tim từ BLE
    bool motor_enabled;   // Motor đang được enable
    bool is_moving;       // Xe đang di chuyển
    
    struct {
        int slot;         // Số ô (0-3)
        bool is_open;     // true = mở, false = đóng
    } storage[4];
};
```

## API Summary

### Vehicle Control
- `VehicleMoveTime(direction, speed_percent, duration_ms)` - Di chuyển theo thời gian
- `VehicleMoveDistance(direction, speed_percent, distance_mm)` - Di chuyển theo khoảng cách
- `VehicleStop()` - Dừng xe

### Storage Control
- `StorageOpen(slot)` - Mở cửa tủ
- `StorageClose(slot)` - Đóng cửa tủ
- `StorageControl(slot, action)` - Điều khiển trực tiếp

### Status & Monitoring
- `GetStatus()` - Lấy status một lần (trả về JSON string)
- `SetStatusCallback(callback, user_data)` - Đăng ký callback
- `StartStatusPolling(interval_ms)` - Bắt đầu polling tự động
- `StopStatusPolling()` - Dừng polling
- `IsPollingActive()` - Kiểm tra polling có đang chạy không
- `IsSlaveOnline()` - Ping test slave

### Legacy API (tương thích code cũ)
- `SendVehicleCommand(direction_string, speed, duration_ms)`
- `SendStorageCommand(slot, action_string)`
