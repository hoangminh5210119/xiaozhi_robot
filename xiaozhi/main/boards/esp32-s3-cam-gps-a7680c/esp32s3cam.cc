#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/oled_display.h"
#include "dual_network_board.h"
#include "esp32_camera.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "nmea_parser.h"
#include "system_reset.h"
#include "wifi_board.h"
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_spiffs.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include <wifi_station.h>
#include "telegram_manager.h" // Add this include

#include "RecurringSchedule.h"
#include "StorageManager.h"
#include "I2CCommandBridge.h"
#include "VehicleController.h"
#include "DistanceSensor.h"

#define TAG "esp32-s3-cam-gps-a7680c"

class Esp32S3CamGpsA7680cBoard : public DualNetworkBoard {
private:
  i2c_master_bus_handle_t display_i2c_bus_;
  esp_lcd_panel_io_handle_t panel_io_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;
  Display *display_ = nullptr;
  Button capture_button_;
  Button mode_button_;
  Button send_button_;

  Esp32Camera *camera_;
  
  // Vehicle control system
  DistanceSensor *distance_sensor_;
  VehicleController *vehicle_controller_;

  // RecurringSchedule *scheduler_;

  void MountStorage() {
    // Mount partition "storage" riêng cho lưu trữ dữ liệu người dùng
    esp_vfs_spiffs_conf_t storage_conf = {
        .base_path = "/storage",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true, // Có thể format nếu cần
    };
    esp_err_t ret = esp_vfs_spiffs_register(&storage_conf);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to mount storage partition (%s)",
               esp_err_to_name(ret));
    } else {
      ESP_LOGI(TAG, "Storage partition mounted at /storage");
    }
  }

  void InitializeDisplayI2c() {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = (i2c_port_t)0,
        .sda_io_num = DISPLAY_SDA_PIN,
        .scl_io_num = DISPLAY_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags =
            {
                .enable_internal_pullup = 1,
            },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
  }

  void InitializeCamera() {
    camera_config_t config = {};
    config.pin_d0 = CAMERA_PIN_D0;
    config.pin_d1 = CAMERA_PIN_D1;
    config.pin_d2 = CAMERA_PIN_D2;
    config.pin_d3 = CAMERA_PIN_D3;
    config.pin_d4 = CAMERA_PIN_D4;
    config.pin_d5 = CAMERA_PIN_D5;
    config.pin_d6 = CAMERA_PIN_D6;
    config.pin_d7 = CAMERA_PIN_D7;
    config.pin_xclk = CAMERA_PIN_XCLK;
    config.pin_pclk = CAMERA_PIN_PCLK;
    config.pin_vsync = CAMERA_PIN_VSYNC;
    config.pin_href = CAMERA_PIN_HREF;
    config.pin_sccb_sda = CAMERA_PIN_SIOD;
    config.pin_sccb_scl = CAMERA_PIN_SIOC;
    config.sccb_i2c_port = 0;
    config.pin_pwdn = CAMERA_PIN_PWDN;
    config.pin_reset = CAMERA_PIN_RESET;
    config.xclk_freq_hz = XCLK_FREQ_HZ;
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    camera_ = new Esp32Camera(config);
    camera_->SetHMirror(false);
    camera_->SetVFlip(true);
  }

  void InitializeSsd1306Display() {
    // SSD1306 config
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = 0x3C,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags =
            {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
        .scl_speed_hz = 400 * 1000,
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

    ESP_LOGI(TAG, "Install SSD1306 driver");
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1;
    panel_config.bits_per_pixel = 1;

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
    };
    panel_config.vendor_config = &ssd1306_config;

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
    ESP_LOGI(TAG, "SSD1306 driver installed");

    // Reset the display
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    if (esp_lcd_panel_init(panel_) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize display");
      display_ = new NoDisplay();
      return;
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                               DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
  }

  void fetch_reverse_geocode(double lat, double lon) {
    char url[256];
    snprintf(
        url, sizeof(url),
        "https://nominatim.openstreetmap.org/reverse?format=json&lat=%f&lon=%f",
        lat, lon);

    esp_http_client_config_t config = {};
    config.url = url;
    config.cert_pem = NULL;
    config.method = HTTP_METHOD_GET;
    config.disable_auto_redirect = false;
    config.user_agent = "ESP32GeoApp/1.0 (contact: your_email@example.com)";

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
      ESP_LOGE(TAG, "Failed to init http client");
      return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
      int status = esp_http_client_get_status_code(client);
      int content_length = esp_http_client_get_content_length(client);
      ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d", status,
               content_length);

      char buffer[1024];
      int read_len =
          esp_http_client_read_response(client, buffer, sizeof(buffer) - 1);
      if (read_len >= 0) {
        buffer[read_len] = 0; // null terminate
        ESP_LOGI(TAG, "Response: %s", buffer);

        // parse JSON (ví dụ lấy địa chỉ display_name)
        cJSON *root = cJSON_Parse(buffer);
        if (root) {
          cJSON *display = cJSON_GetObjectItem(root, "display_name");
          if (cJSON_IsString(display)) {
            ESP_LOGI(TAG, "Location: %s", display->valuestring);
          }
          cJSON_Delete(root);
        }
      } else {
        ESP_LOGE(TAG, "Failed to read response");
      }
    } else {
      ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
  }

  void InitializeButtons() {
    capture_button_.OnClick([this]() {
      ESP_LOGI(TAG,
               "Capture button clicked - Switching network type (4G <-> WiFi)");
      auto &app = Application::GetInstance();
      // Chuyển đổi giữa 4G và WiFi
      app.Schedule([this]() {
        SwitchNetworkType();
        ESP_LOGI(TAG, "Network type switched successfully");
      });
    });

    mode_button_.OnClick([this]() {
      ESP_LOGI(TAG, "Mode button clicked - toggle chat state");
      auto &app = Application::GetInstance();
      app.Schedule([this]() {
        auto &app = Application::GetInstance();
        app.SendTextCommandToServer("hủy bỏ lịch 1");
        auto scheduler_ = &RecurringSchedule::GetInstance();
        scheduler_->removeSchedule(1);
      });
    });

    send_button_.OnClick([this]() {
      ESP_LOGI(TAG, "Send button clicked - toggle chat state");

      auto &app = Application::GetInstance();
      app.Schedule([this]() {
        auto &app = Application::GetInstance();
        app.ToggleChatState();
      });

      // toggle chat state
    });
  }

  void InitializeTools() {
    static LampController lamp(SOS_LED_PIN);
    auto &mcp = McpServer::GetInstance();
    auto &app = Application::GetInstance();
    auto &telegram = TelegramManager::GetInstance();
    auto &storage = StorageManager::GetInstance();
    auto &scheduler = RecurringSchedule::GetInstance();
    
    // Khởi tạo I2C Command Bridge với bus đã có sẵn (shared với display)
    static I2CCommandBridge i2cBridge;
    if (!i2cBridge.InitWithExistingBus(display_i2c_bus_)) {
      ESP_LOGW(TAG, "⚠️  I2C Command Bridge init failed, continuing without actuator control");
    }
    
    // Lưu pointer để tránh warning khi capture static variable trong lambda
    I2CCommandBridge* i2cBridgePtr = &i2cBridge;

    // Khởi tạo Distance Sensor (cảm biến khoảng cách)
    distance_sensor_ = new DistanceSensor(distance_uart_port, distance_uart_rx_pin, distance_uart_tx_pin);
    if (distance_sensor_->Init(distance_uart_baudrate)) {
      distance_sensor_->SetObstacleThreshold(30.0f); // 30cm threshold
      distance_sensor_->SetVibrateOnClose(is_enable_distance); // Kích hoạt rung theo biến toàn cục
      distance_sensor_->SetVibrateCallback([](int times) {
        // Callback để rung khi có vật cản gần
        vibrate(times);
      });
      distance_sensor_->StartReading();
      ESP_LOGI(TAG, "✅ Distance sensor initialized");
    } else {
      ESP_LOGW(TAG, "⚠️  Distance sensor init failed");
      delete distance_sensor_;
      distance_sensor_ = nullptr;
    }

    // Khởi tạo Vehicle Controller (điều khiển xe thông minh)
    vehicle_controller_ = new VehicleController(&i2cBridge, distance_sensor_);
    vehicle_controller_->SetStatusCallback([&app](const std::string& status) {
      ESP_LOGI(TAG, "🚗 Vehicle status: %s", status.c_str());
      // Có thể gửi trạng thái về cho user qua app
    });
    ESP_LOGI(TAG, "✅ Vehicle controller initialized");

    // ==================== VEHICLE CONTROL - DIRECT COMMANDS ====================
    // Thay vì dùng MCP tools, bây giờ dùng lệnh text tự nhiên qua AI

    mcp.AddTool(
        "vehicle.execute_command",
        "ĐIỀU KHIỂN XE bằng lệnh tự nhiên.\n"
        "Hỗ trợ chuỗi lệnh phức tạp như: 'đi tới 1m rẽ phải đi thẳng 500mm sang trái'\n"
        "Tính năng:\n"
        "- Di chuyển theo khoảng cách chính xác (mm, cm, m)\n"
        "- Chuỗi hành động liên tiếp\n"
        "- Tự động tránh vật cản\n"
        "- Mặc định di chuyển 0.5m nếu không chỉ định khoảng cách\n"
        "Ví dụ:\n"
        "- 'đi tới' → di chuyển 0.5m về phía trước\n"
        "- 'đi tới 2m' → di chuyển 2m về phía trước\n"
        "- 'rẽ trái sang phải 30cm' → rẽ trái rồi di chuyển sang phải 30cm\n"
        "- 'lùi 1m rẽ phải đi thẳng cho đến khi gặp người' → chuỗi lệnh phức tạp\n"
        "Tham số:\n"
        "- command (string): Lệnh điều khiển bằng tiếng Việt tự nhiên",
        PropertyList({Property("command", kPropertyTypeString)}),
        [this](const PropertyList &props) -> ReturnValue {
          if (!vehicle_controller_) {
            return "Lỗi: Hệ thống điều khiển xe chưa sẵn sàng";
          }
          
          std::string command = props["command"].value<std::string>();
          ESP_LOGI(TAG, "🚗 Executing vehicle command: %s", command.c_str());
          
          auto commands = vehicle_controller_->ParseNaturalCommand(command);
          if (commands.empty()) {
            return "Không hiểu lệnh điều khiển. Vui lòng thử lại với lệnh rõ ràng hơn.";
          }
          
          bool success = vehicle_controller_->ExecuteSequence(commands);
          return success ? "Thực hiện lệnh thành công" : "Lỗi khi thực hiện lệnh";
        });

    mcp.AddTool(
        "vehicle.stop",
        "DỪNG XE khẩn cấp.\n"
        "Dừng tất cả hoạt động di chuyển ngay lập tức.\n"
        "Dùng khi cần dừng khẩn cấp hoặc hủy bỏ lệnh đang thực hiện.",
        PropertyList(),
        [this](const PropertyList &) -> ReturnValue {
          if (!vehicle_controller_) {
            return "Lỗi: Hệ thống điều khiển xe chưa sẵn sàng";
          }
          
          ESP_LOGI(TAG, "🛑 Emergency stop requested");
          bool success = vehicle_controller_->Stop();
          return success ? "Đã dừng xe" : "Lỗi khi dừng xe";
        });

    mcp.AddTool(
        "vehicle.status",
        "KIỂM TRA TRẠNG THÁI xe và các thành phần.\n"
        "Trả về thông tin về pin, động cơ, cảm biến, và trạng thái hiện tại.",
        PropertyList(),
        [this](const PropertyList &) -> ReturnValue {
          if (!vehicle_controller_) {
            return "Lỗi: Hệ thống điều khiển xe chưa sẵn sàng";
          }
          
          std::string status = vehicle_controller_->GetVehicleStatus();
          
          // Thêm thông tin cảm biến khoảng cách
          if (distance_sensor_) {
            float distance = distance_sensor_->GetCurrentDistance();
            bool has_obstacle = distance_sensor_->HasObstacle();
            
            // Parse JSON và thêm thông tin cảm biến
            cJSON* json = cJSON_Parse(status.c_str());
            if (json) {
              cJSON_AddNumberToObject(json, "distance_cm", distance);
              cJSON_AddBoolToObject(json, "obstacle_detected", has_obstacle);
              
              char* updated_status = cJSON_Print(json);
              status = std::string(updated_status);
              cJSON_free(updated_status);
              cJSON_Delete(json);
            }
          }
          
          return status;
        });

    // ==================== STORAGE & SCHEDULE INITIALIZATION ====================
    
    // Khởi tạo RecurringSchedule với file path giống StorageManager
    scheduler.begin("/storage/schedule.json");
    scheduler.setCallback([&app](int id, const std::string &note) {
      ESP_LOGI(TAG, "⏰ Schedule triggered: id=%d, note=%s", id, note.c_str());
      app.SendTextCommandToServer(note);
    });

    // Khởi tạo StorageManager với 4 ô (0-3)
    // ⚠️ QUAN TRỌNG: addSlot TRƯỚC, begin() SAU để load dữ liệu vào slots
    // GPIO_NUM_1 dùng tạm cho test, chưa gắn LED thật
    storage.addSlot(0, GPIO_NUM_NC); // Ô 0
    storage.addSlot(1, GPIO_NUM_NC); // Ô 1
    storage.addSlot(2, GPIO_NUM_NC); // Ô 2
    storage.addSlot(3, GPIO_NUM_NC); // Ô 3

    // begin() sẽ auto-load dữ liệu từ file vào các slot đã add
    if (!storage.begin("/storage/storage.json")) {
      ESP_LOGW(TAG, "Failed to initialize storage, but continuing...");
    }

    // ==================== STORAGE MANAGER MCP TOOLS ====================
    mcp.AddTool(
        "storage.put_item",
        "LƯU/ĐẶT đồ vào một ô cụ thể trong tủ.\n"
        "Dùng khi: Người dùng nói 'tôi để [đồ vật] vào ô số [X]', 'cất [đồ] "
        "vào ngăn [X]'.\n"
        "⚠️ QUAN TRỌNG: Người dùng đếm từ 1-4, HỆ THỐNG đếm từ 0-3.\n"
        "Tham số:\n"
        "- slot (integer): Số ô THEO NGƯỜI DÙNG (1-4), hệ thống sẽ tự convert "
        "thành 0-3\n"
        "- item (string): Tên đồ vật (ví dụ: 'kính', 'chìa khóa', 'ví')\n"
        "Ví dụ: Người dùng: 'Tôi để kính vào ô số 1' → slot=1 → hệ thống lưu "
        "vào index 0\n"
        "Ví dụ: Người dùng: 'Cất ví vào ngăn 4' → slot=4 → hệ thống lưu vào "
        "index 3\n"
        "Trả về: true nếu thành công, false nếu ô không tồn tại",
        PropertyList({Property("slot", kPropertyTypeInteger, 1, 4),
                      Property("item", kPropertyTypeString)}),
        [&storage](const PropertyList &props) -> ReturnValue {
          int userSlot = props["slot"].value<int>();
          int internalSlot = userSlot - 1; // Convert 1-6 to 0-5
          std::string item = props["item"].value<std::string>();
          ESP_LOGI(TAG, "📦 Putting '%s' into slot %d (user: %d)", item.c_str(),
                   internalSlot, userSlot);
          return storage.putItem(internalSlot, item, true);
        });

    mcp.AddTool(
        "storage.take_item",
        "LẤY đồ ra khỏi một ô (đánh dấu ô trống).\n"
        "Dùng khi: Người dùng nói 'tôi lấy đồ ở ô [X] ra', 'lấy [đồ] ra'.\n"
        "⚠️ QUAN TRỌNG: Người dùng đếm từ 1-4, HỆ THỐNG đếm từ 0-3.\n"
        "Tham số:\n"
        "- slot (integer): Số ô THEO NGƯỜI DÙNG (1-4), hệ thống sẽ tự convert "
        "thành 0-3\n"
        "Trả về: true nếu thành công, false nếu ô không tồn tại",
        PropertyList({Property("slot", kPropertyTypeInteger, 1, 4)}),
        [&storage](const PropertyList &props) -> ReturnValue {
          int userSlot = props["slot"].value<int>();
          int internalSlot = userSlot - 1; // Convert 1-6 to 0-5
          ESP_LOGI(TAG, "📤 Taking item from slot %d (user: %d)", internalSlot,
                   userSlot);
          return storage.takeItem(internalSlot, true);
        });

    mcp.AddTool(
        "storage.find_item",
        "TÌM KIẾM đồ vật trong tủ, trả về danh sách các ô chứa đồ đó.\n"
        "Dùng khi: Người dùng hỏi '[đồ vật] của tôi ở đâu?', 'tìm [đồ]'.\n"
        "⚠️ QUAN TRỌNG: Trả về số ô THEO NGƯỜI DÙNG (1-4), đã convert từ hệ "
        "thống (0-3).\n"
        "Tham số:\n"
        "- item (string): Tên đồ vật cần tìm\n"
        "Ví dụ: 'Kính của tôi ở đâu?' → item='kính'\n"
        "Trả về: Danh sách số ô chứa đồ đó (JSON array). Nếu không tìm thấy "
        "→ []",
        PropertyList({Property("item", kPropertyTypeString)}),
        [&storage](const PropertyList &props) -> ReturnValue {
          std::string item = props["item"].value<std::string>();
          ESP_LOGI(TAG, "🔍 Finding item: %s", item.c_str());
          auto slots = storage.findItemByName(item);

          // Tạo JSON response với slot theo số người dùng (1-6)
          cJSON *root = cJSON_CreateObject();
          cJSON_AddStringToObject(root, "item", item.c_str());
          cJSON *slotsArray = cJSON_CreateArray();
          for (int internalSlot : slots) {
            int userSlot = internalSlot + 1; // Convert 0-5 to 1-6
            cJSON_AddItemToArray(slotsArray, cJSON_CreateNumber(userSlot));
          }
          cJSON_AddItemToObject(root, "slots", slotsArray);
          cJSON_AddNumberToObject(root, "count", slots.size());

          char *jsonStr = cJSON_Print(root);
          std::string result(jsonStr);
          cJSON_free(jsonStr);
          cJSON_Delete(root);

          return result;
        });

    mcp.AddTool(
        "storage.led",
        "ĐIỀU KHIỂN ĐÈN LED của một ô.\n"
        "⚠️ QUAN TRỌNG: Người dùng đếm từ 1-4, HỆ THỐNG đếm từ 0-3.\n"
        "Tham số:\n"
        "- slot (integer): Số ô THEO NGƯỜI DÙNG (1-4), hệ thống sẽ tự convert "
        "thành 0-3\n"
        "- action (string): 'on' = bật, 'off' = tắt, 'blink' = nhấp nháy\n"
        "- times (integer): Số lần nhấp nháy (chỉ dùng khi action='blink', mặc "
        "định 3)\n"
        "- delay_ms (integer): Độ trễ ms (chỉ dùng khi action='blink', mặc "
        "định 500)\n"
        "Trả về: true nếu thành công",
        PropertyList({Property("slot", kPropertyTypeInteger, 1, 4),
                      Property("action", kPropertyTypeString),
                      Property("times", kPropertyTypeInteger, 1, 10),
                      Property("delay_ms", kPropertyTypeInteger, 100, 2000)}),
        [&storage](const PropertyList &props) -> ReturnValue {
          int userSlot = props["slot"].value<int>();
          int internalSlot = userSlot - 1; // Convert 1-6 to 0-5
          std::string action = props["action"].value<std::string>();

          if (action == "on") {
            ESP_LOGI(TAG, "💡 Turning ON LED for slot %d (user: %d)",
                     internalSlot, userSlot);
            return storage.turnOnLED(internalSlot);
          } else if (action == "off") {
            ESP_LOGI(TAG, "🌑 Turning OFF LED for slot %d (user: %d)",
                     internalSlot, userSlot);
            return storage.turnOffLED(internalSlot);
          } else if (action == "blink") {
            int times = props["times"].value<int>();
            int delayMs = props["delay_ms"].value<int>();
            ESP_LOGI(TAG, "✨ Blinking LED for slot %d (user: %d), %d times",
                     internalSlot, userSlot, times);
            return storage.blinkLED(internalSlot, times, delayMs);
          }
          return false;
        });

    mcp.AddTool(
        "storage.door",
        "MỞ/ĐÓNG cửa tủ (gửi lệnh qua I2C sang ESP32 phụ).\n"
        "⚠️ QUAN TRỌNG: Người dùng đếm từ 1-4, HỆ THỐNG đếm từ 0-3.\n"
        "Dùng khi: Người dùng nói 'mở tủ số [X]', 'đóng ngăn [X]'.\n"
        "Tham số:\n"
        "- slot (integer): Số ô THEO NGƯỜI DÙNG (1-4), hệ thống sẽ tự convert thành 0-3\n"
        "- action (string): 'open' = mở cửa, 'close' = đóng cửa\n"
        "Ví dụ:\n"
        "- 'Mở tủ số 1' → slot=1, action='open'\n"
        "- 'Đóng ngăn 3' → slot=3, action='close'\n"
        "Trả về: JSON response từ ESP32 phụ",
        PropertyList({
            Property("slot", kPropertyTypeInteger, 1, 4),
            Property("action", kPropertyTypeString)
        }),
        [i2cBridgePtr](const PropertyList &props) -> ReturnValue {
          int userSlot = props["slot"].value<int>();
          int internalSlot = userSlot - 1;  // Convert 1-6 to 0-5
          std::string action = props["action"].value<std::string>();
          
          ESP_LOGI(TAG, "🚪 Storage door: slot %d (user: %d), action=%s", 
                   internalSlot, userSlot, action.c_str());
          
          return i2cBridgePtr->SendStorageCommand(internalSlot, action);
        });

    mcp.AddTool(
        "storage.list_all",
        "XEM DANH SÁCH tất cả các ô và đồ vật trong tủ.\n"
        "Dùng khi: Người dùng hỏi 'tủ có gì?', 'xem tất cả đồ'.\n"
        "⚠️ QUAN TRỌNG: JSON trả về chứa index THEO NGƯỜI DÙNG (1-4).\n"
        "Không cần tham số.\n"
        "Trả về: Chuỗi JSON chứa thông tin tất cả ô (index, status, item, "
        "...)",
        PropertyList(), [&storage](const PropertyList &) -> ReturnValue {
          std::string json = storage.getSlotsJSON();
          ESP_LOGI(TAG, "📋 Listing all storage slots %s", json.c_str());
          return json;
        });

    mcp.AddTool(
        "storage.get_info",
        "LẤY THÔNG TIN tổng quan về tủ (số ô trống, số ô có đồ, tổng số ô).\n"
        "Dùng khi: Cần biết tình trạng tủ.\n"
        "Không cần tham số.\n"
        "Trả về: JSON object chứa total, empty, occupied",
        PropertyList(), [&storage](const PropertyList &) -> ReturnValue {
          cJSON *root = cJSON_CreateObject();
          cJSON_AddNumberToObject(root, "total", storage.getTotalSlotCount());
          cJSON_AddNumberToObject(root, "empty", storage.getEmptySlotCount());
          cJSON_AddNumberToObject(root, "occupied",
                                  storage.getOccupiedSlotCount());

          char *jsonStr = cJSON_Print(root);
          std::string result(jsonStr);
          cJSON_free(jsonStr);
          cJSON_Delete(root);

          ESP_LOGI(TAG, "ℹ️ Storage info: %s", result.c_str());
          return result;
        });

    // ==================== RECURRING SCHEDULE MCP TOOLS ====================

    mcp.AddTool(
        "schedule.add_once_delay",
        "⏰ NHẮC SAU MỘT KHOẢNG THỜI GIAN (tính từ bây giờ).\n"
        "Dùng khi: User nói 'nhắc tôi SAU 30 giây', 'đặt lịch 5 PHÚT NỮA', '10 "
        "giây sau hãy...'.\n"
        "❗ QUAN TRỌNG: Tham số delay_seconds là SỐ GIÂY TƯƠNG ĐỐI từ bây giờ, "
        "KHÔNG phải timestamp.\n"
        "Ví dụ:\n"
        "- 'nhắc sau 30 giây' → delay_seconds=30\n"
        "- 'nhắc sau 5 phút' → delay_seconds=300 (5*60)\n"
        "- 'nhắc sau 2 giờ' → delay_seconds=7200 (2*60*60)\n"
        "Tham số:\n"
        "- id (integer): ID duy nhất cho lịch\n"
        "- delay_seconds (integer): Số giây delay từ bây giờ (VD: 30, 300, "
        "7200)\n"
        "- note (string): Lệnh sẽ thực thi khi hết thời gian\n"
        "Trả về: true nếu thành công",
        PropertyList({Property("id", kPropertyTypeInteger),
                      Property("delay_seconds", kPropertyTypeInteger, 1,
                               86400), // 1 sec to 24 hours
                      Property("note", kPropertyTypeString)}),
        [&scheduler, &app](const PropertyList &props) -> ReturnValue {
          int id = props["id"].value<int>();
          int delay = props["delay_seconds"].value<int>();
          std::string note = props["note"].value<std::string>();

          ESP_LOGI(TAG,
                   "⏰ Adding delayed schedule: id=%d, delay=%d sec, note=%s",
                   id, delay, note.c_str());

          app.Schedule([&scheduler, id, delay, note]() {
            scheduler.addOnceAfterDelay(id, delay, note, true);
          });

          return true;
        });

    mcp.AddTool(
        "schedule.add_once",
        "LỊCH CHẠY 1 LẦN duy nhất vào giờ cụ thể trong ngày hôm nay.\n"
        "Dùng khi: User muốn nhắc 1 lần (ví dụ: 'nhắc họp lúc 3h chiều', 'bật "
        "đèn 8h tối').\n"
        "Tham số:\n"
        "- id (integer): ID duy nhất cho lịch\n"
        "- hour (integer): Giờ (0-23)\n"
        "- minute (integer): Phút (0-59)\n"
        "- note (string): Lệnh sẽ thực thi khi đến giờ\n"
        "Trả về: true nếu thành công",
        PropertyList({Property("id", kPropertyTypeInteger),
                      Property("hour", kPropertyTypeInteger, 0, 23),
                      Property("minute", kPropertyTypeInteger, 0, 59),
                      Property("note", kPropertyTypeString)}),
        [&scheduler, &app](const PropertyList &props) -> ReturnValue {
          int id = props["id"].value<int>();
          int hour = props["hour"].value<int>();
          int minute = props["minute"].value<int>();
          std::string note = props["note"].value<std::string>();

          ESP_LOGI(TAG, "⏰ Adding once schedule: id=%d, %02d:%02d, note=%s",
                   id, hour, minute, note.c_str());

          app.Schedule([&scheduler, id, hour, minute, note]() {
            scheduler.addOnceAtTime(id, hour, minute, note, true);
          });

          return true;
        });

    mcp.AddTool(
        "schedule.add_interval",
        "LỊCH LẶP LẠI theo khoảng thời gian đều đặn.\n"
        "Dùng khi: Lặp sau mỗi X phút/giờ (ví dụ: 'kiểm tra mỗi 30 phút', "
        "'tưới cây 2 giờ 1 lần').\n"
        "Tham số:\n"
        "- id (integer): ID duy nhất\n"
        "- interval_seconds (integer): Khoảng thời gian (giây). VD: 30 phút = "
        "1800, 1 giờ = 3600\n"
        "- note (string): Lệnh thực thi mỗi lần\n"
        "Trả về: true nếu thành công",
        PropertyList({Property("id", kPropertyTypeInteger),
                      Property("interval_seconds", kPropertyTypeInteger),
                      Property("note", kPropertyTypeString)}),
        [&scheduler, &app](const PropertyList &props) -> ReturnValue {
          int id = props["id"].value<int>();
          int sec = props["interval_seconds"].value<int>();
          std::string note = props["note"].value<std::string>();
          uint32_t interval = static_cast<uint32_t>(sec);

          ESP_LOGI(TAG, "⏰ Adding interval schedule: id=%d, every %d seconds",
                   id, sec);

          app.Schedule([&scheduler, id, interval, note]() {
            scheduler.addIntervalSchedule(id, interval, note, true);
          });

          return true;
        });

    mcp.AddTool(
        "schedule.add_daily",
        "LỊCH HÀNG NGÀY vào cùng giờ mỗi ngày.\n"
        "Dùng khi: Lặp cùng giờ mỗi ngày (ví dụ: 'báo thức 6h30 sáng', 'tắt "
        "đèn 10h tối').\n"
        "Tham số:\n"
        "- id (integer): ID duy nhất\n"
        "- hour (integer): Giờ (0-23)\n"
        "- minute (integer): Phút (0-59)\n"
        "- note (string): Lệnh thực thi hàng ngày\n"
        "Trả về: true nếu thành công",
        PropertyList({Property("id", kPropertyTypeInteger),
                      Property("hour", kPropertyTypeInteger, 0, 23),
                      Property("minute", kPropertyTypeInteger, 0, 59),
                      Property("note", kPropertyTypeString)}),
        [&scheduler, &app](const PropertyList &props) -> ReturnValue {
          int id = props["id"].value<int>();
          int hour = props["hour"].value<int>();
          int minute = props["minute"].value<int>();
          std::string note = props["note"].value<std::string>();

          std::vector<RecurringSchedule::DailyTime> times;
          times.emplace_back(hour, minute);

          ESP_LOGI(TAG, "⏰ Adding daily schedule: id=%d, %02d:%02d daily", id,
                   hour, minute);

          app.Schedule([&scheduler, id, times, note]() {
            scheduler.addDailySchedule(id, times, note, true);
          });

          return true;
        });

    mcp.AddTool(
        "schedule.add_weekly",
        "LỊCH HÀNG TUẦN vào ngày và giờ cố định.\n"
        "Dùng khi: Lặp vào ngày cụ thể trong tuần (ví dụ: 'họp mỗi thứ 2 lúc "
        "9h').\n"
        "Tham số:\n"
        "- id (integer): ID duy nhất\n"
        "- weekday (string): MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, "
        "SATURDAY, SUNDAY\n"
        "- hour (integer): Giờ (0-23)\n"
        "- minute (integer): Phút (0-59)\n"
        "- note (string): Lệnh thực thi hàng tuần\n"
        "Trả về: true nếu thành công",
        PropertyList({Property("id", kPropertyTypeInteger),
                      Property("weekday", kPropertyTypeString),
                      Property("hour", kPropertyTypeInteger, 0, 23),
                      Property("minute", kPropertyTypeInteger, 0, 59),
                      Property("note", kPropertyTypeString)}),
        [&scheduler, &app](const PropertyList &props) -> ReturnValue {
          int id = props["id"].value<int>();
          std::string wd = props["weekday"].value<std::string>();
          int hour = props["hour"].value<int>();
          int minute = props["minute"].value<int>();
          std::string note = props["note"].value<std::string>();

          RecurringSchedule::WeekDay day =
              (wd == "MONDAY")      ? RecurringSchedule::MONDAY
              : (wd == "TUESDAY")   ? RecurringSchedule::TUESDAY
              : (wd == "WEDNESDAY") ? RecurringSchedule::WEDNESDAY
              : (wd == "THURSDAY")  ? RecurringSchedule::THURSDAY
              : (wd == "FRIDAY")    ? RecurringSchedule::FRIDAY
              : (wd == "SATURDAY")  ? RecurringSchedule::SATURDAY
                                    : RecurringSchedule::SUNDAY;

          std::vector<RecurringSchedule::WeeklyTime> times;
          times.emplace_back(day, hour, minute);

          ESP_LOGI(TAG, "⏰ Adding weekly schedule: id=%d, %s %02d:%02d", id,
                   wd.c_str(), hour, minute);

          app.Schedule([&scheduler, id, times, note]() {
            scheduler.addWeeklySchedule(id, times, note, true);
          });

          return true;
        });

    mcp.AddTool(
        "schedule.remove",
        "XÓA một lịch theo ID.\n"
        "Dùng khi: User muốn hủy lịch.\n"
        "⚠️ LƯU Ý: PHẢI gọi 'schedule.list' trước để biết ID, và XÁC NHẬN "
        "với user!\n"
        "Tham số:\n"
        "- id (integer): ID lịch cần xóa\n"
        "Trả về: true nếu thành công",
        PropertyList({Property("id", kPropertyTypeInteger)}),
        [&scheduler, &app](const PropertyList &props) -> ReturnValue {
          int id = props["id"].value<int>();

          ESP_LOGI(TAG, "🗑️ Removing schedule: id=%d", id);

          app.Schedule(
              [&scheduler, id]() { scheduler.removeSchedule(id, true); });

          return true;
        });

    mcp.AddTool(
        "schedule.list",
        "XEM DANH SÁCH tất cả lịch đang có.\n"
        "Dùng khi: User hỏi 'có lịch nào', 'xem lịch', hoặc trước khi xóa.\n"
        "Không cần tham số.\n"
        "Trả về: JSON chứa thông tin tất cả lịch",
        PropertyList(), [&scheduler](const PropertyList &) -> ReturnValue {
          ESP_LOGI(TAG, "📋 Listing all schedules");
          return scheduler.getSchedulesJSON();
        });

    // ==================== END MCP TOOLS ====================

    // Light controls
    mcp.AddTool("light.power",
                "Kiểm tra trạng thái nguồn điện của hệ thống đèn LED, trả về "
                "true nếu đèn đang hoạt động bình thường",
                PropertyList(),
                [this](const PropertyList &) -> ReturnValue { return true; });

    mcp.AddTool("light.sos",
                "Điều khiển chế độ đèn SOS khẩn cấp.\n"
                "Tham số:\n"
                "- action (string): 'on' = bật SOS, 'off' = tắt SOS, 'status' "
                "= kiểm tra trạng thái\n"
                "Trả về: true/false tùy action",
                PropertyList({Property("action", kPropertyTypeString)}),
                [this](const PropertyList &props) -> ReturnValue {
                  std::string action = props["action"].value<std::string>();
                  if (action == "on") {
                    is_sos_led_on = true;
                    return true;
                  } else if (action == "off") {
                    is_sos_led_on = false;
                    return false;
                  } else if (action == "status") {
                    return is_sos_led_on;
                  }
                  return false;
                });

    // Distance sensor
    mcp.AddTool("sensor.distance",
                "Điều khiển cảm biến khoảng cách dò đường.\n"
                "Tham số:\n"
                "- action (string): 'on' = bật cảm biến rung khi có vật cản gần (<1m), "
                "'off' = tắt rung, 'status' = kiểm tra trạng thái\n"
                "Trả về: true/false tùy action",
                PropertyList({Property("action", kPropertyTypeString)}),
                [this](const PropertyList &props) -> ReturnValue {
                  std::string action = props["action"].value<std::string>();
                  if (action == "on") {
                    is_enable_distance = true;
                    if (distance_sensor_) {
                      distance_sensor_->SetVibrateOnClose(true);
                    }
                    return true;
                  } else if (action == "off") {
                    is_enable_distance = false;
                    if (distance_sensor_) {
                      distance_sensor_->SetVibrateOnClose(false);
                    }
                    return false;
                  } else if (action == "status") {
                    if (distance_sensor_) {
                      float distance = distance_sensor_->GetCurrentDistance();
                      bool has_obstacle = distance_sensor_->HasObstacle();
                      
                      cJSON* json = cJSON_CreateObject();
                      cJSON_AddBoolToObject(json, "enabled", is_enable_distance);
                      cJSON_AddNumberToObject(json, "distance_cm", distance);
                      cJSON_AddBoolToObject(json, "has_obstacle", has_obstacle);
                      
                      char* jsonStr = cJSON_Print(json);
                      std::string result(jsonStr);
                      cJSON_free(jsonStr);
                      cJSON_Delete(json);
                      
                      return result;
                    }
                    return is_enable_distance;
                  }
                  return false;
                });

    // System functions
    // mcp.AddTool(
    //     "system.gps",
    //     "Lấy vị trí GPS hiện tại của thiết bị và tạo link Google Maps để xem "
    //     "vị trí. "
    //     "Trả về đường link dạng http://maps.google.com/?q=lat,lon có thể mở "
    //     "trên trình duyệt. "
    //     "Nếu tọa độ lat/lon = 0 nghĩa là chưa bắt được tín hiệu GPS, khuyên "
    //     "người dùng ra khu vực thông thoáng. "
    //     "Dùng để xác định vị trí hiện tại, chia sẻ địa điểm hoặc tìm đường",
    //     PropertyList(), [this](const PropertyList &) -> ReturnValue {
    //       latitude = 10.036935;
    //       longitude = 105.761735;
    //       sprintf(gps_link, "http://maps.google.com/?q=%.05f,%.05f", latitude,
    //               longitude);
    //       return gps_link;
    //     });

    // mcp.AddTool("system.wifi_reset",
    //             "Khởi động lại thiết bị và vào chế độ cấu hình WiFi để kết nối "
    //             "mạng mới. "
    //             "**CẢNH BÁO**: Hành động này sẽ ngắt kết nối hiện tại và yêu "
    //             "cầu cấu hình lại WiFi. "
    //             "Chỉ sử dụng khi cần thay đổi mạng WiFi hoặc khắc phục sự cố "
    //             "kết nối. Cần xác nhận từ người dùng",
    //             PropertyList(), [](const PropertyList &) -> ReturnValue {
    //               ESP_LOGW(TAG, "WiFi reset requested");
    //               // SwitchNetworkType();
    //               return true;
    //             });

    // // Telegram functions
    // mcp.AddTool("msg.check",
    //             "Kiểm tra và đọc tin nhắn mới từ Telegram bot. Trả về nội dung "
    //             "JSON chứa các tin nhắn chưa đọc "
    //             "bao gồm người gửi, thời gian, nội dung tin nhắn. Nếu có tin "
    //             "nhắn mới sẽ đọc lần lượt từng tin. "
    //             "Dùng để nhận thông báo, tin nhắn từ người thân hoặc hệ thống "
    //             "giám sát từ xa",
    //             PropertyList(), [&app](const PropertyList &) -> ReturnValue {
    //               return app.GetTelegramMsgBufferAsJson();
    //             });

    // mcp.AddTool(
    //     "msg.send",
    //     "Gửi tin nhắn text qua Telegram bot đến chat/group đã cấu hình. "
    //     "Tham số msg: nội dung tin nhắn cần gửi (hỗ trợ tiếng Việt và "
    //     "emoji). "
    //     "Dùng để báo cáo tình trạng, gửi thông báo khẩn cấp, hoặc liên lạc "
    //     "với "
    //     "người thân. "
    //     "Tin nhắn sẽ được gửi ngay lập tức nếu có kết nối internet",
    //     PropertyList({Property("msg", kPropertyTypeString)}),
    //     [&app](const PropertyList &props) -> ReturnValue {
    //       app.SendTelegramMessage(props["msg"].value<std::string>());
    //       return true;
    //     });

    // Camera functions (if available)
    if (camera_) {
      mcp.AddTool(
          "camera.photo",
          "Chụp ảnh bằng camera tích hợp và tự động gửi qua Telegram bot. "
          "Không cần tham số đầu vào, hệ thống sẽ tự động chụp, nén và gửi "
          "ảnh "
          "với chất lượng tối ưu. "
          "Sử dụng khi người dùng yêu cầu chụp ảnh gửi cho người thân, ghi "
          "lại "
          "sự kiện, "
          "hoặc chia sẻ hình ảnh môi trường xung quanh. Cần kết nối internet "
          "để gửi ảnh",
          PropertyList(),
          [this, &telegram](const PropertyList &) -> ReturnValue {
            // if (!camera_->Capture())
            //   return false;

            // TelegramPhotoInfo info;
            // auto config = telegram.GetConfig();

            // if (!config.chat_id.empty() && !config.bot_token.empty()) {
            //   info = {config.bot_token, config.chat_id, "", ""};
            // } else {
            //   info = {"7354122596:AAFVA3MTxPZwCwmDPJNBvbchMe3ZlHvyjqU",
            //           "7799806969", "", ""};
            // }

            // return camera_->SendPhotoToTelegram(info);

            auto &app = Application::GetInstance();

            app.Schedule([this]() {
              if (!camera_->Capture()) {
                throw std::runtime_error("Failed to capture photo");
              }

              ESP_LOGI(TAG, "Captured photo, sending to Telegram...");
              // auto question = properties["question"].value<std::string>();
              TelegramPhotoInfo info;

              // Load configuration from TelegramManager if parameters are
              // empty
              auto &telegram_manager = TelegramManager::GetInstance();
              auto config = telegram_manager.GetConfig();

              info.caption = "";
              info.parse_mode = ""; // hoặc "" nếu không dùng

              if (!config.chat_id.empty() && !config.bot_token.empty()) {
                ESP_LOGI(TAG, "Loaded bot token from TelegramManager");
                info.bot_token = config.bot_token;
                info.chat_id = config.chat_id;
              } else {
                ESP_LOGW(TAG,
                         "Telegram bot not configured, using default test bot");
                info.bot_token =
                    "7354122596:AAFVA3MTxPZwCwmDPJNBvbchMe3ZlHvyjqU";
                info.chat_id = "7799806969"; // group/supergroup
              }
              return camera_->SendPhotoToTelegram(info);
            });

            return true;
          });

      mcp.AddTool(
          "camera.analyze",
          "Chụp ảnh và phân tích nội dung bằng AI để trả lời câu hỏi về hình "
          "ảnh. "
          "Tham số question: câu hỏi về những gì muốn biết trong ảnh (VD: "
          "'có "
          "gì trong ảnh?', 'đây là mệnh giá tiền bao nhiêu?', 'đọc chữ trong "
          "ảnh'). "
          "AI có thể nhận diện vật thể, đọc văn bản, nhận diện tiền tệ, mô "
          "tả "
          "cảnh vật, đếm số lượng đồ vật. "
          "Hữu ích cho người khiếm thị để 'nhìn' và hiểu môi trường xung "
          "quanh",
          PropertyList({Property("question", kPropertyTypeString)}),
          [this](const PropertyList &props) -> ReturnValue {
            if (!camera_->Capture())
              return "Lỗi chụp ảnh";
            return camera_->Explain(props["question"].value<std::string>());
          });
    }
  }

  static void gps_event_handler(void *event_handler_arg,
                                esp_event_base_t event_base, int32_t event_id,
                                void *event_data) {
    gps_t *gps = NULL;
    switch (event_id) {
    case GPS_UPDATE:
      gps = (gps_t *)event_data;

      latitude = gps->latitude;
      longitude = gps->longitude;

      if (latitude != last_latitude && longitude != last_longitude &&
          latitude > 0 && longitude > 0) {
        last_latitude = latitude;
        last_longitude = longitude;
      } else {
        latitude = last_latitude;
        longitude = last_longitude;
      }
      // 10.036935, 105.761735
      // latitude = 10.036935;
      // longitude = 105.761735;

      sprintf(gps_link, "http://maps.google.com/?q=%.05f,%.05f", latitude,
              longitude);

      if (latitude > 0 && longitude > 0 && !is_speak_gps) {
        // send_json_via_ws(ask_weather, gps_link);
        is_speak_gps = true;
        // start_play_mp3_task(mp3_gps);
      }

      // sprintf(gps_link, "http://maps.google.com/?q=%.05f,%.05f", latitude,
      //         longitude);
      // gps_link
      /* print information parsed from GPS statements */
      // ESP_LOGI(TAG,
      //          "%d/%d/%d %d:%d:%d => \r\n"
      //          "\t\t\t\t\t\tlatitude   = %.05f°N\r\n"
      //          "\t\t\t\t\t\tlongitude = %.05f°E\r\n"
      //          "\t\t\t\t\t\taltitude   = %.02fm\r\n"
      //          "\t\t\t\t\t\tspeed      = %fm/s",
      //          gps->date.year + YEAR_BASE, gps->date.month, gps->date.day,
      //          gps->tim.hour + TIME_ZONE, gps->tim.minute, gps->tim.second,
      //          gps->latitude, gps->longitude, gps->altitude, gps->speed);
      break;
    case GPS_UNKNOWN:
      /* print unknown statements */
      // ESP_LOGW(TAG, "%s", (char *)event_data);
      break;
    default:
      break;
    }
  }

  static void vibrate(int times) {
    for (int i = 0; i < times; i++) {
      gpio_set_level(VABRITE_PIN, 1);
      vTaskDelay(pdMS_TO_TICKS(300));
      gpio_set_level(VABRITE_PIN, 0);
      vTaskDelay(pdMS_TO_TICKS(300));
    }
  }

  void setup_gps() {
    /* NMEA parser configuration */
    nmea_parser_config_t config = NMEA_PARSER_CONFIG_DEFAULT();
    /* init NMEA parser library */
    nmea_parser_handle_t nmea_hdl = nmea_parser_init(&config);
    /* register event handler for NMEA parser library */
    nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);
    ESP_LOGI(TAG, "GPS initialized");
  }

  void blink(int times) {
    for (int i = 0; i < times; i++) {
      gpio_set_level(SOS_LED_PIN, 1);
      vTaskDelay(pdMS_TO_TICKS(200));
      gpio_set_level(SOS_LED_PIN, 0);
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }

  void setup_pins() {
    gpio_reset_pin(SOS_LED_PIN);
    gpio_set_direction(SOS_LED_PIN, GPIO_MODE_OUTPUT);
    blink(3);

    gpio_reset_pin(VABRITE_PIN);
    gpio_set_direction(VABRITE_PIN, GPIO_MODE_OUTPUT);
    vibrate(2);

    // gpio_set_level(VABRITE_PIN, 1);
  }

public:
  Esp32S3CamGpsA7680cBoard()
      : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, GPIO_NUM_NC, 1),
        capture_button_(CAPTURE_BUTTON_GPIO, false, 2000, 50, false),
        mode_button_(MODE_BUTTON_GPIO, false, 2000, 50, false),
        send_button_(SEND_BUTTON_GPIO, false, 2000, 50, false),
        distance_sensor_(nullptr), vehicle_controller_(nullptr) {
    MountStorage();
    InitializeDisplayI2c();
    InitializeSsd1306Display();
    InitializeButtons();
    InitializeTools();
    InitializeCamera();
    setup_gps();
    setup_pins();
  }

  virtual ~Esp32S3CamGpsA7680cBoard() {
    if (vehicle_controller_) {
      delete vehicle_controller_;
      vehicle_controller_ = nullptr;
    }
    if (distance_sensor_) {
      delete distance_sensor_;
      distance_sensor_ = nullptr;
    }
  }

  virtual Led *GetLed() override {
    static SingleLed led(SOS_LED_PIN);
    return &led;
  }

  virtual AudioCodec *GetAudioCodec() override {
    static NoAudioCodecDuplex audio_codec(
        AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_BCLK,
        AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
    return &audio_codec;
  }

  virtual Display *GetDisplay() override { return display_; }

  virtual Camera *GetCamera() override { return camera_; }
};

DECLARE_BOARD(Esp32S3CamGpsA7680cBoard);