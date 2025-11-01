# I2C Command Bridge - Callback Example

## Ví dụ đơn giản: Callback để monitor status

```cpp
#include "I2CCommandBridge.h"

I2CCommandBridge bridge;

// Callback function - sẽ được gọi mỗi khi nhận status từ actuator
void myStatusCallback(const ActuatorStatus& status, void* user_data) {
    // In thông tin cơ bản
    ESP_LOGI("STATUS", "Battery: %.1fV | HR: %d bpm | Moving: %s", 
             status.battery, 
             status.heart_rate,
             status.is_moving ? "YES" : "NO");
    
    // Cảnh báo nếu pin thấp
    if (status.battery < 11.0f) {
        ESP_LOGW("STATUS", "⚠️ Low battery!");
    }
    
    // In trạng thái BLE
    if (!status.ble_connected) {
        ESP_LOGW("STATUS", "❌ BLE disconnected");
    }
}

void setup() {
    // 1. Khởi tạo I2C bridge
    bridge.Init();
    
    // 2. Đăng ký callback
    bridge.SetStatusCallback(myStatusCallback);
    
    // 3. Bắt đầu polling mỗi 1 giây
    bridge.StartStatusPolling(1000);
}

void loop() {
    // Main loop có thể làm việc khác
    // Callback sẽ chạy tự động trong background
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

## Ví dụ với user data

```cpp
struct MyAppData {
    int total_status_received;
    float max_battery_seen;
    int max_heart_rate;
};

void advancedCallback(const ActuatorStatus& status, void* user_data) {
    MyAppData* app = (MyAppData*)user_data;
    
    // Đếm số lần nhận status
    app->total_status_received++;
    
    // Track max battery
    if (status.battery > app->max_battery_seen) {
        app->max_battery_seen = status.battery;
    }
    
    // Track max heart rate
    if (status.heart_rate > app->max_heart_rate) {
        app->max_heart_rate = status.heart_rate;
    }
    
    ESP_LOGI("STATS", "Total updates: %d | Max battery: %.1fV | Max HR: %d",
             app->total_status_received,
             app->max_battery_seen,
             app->max_heart_rate);
}

void setup() {
    MyAppData app_data = {0, 0.0f, 0};
    
    bridge.Init();
    bridge.SetStatusCallback(advancedCallback, &app_data);
    bridge.StartStatusPolling(2000); // Mỗi 2 giây
}
```

## Ví dụ: Tự động mở tủ khi nhịp tim cao

```cpp
void autoOpenStorageCallback(const ActuatorStatus& status, void* user_data) {
    I2CCommandBridge* bridge = (I2CCommandBridge*)user_data;
    
    // Nếu nhịp tim > 120 và xe đang không di chuyển
    if (status.heart_rate > 120 && !status.is_moving) {
        ESP_LOGW("AUTO", "🚨 High heart rate detected! Opening emergency storage...");
        
        // Mở tủ số 0 (emergency kit)
        bridge->StorageOpen(0);
    }
    
    // Nếu nhịp tim bình thường trở lại
    if (status.heart_rate < 100 && status.storage[0].is_open) {
        ESP_LOGI("AUTO", "✅ Heart rate normalized, closing storage...");
        bridge->StorageClose(0);
    }
}

void setup() {
    bridge.Init();
    
    // Pass bridge pointer để có thể gọi lệnh trong callback
    bridge.SetStatusCallback(autoOpenStorageCallback, &bridge);
    bridge.StartStatusPolling(500); // Check mỗi 0.5 giây
}
```

## Ví dụ: Monitor và log vào file

```cpp
#include <stdio.h>

FILE* log_file = nullptr;

void logToFileCallback(const ActuatorStatus& status, void* user_data) {
    if (!log_file) return;
    
    // Log timestamp, battery, heart rate
    fprintf(log_file, "%lu,%.1f,%d,%d\n", 
            millis(), 
            status.battery, 
            status.heart_rate,
            status.is_moving);
    
    fflush(log_file); // Đảm bảo ghi ngay
}

