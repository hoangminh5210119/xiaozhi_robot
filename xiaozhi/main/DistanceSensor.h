#pragma once

#include <functional>
#include "driver/uart.h"
#include "esp_log.h"

/**
 * @brief Distance Sensor Manager
 * 
 * Quản lý cảm biến khoảng cách để phát hiện vật cản
 * Hỗ trợ callback khi phát hiện người/vật trong tầm
 */
class DistanceSensor {
public:
    typedef std::function<void(float distance_cm)> ObstacleCallback;

    DistanceSensor(uart_port_t uart_port, int rx_pin, int tx_pin = UART_PIN_NO_CHANGE);
    ~DistanceSensor();

    /**
     * @brief Khởi tạo cảm biến
     * @param baudrate Tốc độ baud (mặc định 9600)
     * @return true nếu thành công
     */
    bool Init(int baudrate = 9600);

    /**
     * @brief Dọn dẹp tài nguyên
     */
    void Deinit();

    /**
     * @brief Bắt đầu đọc dữ liệu liên tục
     */
    void StartReading();

    /**
     * @brief Dừng đọc dữ liệu
     */
    void StopReading();

    /**
     * @brief Đặt ngưỡng phát hiện vật cản (cm)
     * @param threshold_cm Ngưỡng tính bằng cm
     */
    void SetObstacleThreshold(float threshold_cm);

    /**
     * @brief Đặt callback khi phát hiện vật cản
     * @param callback Hàm callback nhận khoảng cách
     */
    void SetObstacleCallback(ObstacleCallback callback);

    /**
     * @brief Lấy khoảng cách hiện tại
     * @return Khoảng cách tính bằng cm, -1 nếu lỗi
     */
    float GetCurrentDistance();

    /**
     * @brief Kiểm tra có vật cản trong tầm không
     * @return true nếu có vật cản
     */
    bool HasObstacle();

    /**
     * @brief Bật/tắt chế độ rung khi phát hiện vật cản gần (< 1m)
     * @param enabled true = bật rung cảnh báo
     */
    void SetVibrateOnClose(bool enabled);

    /**
     * @brief Đặt callback để điều khiển rung
     * @param vibrate_callback Hàm callback nhận số lần rung
     */
    void SetVibrateCallback(std::function<void(int times)> vibrate_callback);

private:
    static void UartReadTask(void* param);
    void ProcessUartData();
    float ParseDistanceFromUart(const char* data, size_t len);

    uart_port_t uart_port_;
    int rx_pin_;
    int tx_pin_;
    bool initialized_;
    bool reading_;
    float current_distance_;
    float obstacle_threshold_;
    ObstacleCallback obstacle_callback_;
    std::function<void(int times)> vibrate_callback_;
    bool vibrate_on_close_;
    TaskHandle_t read_task_handle_;
    
    static const char* TAG;
};