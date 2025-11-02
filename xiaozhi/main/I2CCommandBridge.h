#pragma once

#include "cJSON.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>
#include <vector>

// I2C Configuration (shared with display)
#define I2C_MASTER_SCL_IO                                                      \
  41 /*!< GPIO number for I2C master clock (shared with display) */
#define I2C_MASTER_SDA_IO                                                      \
  42 /*!< GPIO number for I2C master data (shared with display) */
#define I2C_MASTER_FREQ_HZ 100000 /*!< I2C master clock frequency */
#define I2C_MASTER_TIMEOUT_MS 1000

// I2C Slave Address
#define ACTUATOR_ESP32_ADDR 0x55 /*!< ESP32 actuator I2C address */

// Command Types (short keys for compact JSON)
#define KEY_TYPE "t"      // type
#define KEY_DIR "d"       // direction
#define KEY_SPEED "p"     // power/speed
#define KEY_DURATION "ms" // milliseconds
#define KEY_DISTANCE "mm" // millimeters
#define KEY_SLOT "i"      // slot index
#define KEY_ACTION "a"    // action

// Type values
#define TYPE_VEHICLE "v"
#define TYPE_STORAGE "s"
#define TYPE_STATUS "t"

// Direction values
#define DIR_STOP 0
#define DIR_FORWARD 1
#define DIR_BACKWARD 2
#define DIR_LEFT 3
#define DIR_RIGHT 4
#define DIR_ROTATE_LEFT 5
#define DIR_ROTATE_RIGHT 6

// Action values
#define ACT_CLOSE 0
#define ACT_OPEN 1

/**
 * @brief Status data structure từ actuator
 */
struct ActuatorStatus {
  int status;         // STATUS_OK (1) or STATUS_ERROR (-1)
  float battery;      // Điện áp pin (V)
  bool ble_connected; // BLE kết nối với đồng hồ
  int heart_rate;     // Nhịp tim từ BLE
  bool motor_enabled; // Motor đang được enable
  bool is_moving;     // Xe đang di chuyển

  // Storage states (4 slots)
  struct {
    int slot;     // Số ô (0-3)
    bool is_open; // true = mở, false = đóng
  } storage[4];
};

/**
 * @brief Callback function để nhận status từ actuator
 * @param status Struct chứa thông tin trạng thái
 * @param user_data Con trỏ dữ liệu user (optional)
 */
typedef void (*ActuatorStatusCallback)(const ActuatorStatus &status,
                                       void *user_data);

/**
 * @brief I2C Command Bridge
 *
 * Gửi lệnh JSON qua I2C từ ESP32 chính (Xiaozhi) sang ESP32 phụ (actuator)
 *
 * Protocol (compact JSON):
 * Vehicle (time-based):
 *   {"t":"v","d":1,"p":50,"ms":2000}  - Forward 50% speed for 2s
 *   {"t":"v","d":2,"p":60,"ms":1000}  - Backward 60% speed for 1s
 *   {"t":"v","d":3,"p":40,"ms":1500}  - Left 40% speed for 1.5s
 *   {"t":"v","d":4,"p":40,"ms":1500}  - Right 40% speed for 1.5s
 *   {"t":"v","d":5,"p":50,"ms":2000}  - Rotate left 50% for 2s
 *   {"t":"v","d":6,"p":50,"ms":2000}  - Rotate right 50% for 2s
 *   {"t":"v","d":0}                   - Stop
 *
 * Vehicle (distance-based):
 *   {"t":"v","d":1,"p":50,"mm":500}   - Forward 500mm at 50% speed
 *   {"t":"v","d":2,"p":40,"mm":300}   - Backward 300mm at 40% speed
 *
 * Storage:
 *   {"t":"s","i":0,"a":1}             - Open slot 0
 *   {"t":"s","i":0,"a":0}             - Close slot 0
 *   {"t":"s","i":1,"a":1}             - Open slot 1
 *
 * Status:
 *   {"t":"t"}                         - Get status
 *
 * Response format:
 *   {"s":1}                           - Status OK
 *   {"s":-1}                          - Status Error
 */
class I2CCommandBridge {
public:
  // Singleton pattern - chỉ cho phép 1 instance duy nhất
  static I2CCommandBridge &GetInstance() {
    static I2CCommandBridge instance;
    return instance;
  }
  
