# Sử dụng I2C Command Bridge trong CompactWifiBoardS3Cam

## Tổng quan

Board `CompactWifiBoardS3Cam` đã được tích hợp `I2CCommandBridge` để điều khiển actuator ESP32 và nhận status real-time.

## Tính năng đã tích hợp

### 1. Auto-initialize I2C Bridge
- Tự động khởi tạo khi board start
- Polling status mỗi 2 giây
- Callback tự động log status info

### 2. Button Control
- **Boot Button**: Toggle chat state (giữ nguyên chức năng cũ)
- **Function Button**: Test command - di chuyển xe tiến 500mm

### 3. Status Monitoring
- Tự động nhận status từ actuator
- Log thông tin: battery, heart rate, BLE connection, moving state, storage states
- Có thể extend để hiển thị lên màn hình

## Cách sử dụng trong Application

### 1. Lấy I2C Bridge instance

```cpp
#include "application.h"

void my_function() {
    auto& app = Application::GetInstance();
    auto* board = dynamic_cast<CompactWifiBoardS3Cam*>(app.GetBoard());
    
    if (board) {
        I2CCommandBridge* bridge = board->GetI2CBridge();
        
        // Use bridge
        bridge->VehicleMoveDistance(DIR_FORWARD, 50, 500);
    }
}
```

### 2. Điều khiển xe

```cpp
void controlVehicle() {
    auto* bridge = GetBoardI2CBridge(); // Helper function
    
    if (!bridge->IsSlaveOnline()) {
        ESP_LOGW(TAG, "Actuator offline");
        return;
    }
    
    // Move forward 1 meter at 60% speed
    bridge->VehicleMoveDistance(DIR_FORWARD, 60, 1000);
    
    // Wait a bit
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Rotate right
    bridge->VehicleMoveTime(DIR_ROTATE_RIGHT, 50, 1000);
    
    // Stop
    bridge->VehicleStop();
}
```

### 3. Điều khiển storage

```cpp
void controlStorage() {
    auto* bridge = GetBoardI2CBridge();
    
    // Open slot 0
    bridge->StorageOpen(0);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Close slot 0
    bridge->StorageClose(0);
}
```

### 4. Đọc status

```cpp
void checkStatus() {
    auto& app = Application::GetInstance();
    auto* board = dynamic_cast<CompactWifiBoardS3Cam*>(app.GetBoard());
    
    if (board && board->IsStatusUpdated()) {
        const ActuatorStatus& status = board->GetLastStatus();
        
        ESP_LOGI(TAG, "Battery: %.1fV", status.battery);
        ESP_LOGI(TAG, "Heart Rate: %d", status.heart_rate);
        ESP_LOGI(TAG, "Moving: %d", status.is_moving);
        
        // Clear flag after reading
        board->ClearStatusFlag();
    }
}
```

## Extend: Hiển thị status trên màn hình

Bạn có thể implement `UpdateDisplayWithStatus()` để show status lên LCD:

```cpp
void CompactWifiBoardS3Cam::UpdateDisplayWithStatus(const ActuatorStatus& status) {
    if (!display_) return;
    
    // Example: Draw status info on display
    char buffer[64];
    
    // Battery
    snprintf(buffer, sizeof(buffer), "Bat: %.1fV", status.battery);
    // display_->DrawText(10, 10, buffer, color);
    
    // Heart Rate
    snprintf(buffer, sizeof(buffer), "HR: %d bpm", status.heart_rate);
    // display_->DrawText(10, 30, buffer, color);
    
    // Moving indicator
    if (status.is_moving) {
        // display_->DrawText(10, 50, "Moving...", RED);
    }
    
    // Storage icons
    for (int i = 0; i < 4; i++) {
        if (status.storage[i].is_open) {
            // display_->DrawRect(10 + i*20, 70, 15, 15, GREEN);
        }
    }
}
```

## Extend: Voice command integration

Tích hợp voice command để điều khiển:

