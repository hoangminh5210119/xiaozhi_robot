#pragma once

#include <string>
#include <vector>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "cJSON.h"

// I2C Configuration (shared with display)
#define I2C_MASTER_SCL_IO           42      /*!< GPIO number for I2C master clock (shared with display) */
#define I2C_MASTER_SDA_IO           41      /*!< GPIO number for I2C master data (shared with display) */
#define I2C_MASTER_FREQ_HZ          100000     /*!< I2C master clock frequency */
#define I2C_MASTER_TIMEOUT_MS       1000

// I2C Slave Address
#define ACTUATOR_ESP32_ADDR         0x55    /*!< ESP32 actuator I2C address */

// Command Types
#define CMD_TYPE_VEHICLE_MOVE       "vehicle.move"
#define CMD_TYPE_STORAGE_CONTROL    "storage.control"
#define CMD_TYPE_GET_STATUS         "status.get"

/**
 * @brief I2C Command Bridge
 * 
 * Gửi lệnh JSON qua I2C từ ESP32 chính (Xiaozhi) sang ESP32 phụ (actuator)
 * 
 * Protocol:
 * - Master (Xiaozhi) gửi JSON string qua I2C
 * - Slave (Actuator) nhận lệnh, thực thi, trả về JSON response
 * 
 * JSON Format (Request):
 * {
 *   "type": "vehicle.move",
 *   "direction": "forward",
 *   "speed": 50,
 *   "duration_ms": 1000
 * }
 * 
 * JSON Format (Response):
 * {
 *   "status": "ok",
 *   "message": "Moving forward"
 * }
 */
class I2CCommandBridge {
public:
    I2CCommandBridge();
    ~I2CCommandBridge();

    /**
     * @brief Khởi tạo I2C master với bus riêng
     */
    bool Init();

    /**
     * @brief Khởi tạo với I2C bus đã có sẵn (shared với display)
     * @param existing_bus_handle I2C bus handle đã được khởi tạo
     */
    bool InitWithExistingBus(i2c_master_bus_handle_t existing_bus_handle);

    /**
     * @brief Dọn dẹp I2C
     */
    void Deinit();

    /**
     * @brief Gửi lệnh điều khiển xe
     * @param direction forward/backward/left/right/rotate_left/rotate_right/stop
     * @param speed Tốc độ 0-100
     * @param duration_ms Thời gian di chuyển (ms), 0 = liên tục
     * @return JSON response string
     */
    std::string SendVehicleCommand(const std::string& direction, int speed, int duration_ms);

    /**
     * @brief Gửi lệnh điều khiển xe theo khoảng cách (mm)
     * @param direction forward/backward/left/right/rotate_left/rotate_right
     * @param speed Tốc độ 0-100
     * @param distance_mm Khoảng cách di chuyển (mm)
     * @return JSON response string
     */
    std::string SendVehicleCommandDistance(const std::string& direction, int speed, int distance_mm);

    /**
     * @brief Gửi chuỗi lệnh di chuyển phức tạp
     * @param commands Danh sách lệnh di chuyển
     * @return JSON response string
     */
    std::string SendVehicleSequence(const std::vector<std::string>& commands);

    /**
     * @brief Gửi lệnh di chuyển mặc định (0.5m)
     * @param direction Hướng di chuyển
     * @param speed Tốc độ (mặc định 50)
     * @return JSON response string
     */
    std::string SendVehicleDefaultMove(const std::string& direction, int speed = 50);

    /**
     * @brief Gửi lệnh di chuyển cho đến khi gặp vật cản
     * @param direction Hướng di chuyển
     * @param speed Tốc độ (mặc định 30)
     * @return JSON response string
     */
    std::string SendVehicleUntilObstacle(const std::string& direction, int speed = 30);

    /**
     * @brief Gửi lệnh điều khiển tủ đồ (storage)
     * @param slot Số ô (0-5, internal index)
     * @param action open/close/led_on/led_off/led_blink
     * @return JSON response string
     */
    std::string SendStorageCommand(int slot, const std::string& action);

    /**
     * @brief Lấy trạng thái từ ESP32 phụ
     * @return JSON status string
     */
    std::string GetStatus();
    
    /**
     * @brief Kiểm tra slave có online không (ping test)
     * @return true nếu slave phản hồi
     */
    bool IsSlaveOnline();

private:
    /**
     * @brief Gửi JSON command qua I2C
     * @param jsonCmd JSON object command
     * @return JSON response string
     */
    std::string SendI2CCommand(cJSON* jsonCmd);

    /**
     * @brief Gửi raw data qua I2C
     * @param data Con trỏ dữ liệu
     * @param size Kích thước dữ liệu
     * @return true nếu thành công
     */
    bool SendRawData(const uint8_t* data, size_t size);

    /**
     * @brief Nhận response từ slave
     * @param buffer Buffer để chứa dữ liệu
     * @param maxSize Kích thước tối đa
     * @return Số byte nhận được
     */
    int ReceiveResponse(uint8_t* buffer, size_t maxSize);

    bool initialized_;
    bool owns_bus_handle_;  // true if we created the bus, false if using shared bus
    i2c_master_bus_handle_t bus_handle_;
    i2c_master_dev_handle_t dev_handle_;
};