  // Delete copy constructor và assignment operator để ngăn copy
  I2CCommandBridge(const I2CCommandBridge&) = delete;
  I2CCommandBridge& operator=(const I2CCommandBridge&) = delete;
  
  ~I2CCommandBridge();

private:
  // Constructor private để không thể tạo instance từ bên ngoài
  I2CCommandBridge();

public:

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

  // ==================== VEHICLE CONTROL ====================

  /**
   * @brief Di chuyển theo thời gian
   * @param direction DIR_FORWARD, DIR_BACKWARD, DIR_LEFT, DIR_RIGHT,
   * DIR_ROTATE_LEFT, DIR_ROTATE_RIGHT, DIR_STOP
   * @param speed_percent Tốc độ 0-100%
   * @param duration_ms Thời gian (ms), 0 = dừng
   * @return JSON response
   */
  std::string VehicleMoveTime(int direction, int speed_percent,
                              int duration_ms);

  /**
   * @brief Di chuyển theo khoảng cách
   * @param direction DIR_FORWARD hoặc DIR_BACKWARD
   * @param speed_percent Tốc độ 0-100%
   * @param distance_mm Khoảng cách (mm)
   * @return JSON response
   */
  std::string VehicleMoveDistance(int direction, int speed_percent,
                                  int distance_mm);

  /**
   * @brief Dừng xe ngay lập tức
   * @return JSON response
   */
  std::string VehicleStop();

  // ==================== STORAGE CONTROL ====================

  /**
   * @brief Mở cửa tủ
   * @param slot Số ô (0-3)
   * @return JSON response
   */
  std::string StorageOpen(int slot);

  /**
   * @brief Đóng cửa tủ
   * @param slot Số ô (0-3)
   * @return JSON response
   */
  std::string StorageClose(int slot);

  /**
   * @brief Điều khiển cửa tủ
   * @param slot Số ô (0-3)
   * @param action ACT_OPEN hoặc ACT_CLOSE
   * @return JSON response
   */
  std::string StorageControl(int slot, int action);

  // ==================== STATUS ====================

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

  // ==================== STATUS CALLBACK ====================

  /**
   * @brief Đăng ký callback để nhận status từ actuator
   * @param callback Hàm callback sẽ được gọi khi nhận status
   * @param user_data Con trỏ dữ liệu user (optional)
   */
  void SetStatusCallback(ActuatorStatusCallback callback,
                         void *user_data = nullptr);

  /**
   * @brief Bắt đầu polling status từ actuator với interval
   * @param interval_ms Khoảng thời gian giữa các lần lấy status (ms)
   * @return true nếu khởi động thành công
   */
  bool StartStatusPolling(uint32_t interval_ms = 1000);

  /**
   * @brief Dừng polling status
   */
  void StopStatusPolling();

  /**
   * @brief Kiểm tra xem polling có đang chạy không
   * @return true nếu đang polling
   */
  bool IsPollingActive() const { return polling_active_; }

  // ==================== LEGACY COMPATIBILITY ====================
  // Các hàm tương thích với code cũ

  std::string SendVehicleCommand(const std::string &direction, int speed,
                                 int duration_ms);
  std::string SendStorageCommand(int slot, const std::string &action);

private:
  /**
   * @brief Gửi JSON command qua I2C
   * @param jsonCmd JSON object command
   * @return JSON response string
   */
  std::string SendI2CCommand(cJSON *jsonCmd);

  /**
   * @brief Parse JSON status response thành ActuatorStatus struct
   * @param jsonResponse JSON string từ actuator
   * @param status Output struct
   * @return true nếu parse thành công
   */
  bool ParseStatusResponse(const std::string &jsonResponse,
                           ActuatorStatus &status);

  /**
   * @brief FreeRTOS task để polling status
   */
  static void StatusPollingTask(void *param);

  bool initialized_;
  bool owns_bus_handle_;
  i2c_master_bus_handle_t bus_handle_;
  i2c_master_dev_handle_t dev_handle_;

  // Status polling
  bool polling_active_;
  uint32_t polling_interval_ms_;
  TaskHandle_t polling_task_handle_;
  ActuatorStatusCallback status_callback_;
  void *callback_user_data_;
};