```cpp
void handleVoiceCommand(const std::string& command) {
    auto* bridge = GetBoardI2CBridge();
    
    if (!bridge->IsSlaveOnline()) {
        ESP_LOGW(TAG, "Cannot execute - actuator offline");
        return;
    }
    
    if (command.find("tiến") != std::string::npos) {
        bridge->VehicleMoveDistance(DIR_FORWARD, 50, 500);
    }
    else if (command.find("lùi") != std::string::npos) {
        bridge->VehicleMoveDistance(DIR_BACKWARD, 50, 500);
    }
    else if (command.find("trái") != std::string::npos) {
        bridge->VehicleMoveTime(DIR_LEFT, 50, 1000);
    }
    else if (command.find("phải") != std::string::npos) {
        bridge->VehicleMoveTime(DIR_RIGHT, 50, 1000);
    }
    else if (command.find("quay trái") != std::string::npos) {
        bridge->VehicleMoveTime(DIR_ROTATE_LEFT, 50, 1000);
    }
    else if (command.find("quay phải") != std::string::npos) {
        bridge->VehicleMoveTime(DIR_ROTATE_RIGHT, 50, 1000);
    }
    else if (command.find("dừng") != std::string::npos) {
        bridge->VehicleStop();
    }
    else if (command.find("mở tủ") != std::string::npos) {
        // Parse slot number
        int slot = 0; // extract from command
        bridge->StorageOpen(slot);
    }
    else if (command.find("đóng tủ") != std::string::npos) {
        int slot = 0; // extract from command
        bridge->StorageClose(slot);
    }
}
```

## Extend: Auto health monitoring

Tự động theo dõi sức khỏe:

```cpp
void CompactWifiBoardS3Cam::OnActuatorStatusUpdate(const ActuatorStatus& status) {
    // ... existing code ...
    
    // Health monitoring
    if (status.battery < 11.0f) {
        ESP_LOGW(TAG, "🔋 LOW BATTERY WARNING!");
        // Play warning sound
        // Show warning on display
    }
    
    if (status.heart_rate > 120) {
        ESP_LOGW(TAG, "❤️ HIGH HEART RATE DETECTED!");
        // Auto stop vehicle
        i2c_bridge_.VehicleStop();
        // Auto open emergency storage
        i2c_bridge_.StorageOpen(0);
    }
    
    if (!status.ble_connected) {
        ESP_LOGW(TAG, "📡 BLE DISCONNECTED - Heart rate monitor offline");
    }
}
```

## Configuration

Các tham số có thể thay đổi:

```cpp
// Polling interval (default: 2000ms)
i2c_bridge_.StartStatusPolling(1000);  // Poll every 1 second

// Stop polling to save power
i2c_bridge_.StopStatusPolling();

// Restart with different interval
i2c_bridge_.StartStatusPolling(5000);  // Poll every 5 seconds
```

## Error Handling

```cpp
void safeVehicleCommand() {
    auto* bridge = GetBoardI2CBridge();
    
    // Always check slave online first
    if (!bridge->IsSlaveOnline()) {
        ESP_LOGW(TAG, "Actuator is offline - cannot execute command");
        return;
    }
    
    // Send command
    std::string resp = bridge->VehicleMoveDistance(DIR_FORWARD, 50, 500);
    
    // Check response
    if (resp.find("error") != std::string::npos) {
        ESP_LOGE(TAG, "Command failed: %s", resp.c_str());
        return;
    }
    
    ESP_LOGI(TAG, "Command sent successfully");
}
```

## Notes

- I2C bridge được khởi tạo tự động khi board start
- Status callback chạy trong task riêng, không block main thread
- Function button đã được map để test vehicle command
- Có thể extend để thêm nhiều chức năng control và monitoring
- Display update là optional - implement theo nhu cầu
- Polling interval mặc định 2s, có thể điều chỉnh

## API Reference

### Public Methods

```cpp
// Get I2C bridge instance
I2CCommandBridge* GetI2CBridge();

// Get last received status
const ActuatorStatus& GetLastStatus() const;

// Check if new status received
bool IsStatusUpdated() const;

// Clear status update flag
void ClearStatusFlag();
```

### Callback Override

```cpp
// Override để custom xử lý status
void OnActuatorStatusUpdate(const ActuatorStatus& status);

// Override để custom display update
void UpdateDisplayWithStatus(const ActuatorStatus& status);
```
