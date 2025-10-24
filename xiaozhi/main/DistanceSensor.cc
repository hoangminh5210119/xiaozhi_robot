#include "DistanceSensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdlib>

const char* DistanceSensor::TAG = "DistanceSensor";

DistanceSensor::DistanceSensor(uart_port_t uart_port, int rx_pin, int tx_pin)
    : uart_port_(uart_port), rx_pin_(rx_pin), tx_pin_(tx_pin),
      initialized_(false), reading_(false), current_distance_(-1.0f),
      obstacle_threshold_(30.0f), vibrate_on_close_(false), read_task_handle_(nullptr) {
}

DistanceSensor::~DistanceSensor() {
    Deinit();
}

bool DistanceSensor::Init(int baudrate) {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    // Cấu hình UART
    uart_config_t uart_config = {
        .baud_rate = baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t err = uart_param_config(uart_port_, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
        return false;
    }

    // Thiết lập pins
    err = uart_set_pin(uart_port_, tx_pin_, rx_pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        return false;
    }

    // Cài đặt driver
    const int uart_buffer_size = 1024;
    err = uart_driver_install(uart_port_, uart_buffer_size, uart_buffer_size, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Distance sensor initialized (UART%d, RX=%d, TX=%d, baud=%d)", 
             uart_port_, rx_pin_, tx_pin_, baudrate);
    
    return true;
}

void DistanceSensor::Deinit() {
    if (!initialized_) return;

    StopReading();
    
    uart_driver_delete(uart_port_);
    initialized_ = false;
    
    ESP_LOGI(TAG, "Distance sensor deinitialized");
}

void DistanceSensor::StartReading() {
    if (!initialized_ || reading_) return;

    reading_ = true;
    
    xTaskCreate(UartReadTask, "distance_read", 4096, this, 5, &read_task_handle_);
    ESP_LOGI(TAG, "Started distance reading task");
}

void DistanceSensor::StopReading() {
    if (!reading_) return;

    reading_ = false;
    
    if (read_task_handle_) {
        vTaskDelete(read_task_handle_);
        read_task_handle_ = nullptr;
    }
    
    ESP_LOGI(TAG, "Stopped distance reading task");
}

void DistanceSensor::SetObstacleThreshold(float threshold_cm) {
    obstacle_threshold_ = threshold_cm;
    ESP_LOGI(TAG, "Obstacle threshold set to %.1f cm", threshold_cm);
}

void DistanceSensor::SetObstacleCallback(ObstacleCallback callback) {
    obstacle_callback_ = callback;
}

float DistanceSensor::GetCurrentDistance() {
    return current_distance_;
}

bool DistanceSensor::HasObstacle() {
    return (current_distance_ > 0 && current_distance_ < obstacle_threshold_);
}

void DistanceSensor::SetVibrateOnClose(bool enabled) {
    vibrate_on_close_ = enabled;
    ESP_LOGI(TAG, "Vibrate on close obstacle: %s", enabled ? "enabled" : "disabled");
}

void DistanceSensor::SetVibrateCallback(std::function<void(int times)> vibrate_callback) {
    vibrate_callback_ = vibrate_callback;
}

void DistanceSensor::UartReadTask(void* param) {
    DistanceSensor* sensor = static_cast<DistanceSensor*>(param);
    sensor->ProcessUartData();
}

void DistanceSensor::ProcessUartData() {
    const int buffer_size = 1024;
    
    while (reading_) {
        uint8_t *data = (uint8_t *)malloc(buffer_size);
        int len = uart_read_bytes(uart_port_, data, buffer_size, pdMS_TO_TICKS(100));
        
        if (len == 8) {
            // Kiểm tra header (format của cảm biến laser distance)
            if (data[0] != 0x5A) {
                free(data);
                continue;
            }

            // Tính checksum
            uint8_t sum = 0;
            for (int i = 0; i < 7; i++) {
                sum += data[i];
            }

            if (sum == data[7]) {
                // Parse distance từ bytes 4 và 5
                uint16_t distance_mm = ((uint16_t)data[4] << 8) | data[5];
                uint8_t info = data[6];
                uint8_t RangeStatus = (info >> 4) & 0x0F;

                if (RangeStatus != 2) { // 2 = Out of Range
                    // Convert mm sang cm
                    current_distance_ = distance_mm / 10.0f;
                    
                    // Kiểm tra rung khi vật cản gần (< 1m)
                    if (vibrate_on_close_ && distance_mm < 1000 && vibrate_callback_) {
                        vibrate_callback_(1); // Rung 1 lần
                    }
                    
                    // Kiểm tra vật cản và gọi callback
                    if (HasObstacle() && obstacle_callback_) {
                        obstacle_callback_(current_distance_);
                    }
                    
                    ESP_LOGD(TAG, "Distance: %.1f cm (raw: %d mm)", current_distance_, distance_mm);
                } else {
                    current_distance_ = -1.0f; // Out of range
                }
            }
            
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        
        free(data);
    }
    
    vTaskDelete(NULL);
}

float DistanceSensor::ParseDistanceFromUart(const char* data, size_t len) {
    // Hàm này không còn dùng nữa vì đã xử lý trực tiếp trong ProcessUartData
    // Giữ lại để tương thích
    return -1.0f;
}