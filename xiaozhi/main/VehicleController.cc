#include "VehicleController.h"
#include <regex>
#include <algorithm>
#include <cctype>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

const char* VehicleController::TAG = "VehicleController";

VehicleController::VehicleController(I2CCommandBridge* i2c_bridge, DistanceSensor* distance_sensor)
    : i2c_bridge_(i2c_bridge), distance_sensor_(distance_sensor), is_moving_(false) {
    
    if (distance_sensor_) {
        // Đặt callback cho cảm biến khoảng cách
        distance_sensor_->SetObstacleCallback([this](float distance) {
            if (is_moving_) {
                ESP_LOGW(TAG, "Obstacle detected at %.1f cm, stopping vehicle", distance);
                Stop();
                NotifyStatus("Dừng xe do phát hiện vật cản");
            }
        });
    }
}

VehicleController::~VehicleController() {
}

bool VehicleController::ExecuteMove(const MoveCommand& cmd) {
    if (!i2c_bridge_) {
        ESP_LOGE(TAG, "I2C bridge not available");
        return false;
    }

    // ========== KIỂM TRA VẬT CẢN TRƯỚC KHI DI CHUYỂN ==========
    if (distance_sensor_ && (cmd.direction == "forward" || cmd.direction == "backward")) {
        if (distance_sensor_->HasObstacle()) {
            float dist = distance_sensor_->GetCurrentDistance();
            ESP_LOGW(TAG, "🛑 NGĂN CẢN di chuyển: Phát hiện vật cản ở %.1f cm", dist);
            NotifyStatus("Không thể di chuyển - Có vật cản ở phía " + 
                        (cmd.direction == "forward" ? "trước" : "sau"));
            return false;
        }
    }

    ESP_LOGI(TAG, "Executing move: %s, speed=%d, distance=%dmm, duration=%dms", 
             cmd.direction.c_str(), cmd.speed, cmd.distance_mm, cmd.duration_ms);

    NotifyStatus("Đang di chuyển " + cmd.direction);
    is_moving_ = true;

    std::string response;
    bool success = false;
    
    if (cmd.distance_mm > 0) {
        // Di chuyển theo khoảng cách
        response = i2c_bridge_->SendVehicleCommandDistance(cmd.direction, cmd.speed, cmd.distance_mm);
    } else if (cmd.stop_on_obstacle) {
        // Di chuyển cho đến khi gặp vật cản
        response = i2c_bridge_->SendVehicleUntilObstacle(cmd.direction, cmd.speed);
    } else {
        // Di chuyển theo thời gian
        int duration = cmd.duration_ms > 0 ? cmd.duration_ms : 1000; // Mặc định 1 giây
        response = i2c_bridge_->SendVehicleCommand(cmd.direction, cmd.speed, duration);
    }

    // ========== CRITICAL: Check response and stop if error ==========
    success = !response.empty() && response.find("error") == std::string::npos;
    
    if (!success) {
        ESP_LOGE(TAG, "⚠️ I2C error detected: %s", response.c_str());
        ESP_LOGI(TAG, "🛑 Sending EMERGENCY STOP command");
        
        // Gửi lệnh STOP ngay lập tức
        std::string stop_response = i2c_bridge_->SendVehicleCommand("stop", 0, 0);
        ESP_LOGI(TAG, "Stop response: %s", stop_response.c_str());
        
        is_moving_ = false;
        NotifyStatus("Lỗi I2C - Đã dừng khẩn cấp");
        return false;
    }

    is_moving_ = false;
    NotifyStatus("Hoàn thành di chuyển");

    return success;
}

