# I2C Command Bridge - API Reference

## Tổng quan

`I2CCommandBridge` là thư viện giao tiếp I2C giữa ESP32 chính (Xiaozhi) và ESP32 phụ (Actuator). Sử dụng protocol JSON compact để điều khiển xe và tủ đồ, đồng thời monitor status real-time qua callback.

## Khởi tạo

### Constructor
```cpp
I2CCommandBridge bridge;
```

### Init methods
```cpp
bool Init();
bool InitWithExistingBus(i2c_master_bus_handle_t existing_bus_handle);
void Deinit();
```

## Vehicle Control API

### VehicleMoveTime
Di chuyển theo thời gian
```cpp
std::string VehicleMoveTime(int direction, int speed_percent, int duration_ms);
```
**Parameters:**
- `direction`: DIR_FORWARD (1), DIR_BACKWARD (2), DIR_LEFT (3), DIR_RIGHT (4), DIR_ROTATE_LEFT (5), DIR_ROTATE_RIGHT (6), DIR_STOP (0)
- `speed_percent`: 0-100%
- `duration_ms`: Thời gian (milliseconds)

**Example:**
```cpp
bridge.VehicleMoveTime(DIR_FORWARD, 50, 2000); // Forward 50% trong 2 giây
```

### VehicleMoveDistance
Di chuyển theo khoảng cách
```cpp
std::string VehicleMoveDistance(int direction, int speed_percent, int distance_mm);
```
**Parameters:**
- `direction`: DIR_FORWARD (1) hoặc DIR_BACKWARD (2)
- `speed_percent`: 0-100%
- `distance_mm`: Khoảng cách (millimeters)

**Example:**
```cpp
bridge.VehicleMoveDistance(DIR_FORWARD, 50, 500); // Tiến 500mm
```

### VehicleStop
Dừng xe ngay lập tức
```cpp
std::string VehicleStop();
```

**Example:**
```cpp
bridge.VehicleStop();
```

## Storage Control API

### StorageOpen
Mở cửa tủ
```cpp
std::string StorageOpen(int slot);
```
**Parameters:**
- `slot`: Số ô (0-3)

**Example:**
```cpp
bridge.StorageOpen(0); // Mở ô 0
```

### StorageClose
Đóng cửa tủ
```cpp
std::string StorageClose(int slot);
```
**Parameters:**
- `slot`: Số ô (0-3)

**Example:**
```cpp
bridge.StorageClose(1); // Đóng ô 1
```

### StorageControl
Điều khiển cửa tủ (low-level)
```cpp
std::string StorageControl(int slot, int action);
```
**Parameters:**
- `slot`: Số ô (0-3)
- `action`: ACT_OPEN (1) hoặc ACT_CLOSE (0)

**Example:**
```cpp
bridge.StorageControl(2, ACT_OPEN);
```

## Status & Monitoring API

### GetStatus
Lấy status một lần (synchronous)
```cpp
std::string GetStatus();
```
**Returns:** JSON string

**Example:**
```cpp
std::string status_json = bridge.GetStatus();
// Parse JSON manually if needed
```

### SetStatusCallback
Đăng ký callback để nhận status tự động
```cpp
void SetStatusCallback(ActuatorStatusCallback callback, void* user_data = nullptr);
```
**Parameters:**
- `callback`: Function pointer với signature `void callback(const ActuatorStatus& status, void* user_data)`
- `user_data`: Optional pointer để pass custom data

**Example:**
```cpp
void onStatus(const ActuatorStatus& status, void* user_data) {
    ESP_LOGI("APP", "Battery: %.1fV", status.battery);
}

bridge.SetStatusCallback(onStatus);
```

### StartStatusPolling
Bắt đầu polling status tự động
```cpp
bool StartStatusPolling(uint32_t interval_ms = 1000);
```
**Parameters:**
- `interval_ms`: Khoảng thời gian giữa các lần polling (milliseconds), mặc định 1000ms

**Returns:** `true` nếu thành công

**Example:**
```cpp
bridge.StartStatusPolling(2000); // Poll mỗi 2 giây
```

### StopStatusPolling
Dừng polling status
```cpp
void StopStatusPolling();
```

**Example:**
```cpp
bridge.StopStatusPolling();
```

### IsPollingActive
Kiểm tra polling có đang chạy không
```cpp
bool IsPollingActive() const;
```

**Example:**
```cpp
if (bridge.IsPollingActive()) {
    ESP_LOGI(TAG, "Polling is running");
}
```

### IsSlaveOnline
Ping test để check slave có online không
```cpp
bool IsSlaveOnline();
```

**Example:**
```cpp
if (!bridge.IsSlaveOnline()) {
    ESP_LOGW(TAG, "Slave offline!");
}
```

## Data Structures

