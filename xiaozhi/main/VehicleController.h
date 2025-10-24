#pragma once

#include "I2CCommandBridge.h"
#include "DistanceSensor.h"
#include <string>
#include <vector>
#include <functional>

/**
 * @brief Vehicle Controller
 * 
 * Điều khiển xe thông minh với:
 * - Chuỗi lệnh phức tạp
 * - Tránh vật cản
 * - Di chuyển theo khoảng cách chính xác
 */
class VehicleController {
public:
    struct MoveCommand {
        std::string direction;  // forward, backward, left, right, rotate_left, rotate_right
        int speed;             // 0-100
        int distance_mm;       // Khoảng cách (mm), 0 = mặc định
        int duration_ms;       // Thời gian (ms), 0 = theo distance
        bool stop_on_obstacle; // Dừng khi gặp vật cản
        
        MoveCommand(const std::string& dir, int spd = 50, int dist_mm = 0, 
                   int dur_ms = 0, bool stop_obstacle = false)
            : direction(dir), speed(spd), distance_mm(dist_mm), 
              duration_ms(dur_ms), stop_on_obstacle(stop_obstacle) {}
    };

    typedef std::function<void(const std::string& status)> StatusCallback;

    VehicleController(I2CCommandBridge* i2c_bridge, DistanceSensor* distance_sensor = nullptr);
    ~VehicleController();

    /**
     * @brief Thực hiện lệnh di chuyển đơn
     */
    bool ExecuteMove(const MoveCommand& cmd);

    /**
     * @brief Thực hiện chuỗi lệnh di chuyển
     */
    bool ExecuteSequence(const std::vector<MoveCommand>& commands);

    /**
     * @brief Di chuyển mặc định (0.5m)
     */
    bool MoveDefault(const std::string& direction, int speed = 50);

    /**
     * @brief Di chuyển cho đến khi gặp vật cản
     */
    bool MoveUntilObstacle(const std::string& direction, int speed = 30);

    /**
     * @brief Dừng xe ngay lập tức
     */
    bool Stop();

    /**
     * @brief Parse lệnh từ text tự nhiên
     * Ví dụ: "đi tới 1m rẽ phải đi thẳng 500mm sang trái"
     */
    std::vector<MoveCommand> ParseNaturalCommand(const std::string& text);

    /**
     * @brief Đặt callback trạng thái
     */
    void SetStatusCallback(StatusCallback callback);

    /**
     * @brief Kiểm tra trạng thái xe
     */
    std::string GetVehicleStatus();

private:
    I2CCommandBridge* i2c_bridge_;
    DistanceSensor* distance_sensor_;
    StatusCallback status_callback_;
    bool is_moving_;

    void NotifyStatus(const std::string& status);
    int ParseDistance(const std::string& text, size_t& pos);
    std::string ParseDirection(const std::string& text, size_t& pos);

    static const char* TAG;
};