void setup() {
    // Mở file log
    log_file = fopen("/sdcard/status_log.csv", "w");
    if (log_file) {
        fprintf(log_file, "timestamp,battery,heart_rate,moving\n");
    }
    
    bridge.Init();
    bridge.SetStatusCallback(logToFileCallback);
    bridge.StartStatusPolling(1000);
}

void cleanup() {
    bridge.StopStatusPolling();
    if (log_file) {
        fclose(log_file);
    }
}
```

## Ví dụ: Display trên màn hình

```cpp
#include "display.h" // Your display library

void updateDisplayCallback(const ActuatorStatus& status, void* user_data) {
    // Clear display
    display_clear();
    
    // Line 1: Battery
    display_printf(0, 0, "Battery: %.1fV", status.battery);
    
    // Line 2: Heart rate
    display_printf(0, 1, "HR: %d bpm", status.heart_rate);
    
    // Line 3: Status
    display_printf(0, 2, "Moving: %s", status.is_moving ? "YES" : "NO");
    
    // Line 4: Storage status
    char storage_str[32];
    snprintf(storage_str, sizeof(storage_str), "Storage: %d%d%d%d",
             status.storage[0].is_open,
             status.storage[1].is_open,
             status.storage[2].is_open,
             status.storage[3].is_open);
    display_printf(0, 3, "%s", storage_str);
    
    // Update screen
    display_refresh();
}

void setup() {
    display_init();
    bridge.Init();
    bridge.SetStatusCallback(updateDisplayCallback);
    bridge.StartStatusPolling(500); // Update display mỗi 0.5s
}
```

## Lưu ý quan trọng

### ✅ DO (Nên làm)
- Callback nên xử lý nhanh (< 100ms)
- Sử dụng ESP_LOG để debug
- Check null pointer khi dùng user_data
- Dùng user_data để pass context/state

### ❌ DON'T (Không nên)
- Không call `vTaskDelay()` trong callback (sẽ block polling task)
- Không thực hiện I/O nặng (write file lớn, network...)
- Không call `StartStatusPolling()` hoặc `StopStatusPolling()` trong callback
- Không sử dụng blocking operations

### 💡 Tips
- Nếu cần xử lý lâu, tạo queue và xử lý trong task riêng
- Callback chạy trong FreeRTOS task ưu tiên 5
- Có thể thay đổi polling interval bằng cách stop và start lại
- Nếu không cần callback nữa, call `StopStatusPolling()` để tiết kiệm tài nguyên

## Example: Queue pattern cho xử lý phức tạp

```cpp
#include "freertos/queue.h"

QueueHandle_t status_queue;

void queueCallback(const ActuatorStatus& status, void* user_data) {
    // Copy status vào queue (non-blocking)
    xQueueSend(status_queue, &status, 0);
}

void processing_task(void* param) {
    ActuatorStatus status;
    
    while (true) {
        // Wait for status from queue
        if (xQueueReceive(status_queue, &status, portMAX_DELAY)) {
            // Xử lý phức tạp ở đây (có thể mất thời gian)
            ESP_LOGI("PROC", "Processing status...");
            
            // Analyze data
            // Save to database
            // Send to cloud
            // etc.
            
            vTaskDelay(pdMS_TO_TICKS(100)); // OK here, not in callback
        }
    }
}

void setup() {
    // Create queue
    status_queue = xQueueCreate(10, sizeof(ActuatorStatus));
    
    // Create processing task
    xTaskCreate(processing_task, "StatusProc", 4096, NULL, 4, NULL);
    
    // Setup callback
    bridge.Init();
    bridge.SetStatusCallback(queueCallback);
    bridge.StartStatusPolling(1000);
}
```

Với pattern này, callback chỉ copy data vào queue rất nhanh, và task riêng xử lý chi tiết không ảnh hưởng đến polling.