### ActuatorStatus
```cpp
struct ActuatorStatus {
    int status;           // STATUS_OK (1) or STATUS_ERROR (-1)
    float battery;        // Điện áp pin (V)
    bool ble_connected;   // BLE kết nối với đồng hồ
    int heart_rate;       // Nhịp tim từ BLE (bpm)
    bool motor_enabled;   // Motor đang được enable
    bool is_moving;       // Xe đang di chuyển
    
    struct {
        int slot;         // Số ô (0-3)
        bool is_open;     // true = mở, false = đóng
    } storage[4];
};
```

### ActuatorStatusCallback
```cpp
typedef void (*ActuatorStatusCallback)(const ActuatorStatus& status, void* user_data);
```

## Constants

### Direction Constants
```cpp
#define DIR_STOP            0    // Dừng
#define DIR_FORWARD         1    // Tiến
#define DIR_BACKWARD        2    // Lùi
#define DIR_LEFT            3    // Rẽ trái (strafe)
#define DIR_RIGHT           4    // Rẽ phải (strafe)
#define DIR_ROTATE_LEFT     5    // Quay trái tại chỗ
#define DIR_ROTATE_RIGHT    6    // Quay phải tại chỗ
```

### Action Constants
```cpp
#define ACT_CLOSE       0    // Đóng
#define ACT_OPEN        1    // Mở
```

### Status Constants
```cpp
#define STATUS_OK       1    // OK
#define STATUS_ERROR   -1    // Error
#define STATUS_UNKNOWN -2    // Unknown
```

## Legacy API (Backward Compatibility)

### SendVehicleCommand
```cpp
std::string SendVehicleCommand(const std::string& direction, int speed, int duration_ms);
```
**Direction strings:** "forward", "backward", "left", "right", "rotate_left", "rotate_right", "stop"

**Example:**
```cpp
bridge.SendVehicleCommand("forward", 50, 2000);
```

### SendStorageCommand
```cpp
std::string SendStorageCommand(int slot, const std::string& action);
```
**Action strings:** "open", "close"

**Example:**
```cpp
bridge.SendStorageCommand(0, "open");
```

## Protocol Details

### Command Format (Master → Slave)

**Vehicle (time-based):**
```json
{"t":"v","d":1,"p":50,"ms":2000}
```

**Vehicle (distance-based):**
```json
{"t":"v","d":1,"p":50,"mm":500}
```

**Vehicle (stop):**
```json
{"t":"v","d":0}
```

**Storage:**
```json
{"t":"s","i":0,"a":1}
```

**Status request:**
```json
{"t":"t"}
```

### Response Format (Slave → Master)

**Command response:**
```json
{"s":1}
```

**Status response:**
```json
{
  "s": 1,
  "b": 12.4,
  "c": 1,
  "h": 75,
  "m": 1,
  "v": 0,
  "g": [
    {"i":0, "o":0},
    {"i":1, "o":1},
    {"i":2, "o":0},
    {"i":3, "o":0}
  ]
}
```

**Key mapping:**
- `t` = type
- `d` = direction
- `p` = power/speed
- `ms` = milliseconds
- `mm` = millimeters
- `i` = index/slot
- `a` = action
- `s` = status
- `b` = battery
- `c` = connection (BLE)
- `h` = heart rate
- `m` = motor enabled
- `v` = vehicle moving
- `g` = storage array
- `o` = open state

## Error Handling

Tất cả các command functions đều trả về JSON string. Check error bằng cách:

```cpp
std::string resp = bridge.VehicleMoveTime(DIR_FORWARD, 50, 2000);

if (resp.find("error") != std::string::npos) {
    ESP_LOGE(TAG, "Command failed: %s", resp.c_str());
    
    if (resp.find("slave_offline") != std::string::npos) {
        ESP_LOGW(TAG, "Slave is offline");
    }
}
```

## Performance Notes

- I2C clock: 100kHz
- Timeout: 100ms per command
- Polling task priority: 5
- Polling task stack: 4KB
- JSON buffer size: 128 bytes (receive), 256 bytes (status)
- Response sanitization: Yes (non-printable chars removed)

## Thread Safety

- Callback chạy trong FreeRTOS task riêng
- Không nên call I2C functions trong callback
- Sử dụng queue pattern nếu cần xử lý phức tạp trong callback

## Complete Example

```cpp
#include "I2CCommandBridge.h"

I2CCommandBridge bridge;

void onStatus(const ActuatorStatus& status, void* user_data) {
    ESP_LOGI("APP", "Battery: %.1fV, HR: %d, Moving: %d",
             status.battery, status.heart_rate, status.is_moving);
}

extern "C" void app_main() {
    // Init
    bridge.Init();
    
    // Setup callback
    bridge.SetStatusCallback(onStatus);
    bridge.StartStatusPolling(1000);
    
    // Commands
    bridge.VehicleMoveDistance(DIR_FORWARD, 50, 500);
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    bridge.StorageOpen(0);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    bridge.StorageClose(0);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    bridge.VehicleStop();
    
    // Keep running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

## See Also

- `I2CCommandBridge_Example.md` - Detailed examples
- `I2CCommandBridge_Callback_Examples.md` - Callback patterns
- `cmd.md` - Protocol specification