bool VehicleController::ExecuteSequence(const std::vector<MoveCommand>& commands) {
    ESP_LOGI(TAG, "Executing sequence of %d commands", (int)commands.size());
    
    for (size_t i = 0; i < commands.size(); i++) {
        ESP_LOGI(TAG, "Command %d/%d: %s", (int)i+1, (int)commands.size(), 
                 commands[i].direction.c_str());
        
        if (!ExecuteMove(commands[i])) {
            ESP_LOGE(TAG, "Failed to execute command %d", (int)i+1);
            NotifyStatus("Lỗi thực hiện lệnh " + std::to_string(i+1));
            return false;
        }
        
        // Delay nhỏ giữa các lệnh
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    NotifyStatus("Hoàn thành chuỗi lệnh");
    return true;
}

bool VehicleController::MoveDefault(const std::string& direction, int speed) {
    MoveCommand cmd(direction, speed, 500); // 500mm = 0.5m
    return ExecuteMove(cmd);
}

bool VehicleController::MoveUntilObstacle(const std::string& direction, int speed) {
    MoveCommand cmd(direction, speed, 0, 0, true);
    return ExecuteMove(cmd);
}

bool VehicleController::Stop() {
    if (!i2c_bridge_) return false;
    
    ESP_LOGI(TAG, "Emergency stop");
    is_moving_ = false;
    
    std::string response = i2c_bridge_->SendVehicleCommand("stop", 0, 0);
    NotifyStatus("Dừng khẩn cấp");
    
    return !response.empty();
}

std::vector<VehicleController::MoveCommand> VehicleController::ParseNaturalCommand(const std::string& text) {
    std::vector<MoveCommand> commands;
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
    
    ESP_LOGI(TAG, "Parsing command: %s", text.c_str());
    
    // Các từ khóa hướng
    std::vector<std::pair<std::string, std::string>> direction_keywords = {
        {"đi tới", "forward"}, {"tiến", "forward"}, {"đi thẳng", "forward"},
        {"lùi", "backward"}, {"đi lùi", "backward"},
        {"rẽ trái", "rotate_left"}, {"xoay trái", "rotate_left"}, {"quẹo trái", "rotate_left"},
        {"rẽ phải", "rotate_right"}, {"xoay phải", "rotate_right"}, {"quẹo phải", "rotate_right"},
        {"sang trái", "left"}, {"trượt trái", "left"},
        {"sang phải", "right"}, {"trượt phải", "right"},
        {"dừng", "stop"}
    };
    
    size_t pos = 0;
    while (pos < lower_text.length()) {
        // Tìm hướng di chuyển
        std::string direction;
        size_t direction_pos = std::string::npos;
        
        for (const auto& keyword : direction_keywords) {
            size_t found = lower_text.find(keyword.first, pos);
            if (found != std::string::npos && (direction_pos == std::string::npos || found < direction_pos)) {
                direction = keyword.second;
                direction_pos = found;
            }
        }
        
        if (direction_pos == std::string::npos) break;
        
        // Di chuyển pos đến sau từ khóa
        pos = direction_pos;
        for (const auto& keyword : direction_keywords) {
            if (lower_text.substr(pos, keyword.first.length()) == keyword.first) {
                pos += keyword.first.length();
                break;
            }
        }
        
        // Tìm khoảng cách
        int distance_mm = 0;
        
        // Tìm số và đơn vị
        while (pos < lower_text.length() && std::isspace(lower_text[pos])) pos++;
        
        if (pos < lower_text.length() && std::isdigit(lower_text[pos])) {
            size_t num_start = pos;
            while (pos < lower_text.length() && (std::isdigit(lower_text[pos]) || lower_text[pos] == '.')) {
                pos++;
            }
            
            float distance = std::stof(lower_text.substr(num_start, pos - num_start));
            
            // Tìm đơn vị
            while (pos < lower_text.length() && std::isspace(lower_text[pos])) pos++;
            
            if (pos < lower_text.length()) {
                if (lower_text.substr(pos, 2) == "mm") {
                    distance_mm = (int)distance;
                    pos += 2;
                } else if (lower_text.substr(pos, 2) == "cm") {
                    distance_mm = (int)(distance * 10);
                    pos += 2;
                } else if (lower_text[pos] == 'm') {
                    distance_mm = (int)(distance * 1000);
                    pos += 1;
                }
            } else {
                // Không có đơn vị, mặc định là cm
                distance_mm = (int)(distance * 10);
            }
        }
        
        // Tạo lệnh
        if (direction == "stop") {
            commands.emplace_back("stop", 0, 0);
        } else if (distance_mm > 0) {
            commands.emplace_back(direction, 50, distance_mm);
        } else {
            // Không có khoảng cách, dùng mặc định 0.5m
            commands.emplace_back(direction, 50, 500);
        }
        
        ESP_LOGI(TAG, "Parsed: %s, distance=%dmm", direction.c_str(), distance_mm);
    }
    
    ESP_LOGI(TAG, "Parsed %d commands from text", (int)commands.size());
    return commands;
}

void VehicleController::SetStatusCallback(StatusCallback callback) {
    status_callback_ = callback;
}

std::string VehicleController::GetVehicleStatus() {
    if (!i2c_bridge_) return "{\"error\":\"no_i2c\"}";
    
    return i2c_bridge_->GetStatus();
}

void VehicleController::NotifyStatus(const std::string& status) {
    ESP_LOGI(TAG, "Status: %s", status.c_str());
    if (status_callback_) {
        status_callback_(status);
    }
}