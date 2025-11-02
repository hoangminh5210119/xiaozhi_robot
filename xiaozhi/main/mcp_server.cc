/*
 * MCP Server Implementation
 * Reference: https://modelcontextprotocol.io/specification/2024-11-05
 */

#include "mcp_server.h"
#include <algorithm>
#include <cstring>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_pthread.h>

#include "application.h"
#include "board.h"
#include "boards/common/wifi_board.h"
// #include "boards/common/esp32_music.h"
#include "display.h"
#include "lvgl_display.h"
#include "lvgl_theme.h"
#include "oled_display.h"
#include "settings.h"

#include "I2CCommandBridge.h"
#include "StorageManager.h"
#include "VehicleController.h"
#include "telegram_manager.h"
#include "RecurringSchedule.h"
#include "esp32_camera.h"

#define TAG "MCP"

// Global instances
static I2CCommandBridge* g_i2c_bridge = nullptr;
static StorageManager* g_storage_manager = nullptr;
static VehicleController* g_vehicle_controller = nullptr;

// Initialize hardware controllers
static void InitializeControllers() {
    if (!g_i2c_bridge) {
        g_i2c_bridge = new I2CCommandBridge();
        if (!g_i2c_bridge->Init()) {
            ESP_LOGE(TAG, "Failed to initialize I2C bridge");
            delete g_i2c_bridge;
            g_i2c_bridge = nullptr;
            return;
        }
        
        // Start status polling
        g_i2c_bridge->StartStatusPolling(2000);
        ESP_LOGI(TAG, "✅ I2C Bridge initialized");
    }
    
    if (!g_storage_manager) {
        g_storage_manager = &StorageManager::GetInstance();
        if (g_i2c_bridge && g_storage_manager->Init(g_i2c_bridge)) {
            ESP_LOGI(TAG, "✅ Storage Manager initialized");
        }
    }
    
    if (!g_vehicle_controller && g_i2c_bridge) {
        g_vehicle_controller = new VehicleController(g_i2c_bridge, nullptr);
        ESP_LOGI(TAG, "✅ Vehicle Controller initialized");
    }
}

McpServer::McpServer() {}

McpServer::~McpServer() {
  for (auto tool : tools_) {
    delete tool;
  }
  tools_.clear();
}

void McpServer::AddCommonTools() {
  // *Important* To speed up the response time, we add the common tools to the
  // beginning of the tools list to utilize the prompt cache.
  // **重要** 为了提升响应速度，我们把常用的工具放在前面，利用 prompt cache
  // 的特性。

  // Backup the original tools list and restore it after adding the common
  // tools.
  auto original_tools = std::move(tools_);
  auto &board = Board::GetInstance();

  // Do not add custom tools here.
  // Custom tools must be added in the board's InitializeTools function.

  AddTool(
      "self.get_device_status",
      "Provides the real-time information of the device, including the current "
      "status of the audio speaker, screen, battery, network, etc.\n"
      "Use this tool for: \n"
      "1. Answering questions about current condition (e.g. what is the "
      "current volume of the audio speaker?)\n"
      "2. As the first step to control the device (e.g. turn up / down the "
      "volume of the audio speaker, etc.)",
      PropertyList(), [&board](const PropertyList &properties) -> ReturnValue {
        return board.GetDeviceStatusJson();
      });

  AddTool("self.audio_speaker.set_volume",
          "Set the volume of the audio speaker. If the current volume is "
          "unknown, you must call `self.get_device_status` tool first and then "
          "call this tool.",
          PropertyList({Property("volume", kPropertyTypeInteger, 0, 100)}),
          [&board](const PropertyList &properties) -> ReturnValue {
            auto codec = board.GetAudioCodec();
            codec->SetOutputVolume(properties["volume"].value<int>());
            return true;
          });

  auto backlight = board.GetBacklight();
  if (backlight) {
    AddTool(
        "self.screen.set_brightness", "Set the brightness of the screen.",
        PropertyList({Property("brightness", kPropertyTypeInteger, 0, 100)}),
        [backlight](const PropertyList &properties) -> ReturnValue {
          uint8_t brightness =
              static_cast<uint8_t>(properties["brightness"].value<int>());
          backlight->SetBrightness(brightness, true);
          return true;
        });
  }

#ifdef HAVE_LVGL
  auto display = board.GetDisplay();
  if (display && display->GetTheme() != nullptr) {
    AddTool("self.screen.set_theme",
            "Set the theme of the screen. The theme can be `light` or `dark`.",
            PropertyList({Property("theme", kPropertyTypeString)}),
            [display](const PropertyList &properties) -> ReturnValue {
              auto theme_name = properties["theme"].value<std::string>();
              auto &theme_manager = LvglThemeManager::GetInstance();
              auto theme = theme_manager.GetTheme(theme_name);
              if (theme != nullptr) {
                display->SetTheme(theme);
                return true;
              }
              return false;
            });
  }

  auto camera = board.GetCamera();
  if (camera) {
    AddTool("self.camera.take_photo",
            "Take a photo and explain it. Use this tool after the user asks "
            "you to see something.\n"
            "Args:\n"
            "  `question`: The question that you want to ask about the photo.\n"
            "Return:\n"
            "  A JSON object that provides the photo information.",
            PropertyList({Property("question", kPropertyTypeString)}),
            [camera](const PropertyList &properties) -> ReturnValue {
              // Lower the priority to do the camera capture
              TaskPriorityReset priority_reset(1);

              if (!camera->Capture()) {
                throw std::runtime_error("Failed to capture photo");
              }
              auto question = properties["question"].value<std::string>();
              return camera->Explain(question);
            });
  }
#endif

  //   auto music = board.GetMusic();
  //   if (music) {
  //     AddTool(
  //         "self.music.play_song",
  //         "播放指定的歌曲。当用户要求播放音乐时使用此工具，会自动获取歌曲详情并开"
  //         "始流式播放。\n"
  //         "参数:\n"
  //         "  `song_name`: 要播放的歌曲名称（必需）。\n"
  //         "  `artist_name`:
  //         要播放的歌曲艺术家名称（可选，默认为空字符串）。\n" "返回:\n" "
  //         播放状态信息，不需确认，立刻播放歌曲。", PropertyList({
  //             Property("song_name", kPropertyTypeString), // 歌曲名称（必需）
  //             Property("artist_name", kPropertyTypeString,
  //                      "") // 艺术家名称（可选，默认为空字符串）
  //         }),
  //         [music](const PropertyList &properties) -> ReturnValue {
  //           auto song_name = properties["song_name"].value<std::string>();
  //           auto artist_name =
  //           properties["artist_name"].value<std::string>();

  //           if (!music->Download(song_name, artist_name)) {
  //             return "{\"success\": false, \"message\":
  //             \"获取音乐资源失败\"}";
  //           }
  //           auto download_result = music->GetDownloadResult();
  //           ESP_LOGI(TAG, "Music details result: %s",
  //           download_result.c_str()); return "{\"success\": true,
  //           \"message\": \"音乐开始播放\"}";
  //         });

  //     AddTool(
  //         "self.music.set_display_mode",
  //         "设置音乐播放时的显示模式。可以选择显示频谱或歌词，比如用户说‘打开频谱’"
  //         "或者‘显示频谱’，‘打开歌词’或者‘显示歌词’就设置对应的显示模式。\n"
  //         "参数:\n"
  //         "  `mode`: 显示模式，可选值为 'spectrum'（频谱）或
  //         'lyrics'（歌词）。\n" "返回:\n" "  设置结果信息。", PropertyList({
  //             Property("mode",
  //                      kPropertyTypeString) // 显示模式: "spectrum" 或
  //                      "lyrics"
  //         }),
  //         [music](const PropertyList &properties) -> ReturnValue {
  //           auto mode_str = properties["mode"].value<std::string>();

  //           // 转换为小写以便比较
  //           std::transform(mode_str.begin(), mode_str.end(),
  //           mode_str.begin(),
  //                          ::tolower);

  //           if (mode_str == "spectrum" || mode_str == "频谱") {
  //             // 设置为频谱显示模式
  //             auto esp32_music = static_cast<Esp32Music *>(music);
  //             esp32_music->SetDisplayMode(Esp32Music::DISPLAY_MODE_SPECTRUM);
  //             return "{\"success\": true, \"message\":
  //             \"已切换到频谱显示模式\"}";
  //           } else if (mode_str == "lyrics" || mode_str == "歌词") {
  //             // 设置为歌词显示模式
  //             auto esp32_music = static_cast<Esp32Music *>(music);
  //             esp32_music->SetDisplayMode(Esp32Music::DISPLAY_MODE_LYRICS);
  //             return "{\"success\": true, \"message\":
  //             \"已切换到歌词显示模式\"}";
  //           } else {
  //             return "{\"success\": false, \"message\":
  //             \"无效的显示模式，请使用 "
  //                    "'spectrum' 或 'lyrics'\"}";
  //           }

  //           return "{\"success\": false, \"message\": \"设置显示模式失败\"}";
  //         });
  //   }

  // auto music = board.GetMusic();
  // if (music) {
  //   AddTool("self.music.play_song",
  //           "Phát một bài hát cụ thể. Công cụ này được sử dụng khi người dùng "
  //           "yêu cầu "
  //           "phát nhạc — hệ thống sẽ tự động lấy thông tin bài hát và bắt đầu "
  //           "phát luồng.\n"
  //           "Tham số:\n"
  //           "  `song_name`: Tên bài hát cần phát (bắt buộc).\n"
  //           "  `artist_name`: Tên nghệ sĩ (tùy chọn, mặc định là chuỗi rỗng).\n"
  //           "Kết quả trả về:\n"
  //           "  Thông tin trạng thái phát nhạc, không cần xác nhận, phát ngay "
  //           "lập tức.",
  //           PropertyList({
  //               Property("song_name",
  //                        kPropertyTypeString), // Tên bài hát (bắt buộc)
  //               Property("artist_name", kPropertyTypeString,
  //                        "") // Nghệ sĩ (tùy chọn, mặc định rỗng)
  //           }),
  //           [music](const PropertyList &properties) -> ReturnValue {
  //             auto song_name = properties["song_name"].value<std::string>();
  //             auto artist_name = properties["artist_name"].value<std::string>();

  //             if (!music->Download(song_name, artist_name)) {
  //               return "{\"success\": false, \"message\": \"Không thể tải "
  //                      "nguồn nhạc\"}";
  //             }
  //             auto download_result = music->GetDownloadResult();
  //             ESP_LOGI(TAG, "Kết quả chi tiết bài nhạc: %s",
  //                      download_result.c_str());
  //             return "{\"success\": true, \"message\": \"Đang phát bài hát\"}";
  //           });

  //   AddTool("self.music.set_display_mode",
  //           "Thiết lập chế độ hiển thị khi phát nhạc. Có thể chọn hiển thị "
  //           "dạng phổ âm "
  //           "(spectrum) hoặc lời bài hát (lyrics). Ví dụ: người dùng nói ‘hiển "
  //           "thị phổ âm’ "
  //           "hoặc ‘mở lời bài hát’ thì sẽ chuyển sang chế độ tương ứng.\n"
  //           "Tham số:\n"
  //           "  `mode`: Chế độ hiển thị, giá trị hợp lệ là 'spectrum' (phổ âm) "
  //           "hoặc 'lyrics' (lời bài hát).\n"
  //           "Kết quả trả về:\n"
  //           "  Thông tin kết quả thiết lập chế độ hiển thị.",
  //           PropertyList({
  //               Property("mode",
  //                        kPropertyTypeString) // Chế độ hiển thị: "spectrum"
  //                                             // hoặc "lyrics"
  //           }),
  //           [music](const PropertyList &properties) -> ReturnValue {
  //             auto mode_str = properties["mode"].value<std::string>();

  //             // Chuyển thành chữ thường để so sánh dễ hơn
  //             std::transform(mode_str.begin(), mode_str.end(), mode_str.begin(),
  //                            ::tolower);

  //             if (mode_str == "spectrum" || mode_str == "phổ" ||
  //                 mode_str == "phổ âm") {
  //               // Đặt chế độ hiển thị phổ âm
  //               auto esp32_music = static_cast<Esp32Music *>(music);
  //               esp32_music->SetDisplayMode(Esp32Music::DISPLAY_MODE_SPECTRUM);
  //               return "{\"success\": true, \"message\": \"Đã chuyển sang chế "
  //                      "độ hiển thị phổ âm\"}";
  //             } else if (mode_str == "lyrics" || mode_str == "lời" ||
  //                        mode_str == "lời bài hát") {
  //               // Đặt chế độ hiển thị lời bài hát
  //               auto esp32_music = static_cast<Esp32Music *>(music);
  //               esp32_music->SetDisplayMode(Esp32Music::DISPLAY_MODE_LYRICS);
  //               return "{\"success\": true, \"message\": \"Đã chuyển sang chế "
  //                      "độ hiển thị lời bài hát\"}";
  //             } else {
  //               return "{\"success\": false, \"message\": \"Chế độ hiển thị "
  //                      "không hợp lệ, hãy dùng 'spectrum' hoặc 'lyrics'\"}";
  //             }

  //             return "{\"success\": false, \"message\": \"Thiết lập chế độ "
  //                    "hiển thị thất bại\"}";
  //           });
  // }

  // Initialize hardware controllers
  InitializeControllers();

  // ==================== VEHICLE CONTROL TOOLS ====================
  if (g_vehicle_controller) {
    AddTool("vehicle.move",
            "Di chuyển xe theo hướng và khoảng cách. Sử dụng tool này khi người dùng yêu cầu di chuyển xe.\n"
            "Hướng di chuyển: 'forward' (tiến), 'backward' (lùi), 'left' (trái), 'right' (phải), 'rotate_left' (xoay trái), 'rotate_right' (xoay phải), 'stop' (dừng).\n"
            "Args:\n"
            "  `direction`: Hướng di chuyển (bắt buộc).\n"
            "  `distance_mm`: Khoảng cách di chuyển tính bằng mm (mặc định 500mm).\n"
            "  `speed`: Tốc độ 0-100 (mặc định 50).",
            PropertyList({
                Property("direction", kPropertyTypeString),
                Property("distance_mm", kPropertyTypeInteger, 500),
                Property("speed", kPropertyTypeInteger, 50)
            }),
            [](const PropertyList &properties) -> ReturnValue {
                auto direction = properties["direction"].value<std::string>();
                int distance_mm = properties["distance_mm"].value<int>();
                int speed = properties["speed"].value<int>();
                
                VehicleController::MoveCommand cmd(direction, speed, distance_mm);
                if (g_vehicle_controller->ExecuteMove(cmd)) {
                    return "{\"success\": true, \"message\": \"Xe đang di chuyển " + direction + "\"}";
                }
                return "{\"success\": false, \"message\": \"Không thể điều khiển xe\"}";
            });

    AddTool("vehicle.execute_command",
            "Thực hiện lệnh di chuyển phức tạp bằng ngôn ngữ tự nhiên. Ví dụ: 'đi tới 1m rẽ phải đi thẳng 500mm'.\n"
            "Args:\n"
            "  `command`: Lệnh di chuyển bằng tiếng Việt.",
            PropertyList({
                Property("command", kPropertyTypeString)
            }),
            [](const PropertyList &properties) -> ReturnValue {
                auto command = properties["command"].value<std::string>();
                
                auto commands = g_vehicle_controller->ParseNaturalCommand(command);
                if (g_vehicle_controller->ExecuteSequence(commands)) {
                    return "{\"success\": true, \"message\": \"Đang thực hiện lệnh: " + command + "\"}";
                }
                return "{\"success\": false, \"message\": \"Không thể phân tích lệnh\"}";
            });

    AddTool("vehicle.stop",
            "Dừng xe ngay lập tức.",
            PropertyList(),
            [](const PropertyList &properties) -> ReturnValue {
                if (g_vehicle_controller->Stop()) {
                    return "{\"success\": true, \"message\": \"Xe đã dừng\"}";
                }
                return "{\"success\": false, \"message\": \"Không thể dừng xe\"}";
            });
  }

  // ==================== STORAGE CONTROL TOOLS ====================
  if (g_storage_manager) {
    AddTool("storage.open_slot",
            "Mở ô lưu trữ vật lý (0-3).\n"
            "Args:\n"
            "  `slot_id`: Số ô cần mở (0-3).",
            PropertyList({
                Property("slot_id", kPropertyTypeInteger, 0, 3)
            }),
            [](const PropertyList &properties) -> ReturnValue {
                int slot_id = properties["slot_id"].value<int>();
                
                if (g_storage_manager->OpenHardwareSlot(slot_id)) {
                    return "{\"success\": true, \"message\": \"Đã mở ô " + std::to_string(slot_id + 1) + "\"}";
                }
                return "{\"success\": false, \"message\": \"Không thể mở ô\"}";
            });

    AddTool("storage.close_slot",
            "Đóng ô lưu trữ vật lý (0-3).\n"
            "Args:\n"
            "  `slot_id`: Số ô cần đóng (0-3).",
            PropertyList({
                Property("slot_id", kPropertyTypeInteger, 0, 3)
            }),
            [](const PropertyList &properties) -> ReturnValue {
                int slot_id = properties["slot_id"].value<int>();
                
                if (g_storage_manager->CloseHardwareSlot(slot_id)) {
                    return "{\"success\": true, \"message\": \"Đã đóng ô " + std::to_string(slot_id + 1) + "\"}";
                }
                return "{\"success\": false, \"message\": \"Không thể đóng ô\"}";
            });

    AddTool("storage.store_item",
            "Lưu thông tin vật phẩm vào storage. Vị trí có thể là ô vật lý (slot_0, slot_1) hoặc vị trí ảo (trên bàn, trong túi).\n"
            "Args:\n"
            "  `item_name`: Tên vật phẩm.\n"
            "  `location`: Vị trí lưu trữ.\n"
            "  `description`: Mô tả thêm (tùy chọn).",
            PropertyList({
                Property("item_name", kPropertyTypeString),
                Property("location", kPropertyTypeString),
                Property("description", kPropertyTypeString, "")
            }),
            [](const PropertyList &properties) -> ReturnValue {
                auto item_name = properties["item_name"].value<std::string>();
                auto location = properties["location"].value<std::string>();
                auto description = properties["description"].value<std::string>();
                
                if (g_storage_manager->StoreItem(item_name, location, description)) {
                    return "{\"success\": true, \"message\": \"Đã lưu " + item_name + " vào " + location + "\"}";
                }
                return "{\"success\": false, \"message\": \"Không thể lưu vật phẩm\"}";
            });

    AddTool("storage.find_item",
            "Tìm vị trí của vật phẩm.\n"
            "Args:\n"
            "  `item_name`: Tên vật phẩm cần tìm.",
            PropertyList({
                Property("item_name", kPropertyTypeString)
            }),
            [](const PropertyList &properties) -> ReturnValue {
                auto item_name = properties["item_name"].value<std::string>();
                
                std::string location = g_storage_manager->FindItemLocation(item_name);
                if (!location.empty()) {
                    return "{\"success\": true, \"item\": \"" + item_name + "\", \"location\": \"" + location + "\"}";
                }
                return "{\"success\": false, \"message\": \"Không tìm thấy " + item_name + "\"}";
            });

    AddTool("storage.process_command",
            "Xử lý lệnh lưu trữ bằng ngôn ngữ tự nhiên. Ví dụ: 'để kính vào ô 1', 'kính ở đâu', 'mở ô 2'.\n"
            "Args:\n"
            "  `command`: Lệnh bằng tiếng Việt.",
            PropertyList({
                Property("command", kPropertyTypeString)
            }),
            [](const PropertyList &properties) -> ReturnValue {
                auto command = properties["command"].value<std::string>();
                
                std::string response = g_storage_manager->ProcessNaturalCommand(command);
                return "{\"success\": true, \"message\": \"" + response + "\"}";
            });

    AddTool("storage.list_all_items",
            "Liệt kê tất cả vật phẩm trong storage.",
            PropertyList(),
            [](const PropertyList &properties) -> ReturnValue {
                auto items = g_storage_manager->GetAllItems();
                
                cJSON* json = cJSON_CreateObject();
                cJSON_AddNumberToObject(json, "total", items.size());
                
                cJSON* items_array = cJSON_CreateArray();
                for (const auto& item : items) {
                    cJSON* item_json = cJSON_CreateObject();
                    cJSON_AddStringToObject(item_json, "name", item.name.c_str());
                    cJSON_AddStringToObject(item_json, "location", item.location.c_str());
                    cJSON_AddBoolToObject(item_json, "is_hardware", item.is_hardware_slot);
                    if (!item.description.empty()) {
                        cJSON_AddStringToObject(item_json, "description", item.description.c_str());
                    }
                    cJSON_AddItemToArray(items_array, item_json);
                }
                cJSON_AddItemToObject(json, "items", items_array);
                
                return json;
            });
    
    // ==================== SMART STORAGE WORKFLOW TOOLS ====================
    
    AddTool("storage.smart_store",
            "🤖 THÔNG MINH: Tự động tìm ô trống, mở cửa để user bỏ đồ vào.\n"
            "⚠️ QUAN TRỌNG: User đếm từ 1-4, hệ thống internal dùng 0-3.\n"
            "Use case: User nói 'để điện thoại vào', 'cất ví', 'bỏ kính vào tủ'\n"
            "Workflow:\n"
            "1. Kiểm tra tủ có đầy không\n"
            "2. Tìm ô trống đầu tiên\n"
            "3. Mở cửa ô đó\n"
            "4. Lưu thông tin tạm: đang chờ user bỏ đồ vào\n"
            "5. Trả về message với số ô THEO USER (1-4)\n"
            "Args:\n"
            "  `item_name`: Tên đồ vật cần cất (VD: 'điện thoại', 'kính', 'ví').",
            PropertyList({
                Property("item_name", kPropertyTypeString)
            }),
            [](const PropertyList &properties) -> ReturnValue {
                auto item_name = properties["item_name"].value<std::string>();
                
                // 1. Kiểm tra tủ có ô trống không
                int internal_slot = -1; // 0-3
                for (int i = 0; i < 4; i++) {
                    auto hw_slot = g_storage_manager->GetHardwareSlot(i);
                    if (hw_slot && !hw_slot->has_item) {
                        internal_slot = i;
                        break;
                    }
                }
                
                if (internal_slot == -1) {
                    return "{\"success\": false, \"message\": \"Tủ đã đầy, không còn ô trống. Vui lòng lấy đồ ra trước.\"}";
                }
                
                int user_slot = internal_slot + 1; // Convert 0-3 to 1-4
                
                // 2. Mở cửa ô trống
                if (!g_storage_manager->OpenHardwareSlot(internal_slot)) {
                    return "{\"success\": false, \"message\": \"Không thể mở cửa ô " + std::to_string(user_slot) + "\"}";
                }
                
                // 3. Lưu thông tin tạm
                g_storage_manager->SetPendingItem(internal_slot, item_name);
                
                // 4. Trả về message với số ô theo user (1-4)
                cJSON* json = cJSON_CreateObject();
                cJSON_AddBoolToObject(json, "success", true);
                cJSON_AddNumberToObject(json, "slot_number", user_slot); // 1-4 for user
                cJSON_AddStringToObject(json, "item_name", item_name.c_str());
                cJSON_AddStringToObject(json, "message", 
                    ("Đã mở ô số " + std::to_string(user_slot) + ". Vui lòng bỏ " + item_name + " vào rồi nói 'đóng cửa'.").c_str());
                cJSON_AddStringToObject(json, "status", "waiting_for_item");
                
                return json;
            });
    
    AddTool("storage.smart_close",
            "🤖 THÔNG MINH: Đóng cửa ô đang mở và lưu thông tin đồ vật.\n"
            "⚠️ QUAN TRỌNG: Trả về số ô THEO USER (1-4).\n"
            "Use case: User vừa bỏ đồ vào ô đang mở, nói 'đóng cửa', 'đóng lại'\n"
            "Workflow:\n"
            "1. Tìm ô nào đang mở (is_open=true)\n"
            "2. Đóng cửa ô đó\n"
            "3. Lưu thông tin item vào ô (nếu có pending_item)\n"
            "4. Clear pending state\n"
            "Không cần tham số đầu vào.",
            PropertyList(),
            [](const PropertyList &properties) -> ReturnValue {
                // 1. Tìm ô đang mở (internal 0-3)
                int internal_slot = -1;
                std::string pending_item = "";
                
                for (int i = 0; i < 4; i++) {
                    auto hw_slot = g_storage_manager->GetHardwareSlot(i);
                    if (hw_slot && hw_slot->is_open) {
                        internal_slot = i;
                        pending_item = g_storage_manager->GetPendingItem(i);
                        break;
                    }
                }
                
                if (internal_slot == -1) {
                    return "{\"success\": false, \"message\": \"Không có ô nào đang mở cả.\"}";
                }
                
                int user_slot = internal_slot + 1; // Convert 0-3 to 1-4
                
                // 2. Đóng cửa
                if (!g_storage_manager->CloseHardwareSlot(internal_slot)) {
                    return "{\"success\": false, \"message\": \"Không thể đóng cửa ô " + std::to_string(user_slot) + "\"}";
                }
                
                // 3. Lưu thông tin item (nếu có)
                std::string message;
                if (!pending_item.empty()) {
                    std::string location = "slot_" + std::to_string(internal_slot);
                    g_storage_manager->StoreItem(pending_item, location, "");
                    g_storage_manager->ClearPendingItem(internal_slot);
                    message = "Đã đóng ô số " + std::to_string(user_slot) + " và lưu " + pending_item + ".";
                } else {
                    message = "Đã đóng ô số " + std::to_string(user_slot) + ".";
                }
                
                cJSON* json = cJSON_CreateObject();
                cJSON_AddBoolToObject(json, "success", true);
                cJSON_AddNumberToObject(json, "slot_number", user_slot); // 1-4 for user
                if (!pending_item.empty()) {
                    cJSON_AddStringToObject(json, "item_stored", pending_item.c_str());
                }
                cJSON_AddStringToObject(json, "message", message.c_str());
                
                return json;
            });
    
    AddTool("storage.smart_retrieve",
            "🤖 THÔNG MINH: Tự động tìm đồ và mở cửa ô chứa đồ đó.\n"
            "⚠️ QUAN TRỌNG: Trả về số ô THEO USER (1-4).\n"
            "Use case: User nói 'lấy điện thoại ra', 'lấy ví', 'mở tủ lấy kính'\n"
            "Workflow:\n"
            "1. Tìm vị trí của item\n"
            "2. Nếu là ô vật lý → Mở cửa ô đó, trả về số ô 1-4\n"
            "3. Nếu là vị trí ảo → Chỉ thông báo vị trí\n"
            "Args:\n"
            "  `item_name`: Tên đồ vật cần lấy.",
            PropertyList({
                Property("item_name", kPropertyTypeString)
            }),
            [](const PropertyList &properties) -> ReturnValue {
                auto item_name = properties["item_name"].value<std::string>();
                
                // 1. Tìm vị trí item
                std::string location = g_storage_manager->FindItemLocation(item_name);
                if (location.empty()) {
                    return "{\"success\": false, \"message\": \"Không tìm thấy " + item_name + " trong tủ.\"}";
                }
                
                // 2. Kiểm tra xem có phải ô vật lý không
                if (location.find("slot_") == 0) {
                    // Parse internal slot_id từ "slot_0", "slot_1", etc. (0-3)
                    int internal_slot = std::stoi(location.substr(5));
                    int user_slot = internal_slot + 1; // Convert 0-3 to 1-4
                    
                    // Mở cửa ô
                    if (!g_storage_manager->OpenHardwareSlot(internal_slot)) {
                        return "{\"success\": false, \"message\": \"Không thể mở ô " + std::to_string(user_slot) + "\"}";
                    }
                    
                    // Xóa item khỏi storage (user đã lấy ra)
                    g_storage_manager->RemoveItem(item_name);
                    
                    cJSON* json = cJSON_CreateObject();
                    cJSON_AddBoolToObject(json, "success", true);
                    cJSON_AddNumberToObject(json, "slot_number", user_slot); // 1-4 for user
                    cJSON_AddStringToObject(json, "item_name", item_name.c_str());
                    cJSON_AddStringToObject(json, "message", 
                        ("Đã mở ô số " + std::to_string(user_slot) + " để lấy " + item_name + ". Nhớ nói 'đóng cửa' sau khi lấy xong.").c_str());
                    cJSON_AddStringToObject(json, "action", "opened_hardware_slot");
                    
                    return json;
                } else {
                    // Vị trí ảo, chỉ thông báo
                    cJSON* json = cJSON_CreateObject();
                    cJSON_AddBoolToObject(json, "success", true);
                    cJSON_AddStringToObject(json, "item_name", item_name.c_str());
                    cJSON_AddStringToObject(json, "location", location.c_str());
                    cJSON_AddStringToObject(json, "message", 
                        (item_name + " đang ở " + location + ".").c_str());
                    cJSON_AddStringToObject(json, "action", "virtual_location_info");
                    
                    return json;
                }
            });
  }

  // ==================== TELEGRAM & SCHEDULE TOOLS ====================
  
  // Reuse board and camera from above (already declared at line 83, 143)
  if (camera) {
    AddTool("telegram.send_photo",
            "📸 Chụp ảnh và gửi qua Telegram bot.\n"
            "Sử dụng khi user yêu cầu chụp ảnh gửi cho người thân.\n"
            "Không cần tham số, hệ thống tự động chụp và gửi.",
            PropertyList(),
            [camera](const PropertyList &properties) -> ReturnValue {
                auto &app = Application::GetInstance();
                
                // Cast to Esp32Camera to access SendPhotoToTelegram
                auto esp32_camera = dynamic_cast<Esp32Camera*>(camera);
                if (!esp32_camera) {
                    return "{\"success\": false, \"message\": \"Camera không hỗ trợ gửi ảnh qua Telegram\"}";
                }
                
                app.Schedule([esp32_camera]() {
                    if (!esp32_camera->Capture()) {
                        ESP_LOGE(TAG, "Failed to capture photo");
                        return;
                    }
                    
                    ESP_LOGI(TAG, "Captured photo, sending to Telegram...");
                    TelegramPhotoInfo info;
                    
                    auto &telegram_manager = TelegramManager::GetInstance();
                    auto config = telegram_manager.GetConfig();
                    
                    info.caption = "";
                    info.parse_mode = "";
                    
                    if (!config.chat_id.empty() && !config.bot_token.empty()) {
                        ESP_LOGI(TAG, "Loaded bot token from TelegramManager");
                        info.bot_token = config.bot_token;
                        info.chat_id = config.chat_id;
                        
                        esp32_camera->SendPhotoToTelegram(info);
                    } else {
                        ESP_LOGW(TAG, "Telegram bot not configured");
                    }
                });
                
                return "{\"success\": true, \"message\": \"Đang chụp và gửi ảnh qua Telegram...\"}";
            });
    
    AddTool("telegram.send_message",
            "💬 Gửi tin nhắn text qua Telegram.\n"
            "Args:\n"
            "  `message`: Nội dung tin nhắn (hỗ trợ tiếng Việt và emoji).",
            PropertyList({
                Property("message", kPropertyTypeString)
            }),
            [](const PropertyList &properties) -> ReturnValue {
                auto message = properties["message"].value<std::string>();
                auto &app = Application::GetInstance();
                
                // app.Schedule([message]() {
                    auto &telegram_manager = TelegramManager::GetInstance();
                    auto config = telegram_manager.GetConfig();
                    
                    if (!config.chat_id.empty() && !config.bot_token.empty()) {
                        ESP_LOGI(TAG, "Sending message to Telegram: %s", message.c_str());
                        // TODO: Implement telegram_manager.SendMessage() method
                        // telegram_manager.SendMessage(message);
                        auto app = &Application::GetInstance();
                        app->SendTelegramMessage(message);

                    } else {
                        ESP_LOGW(TAG, "Telegram bot not configured");
                    }
                // });
                
                return "{\"success\": true, \"message\": \"Đang gửi tin nhắn qua Telegram...\"}";
            });
  }
  
  // ==================== RECURRING SCHEDULE TOOLS ====================
  
  AddTool("schedule.add_reminder",
          "⏰ ĐẶT LỊCH NHẮC NHỞ sau một khoảng thời gian.\n"
          "Use case: 'Nhắc tôi sau 30 giây', 'Đặt lịch 5 phút nữa'\n"
          "Args:\n"
          "  `seconds`: Số giây delay từ bây giờ (VD: 30, 300, 7200)\n"
          "  `message`: Nội dung nhắc nhở sẽ được phát ra.",
          PropertyList({
              Property("seconds", kPropertyTypeInteger, 1, 86400), // 1 sec to 24 hours
              Property("message", kPropertyTypeString)
          }),
          [](const PropertyList &properties) -> ReturnValue {
              int delay = properties["seconds"].value<int>();
              auto message = properties["message"].value<std::string>();
              
              auto &app = Application::GetInstance();
              auto &scheduler = RecurringSchedule::GetInstance();
              
              // Generate unique ID based on timestamp
              int id = (int)(esp_timer_get_time() / 1000);
              
              ESP_LOGI(TAG, "⏰ Adding reminder: delay=%d sec, message=%s", delay, message.c_str());
              
              app.Schedule([&scheduler, id, delay, message]() {
                  scheduler.addOnceAfterDelay(id, delay, message, true);
              });
              
              cJSON* json = cJSON_CreateObject();
              cJSON_AddBoolToObject(json, "success", true);
              cJSON_AddNumberToObject(json, "schedule_id", id);
              cJSON_AddNumberToObject(json, "delay_seconds", delay);
              cJSON_AddStringToObject(json, "message", 
                  ("Đã đặt lịch nhắc sau " + std::to_string(delay) + " giây: " + message).c_str());
              
              return json;
          });
  
  AddTool("schedule.list_all",
          "📋 XEM TẤT CẢ LỊCH NHẮC đã đặt.\n"
          "Trả về JSON chứa thông tin tất cả lịch.",
          PropertyList(),
          [](const PropertyList &properties) -> ReturnValue {
              auto &scheduler = RecurringSchedule::GetInstance();
              ESP_LOGI(TAG, "📋 Listing all schedules");
              return scheduler.getSchedulesJSON();
          });
  
  AddTool("schedule.remove",
          "🗑️ XÓA LỊCH NHẮC theo ID.\n"
          "⚠️ LƯU Ý: Phải gọi 'schedule.list_all' trước để biết ID.\n"
          "Args:\n"
          "  `schedule_id`: ID của lịch cần xóa.",
          PropertyList({
              Property("schedule_id", kPropertyTypeInteger)
          }),
          [](const PropertyList &properties) -> ReturnValue {
              int id = properties["schedule_id"].value<int>();
              auto &app = Application::GetInstance();
              auto &scheduler = RecurringSchedule::GetInstance();
              
              ESP_LOGI(TAG, "🗑️ Removing schedule: id=%d", id);
              
              app.Schedule([&scheduler, id]() {
                  scheduler.removeSchedule(id, true);
              });
              
              return "{\"success\": true, \"message\": \"Đã xóa lịch nhắc ID " + std::to_string(id) + "\"}";
          });


          AddTool("system.wifi_reset",
                "Khởi động lại thiết bị và vào chế độ cấu hình WiFi để kết nối "
                "mạng mới. "
                "**CẢNH BÁO**: Hành động này sẽ ngắt kết nối hiện tại và yêu "
                "cầu cấu hình lại WiFi. "
                "Chỉ sử dụng khi cần thay đổi mạng WiFi hoặc khắc phục sự cố "
                "kết nối. Cần xác nhận từ người dùng",
                PropertyList(), [](const PropertyList &) -> ReturnValue {
                  auto &app = Application::GetInstance();
                  app.Schedule([]() {
                    ESP_LOGW(TAG, "User requested WiFi reset");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    
                    auto &board = Board::GetInstance();
                    auto *wifi_board = dynamic_cast<WifiBoard*>(&board);
                    if (wifi_board) {
                      wifi_board->ResetWifiConfiguration();
                    } else {
                      ESP_LOGE(TAG, "Current board does not support WiFi reset");
                    }
                  });                  
                  return true;
                });

    // Telegram functions
    AddTool("msg.check",
                "Kiểm tra và đọc tin nhắn mới từ Telegram bot. Trả về nội dung "
                "JSON chứa các tin nhắn chưa đọc "
                "bao gồm người gửi, thời gian, nội dung tin nhắn. Nếu có tin "
                "nhắn mới sẽ đọc lần lượt từng tin. "
                "Dùng để nhận thông báo, tin nhắn từ người thân hoặc hệ thống "
                "giám sát từ xa",
                PropertyList(), [](const PropertyList &) -> ReturnValue {
                  auto &app = Application::GetInstance();
                  return app.GetTelegramMsgBufferAsJson();
                });

  // Restore the original tools list to the end of the tools list
  tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
}

void McpServer::AddUserOnlyTools() {
  // System tools
  AddUserOnlyTool("self.get_system_info", "Get the system information",
                  PropertyList(),
                  [this](const PropertyList &properties) -> ReturnValue {
                    auto &board = Board::GetInstance();
                    return board.GetSystemInfoJson();
                  });

  AddUserOnlyTool("self.reboot", "Reboot the system", PropertyList(),
                  [this](const PropertyList &properties) -> ReturnValue {
                    auto &app = Application::GetInstance();
                    app.Schedule([&app]() {
                      ESP_LOGW(TAG, "User requested reboot");
                      vTaskDelay(pdMS_TO_TICKS(1000));

                      app.Reboot();
                    });
                    return true;
                  });

  // Firmware upgrade
  AddUserOnlyTool(
      "self.upgrade_firmware",
      "Upgrade firmware from a specific URL. This will download and install "
      "the firmware, then reboot the device.",
      PropertyList({Property(
          "url", kPropertyTypeString,
          "The URL of the firmware binary file to download and install")}),
      [this](const PropertyList &properties) -> ReturnValue {
        auto url = properties["url"].value<std::string>();
        ESP_LOGI(TAG, "User requested firmware upgrade from URL: %s",
                 url.c_str());

        auto &app = Application::GetInstance();
        app.Schedule([url, &app]() {
          auto ota = std::make_unique<Ota>();

          bool success = app.UpgradeFirmware(*ota, url);
          if (!success) {
            ESP_LOGE(TAG, "Firmware upgrade failed");
          }
        });

        return true;
      });

  // Display control
#ifdef HAVE_LVGL
  auto display = dynamic_cast<LvglDisplay *>(Board::GetInstance().GetDisplay());
  if (display) {
    AddUserOnlyTool(
        "self.screen.get_info",
        "Information about the screen, including width, height, etc.",
        PropertyList(),
        [display](const PropertyList &properties) -> ReturnValue {
          cJSON *json = cJSON_CreateObject();
          cJSON_AddNumberToObject(json, "width", display->width());
          cJSON_AddNumberToObject(json, "height", display->height());
          if (dynamic_cast<OledDisplay *>(display)) {
            cJSON_AddBoolToObject(json, "monochrome", true);
          } else {
            cJSON_AddBoolToObject(json, "monochrome", false);
          }
          return json;
        });

#if CONFIG_LV_USE_SNAPSHOT
    AddUserOnlyTool(
        "self.screen.snapshot",
        "Snapshot the screen and upload it to a specific URL",
        PropertyList({Property("url", kPropertyTypeString),
                      Property("quality", kPropertyTypeInteger, 80, 1, 100)}),
        [display](const PropertyList &properties) -> ReturnValue {
          auto url = properties["url"].value<std::string>();
          auto quality = properties["quality"].value<int>();

          std::string jpeg_data;
          if (!display->SnapshotToJpeg(jpeg_data, quality)) {
            throw std::runtime_error("Failed to snapshot screen");
          }

          ESP_LOGI(TAG, "Upload snapshot %u bytes to %s", jpeg_data.size(),
                   url.c_str());

          // 构造multipart/form-data请求体
          std::string boundary = "----ESP32_SCREEN_SNAPSHOT_BOUNDARY";

          auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
          http->SetHeader("Content-Type",
                          "multipart/form-data; boundary=" + boundary);
          if (!http->Open("POST", url)) {
            throw std::runtime_error("Failed to open URL: " + url);
          }
          {
            // 文件字段头部
            std::string file_header;
            file_header += "--" + boundary + "\r\n";
            file_header += "Content-Disposition: form-data; name=\"file\"; "
                           "filename=\"screenshot.jpg\"\r\n";
            file_header += "Content-Type: image/jpeg\r\n";
            file_header += "\r\n";
            http->Write(file_header.c_str(), file_header.size());
          }

          // JPEG数据
          http->Write((const char *)jpeg_data.data(), jpeg_data.size());

          {
            // multipart尾部
            std::string multipart_footer;
            multipart_footer += "\r\n--" + boundary + "--\r\n";
            http->Write(multipart_footer.c_str(), multipart_footer.size());
          }
          http->Write("", 0);

          if (http->GetStatusCode() != 200) {
            throw std::runtime_error("Unexpected status code: " +
                                     std::to_string(http->GetStatusCode()));
          }
          std::string result = http->ReadAll();
          http->Close();
          ESP_LOGI(TAG, "Snapshot screen result: %s", result.c_str());
          return true;
        });

    AddUserOnlyTool(
        "self.screen.preview_image", "Preview an image on the screen",
        PropertyList({Property("url", kPropertyTypeString)}),
        [display](const PropertyList &properties) -> ReturnValue {
          auto url = properties["url"].value<std::string>();
          auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);

          if (!http->Open("GET", url)) {
            throw std::runtime_error("Failed to open URL: " + url);
          }
          int status_code = http->GetStatusCode();
          if (status_code != 200) {
            throw std::runtime_error("Unexpected status code: " +
                                     std::to_string(status_code));
          }

          size_t content_length = http->GetBodyLength();
          char *data =
              (char *)heap_caps_malloc(content_length, MALLOC_CAP_8BIT);
          if (data == nullptr) {
            throw std::runtime_error("Failed to allocate memory for image: " +
                                     url);
          }
          size_t total_read = 0;
          while (total_read < content_length) {
            int ret =
                http->Read(data + total_read, content_length - total_read);
            if (ret < 0) {
              heap_caps_free(data);
              throw std::runtime_error("Failed to download image: " + url);
            }
            if (ret == 0) {
              break;
            }
            total_read += ret;
          }
          http->Close();

          auto image =
              std::make_unique<LvglAllocatedImage>(data, content_length);
          display->SetPreviewImage(std::move(image));
          return true;
        });
#endif // CONFIG_LV_USE_SNAPSHOT
  }
#endif // HAVE_LVGL

  // Assets download url
  auto &assets = Assets::GetInstance();
  if (assets.partition_valid()) {
    AddUserOnlyTool("self.assets.set_download_url",
                    "Set the download url for the assets",
                    PropertyList({Property("url", kPropertyTypeString)}),
                    [](const PropertyList &properties) -> ReturnValue {
                      auto url = properties["url"].value<std::string>();
                      Settings settings("assets", true);
                      settings.SetString("download_url", url);
                      return true;
                    });
  }
}

void McpServer::AddTool(McpTool *tool) {
  // Prevent adding duplicate tools
  if (std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool *t) {
        return t->name() == tool->name();
      }) != tools_.end()) {
    ESP_LOGW(TAG, "Tool %s already added", tool->name().c_str());
    return;
  }

  ESP_LOGI(TAG, "Add tool: %s%s", tool->name().c_str(),
           tool->user_only() ? " [user]" : "");
  tools_.push_back(tool);
}

void McpServer::AddTool(
    const std::string &name, const std::string &description,
    const PropertyList &properties,
    std::function<ReturnValue(const PropertyList &)> callback) {
  AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::AddUserOnlyTool(
    const std::string &name, const std::string &description,
    const PropertyList &properties,
    std::function<ReturnValue(const PropertyList &)> callback) {
  auto tool = new McpTool(name, description, properties, callback);
  tool->set_user_only(true);
  AddTool(tool);
}

void McpServer::ParseMessage(const std::string &message) {
  cJSON *json = cJSON_Parse(message.c_str());
  if (json == nullptr) {
    ESP_LOGE(TAG, "Failed to parse MCP message: %s", message.c_str());
    return;
  }
  ParseMessage(json);
  cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON *capabilities) {
  auto vision = cJSON_GetObjectItem(capabilities, "vision");
  if (cJSON_IsObject(vision)) {
    auto url = cJSON_GetObjectItem(vision, "url");
    auto token = cJSON_GetObjectItem(vision, "token");
    if (cJSON_IsString(url)) {
      auto camera = Board::GetInstance().GetCamera();
      if (camera) {
        std::string url_str = std::string(url->valuestring);
        std::string token_str;
        if (cJSON_IsString(token)) {
          token_str = std::string(token->valuestring);
        }
        camera->SetExplainUrl(url_str, token_str);
      }
    }
  }
}

void McpServer::ParseMessage(const cJSON *json) {
  // Check JSONRPC version
  auto version = cJSON_GetObjectItem(json, "jsonrpc");
  if (version == nullptr || !cJSON_IsString(version) ||
      strcmp(version->valuestring, "2.0") != 0) {
    ESP_LOGE(TAG, "Invalid JSONRPC version: %s",
             version ? version->valuestring : "null");
    return;
  }

  // Check method
  auto method = cJSON_GetObjectItem(json, "method");
  if (method == nullptr || !cJSON_IsString(method)) {
    ESP_LOGE(TAG, "Missing method");
    return;
  }

  auto method_str = std::string(method->valuestring);
  if (method_str.find("notifications") == 0) {
    return;
  }

  // Check params
  auto params = cJSON_GetObjectItem(json, "params");
  if (params != nullptr && !cJSON_IsObject(params)) {
    ESP_LOGE(TAG, "Invalid params for method: %s", method_str.c_str());
    return;
  }

  auto id = cJSON_GetObjectItem(json, "id");
  if (id == nullptr || !cJSON_IsNumber(id)) {
    ESP_LOGE(TAG, "Invalid id for method: %s", method_str.c_str());
    return;
  }
  auto id_int = id->valueint;

  if (method_str == "initialize") {
    if (cJSON_IsObject(params)) {
      auto capabilities = cJSON_GetObjectItem(params, "capabilities");
      if (cJSON_IsObject(capabilities)) {
        ParseCapabilities(capabilities);
      }
    }
    auto app_desc = esp_app_get_description();
    std::string message =
        "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},"
        "\"serverInfo\":{\"name\":\"" BOARD_NAME "\",\"version\":\"";
    message += app_desc->version;
    message += "\"}}";
    ReplyResult(id_int, message);
  } else if (method_str == "tools/list") {
    std::string cursor_str = "";
    bool list_user_only_tools = false;
    if (params != nullptr) {
      auto cursor = cJSON_GetObjectItem(params, "cursor");
      if (cJSON_IsString(cursor)) {
        cursor_str = std::string(cursor->valuestring);
      }
      auto with_user_tools = cJSON_GetObjectItem(params, "withUserTools");
      if (cJSON_IsBool(with_user_tools)) {
        list_user_only_tools = with_user_tools->valueint == 1;
      }
    }
    GetToolsList(id_int, cursor_str, list_user_only_tools);
  } else if (method_str == "tools/call") {
    if (!cJSON_IsObject(params)) {
      ESP_LOGE(TAG, "tools/call: Missing params");
      ReplyError(id_int, "Missing params");
      return;
    }
    auto tool_name = cJSON_GetObjectItem(params, "name");
    if (!cJSON_IsString(tool_name)) {
      ESP_LOGE(TAG, "tools/call: Missing name");
      ReplyError(id_int, "Missing name");
      return;
    }
    auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
    if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments)) {
      ESP_LOGE(TAG, "tools/call: Invalid arguments");
      ReplyError(id_int, "Invalid arguments");
      return;
    }
    DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments);
  } else {
    ESP_LOGE(TAG, "Method not implemented: %s", method_str.c_str());
    ReplyError(id_int, "Method not implemented: " + method_str);
  }
}

void McpServer::ReplyResult(int id, const std::string &result) {
  std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
  payload += std::to_string(id) + ",\"result\":";
  payload += result;
  payload += "}";
  Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::ReplyError(int id, const std::string &message) {
  std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
  payload += std::to_string(id);
  payload += ",\"error\":{\"message\":\"";
  payload += message;
  payload += "\"}}";
  Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::GetToolsList(int id, const std::string &cursor,
                             bool list_user_only_tools) {
  const int max_payload_size = 8000;
  std::string json = "{\"tools\":[";

  bool found_cursor = cursor.empty();
  auto it = tools_.begin();
  std::string next_cursor = "";

  while (it != tools_.end()) {
    // 如果我们还没有找到起始位置，继续搜索
    if (!found_cursor) {
      if ((*it)->name() == cursor) {
        found_cursor = true;
      } else {
        ++it;
        continue;
      }
    }

    if (!list_user_only_tools && (*it)->user_only()) {
      ++it;
      continue;
    }

    // 添加tool前检查大小
    std::string tool_json = (*it)->to_json() + ",";
    if (json.length() + tool_json.length() + 30 > max_payload_size) {
      // 如果添加这个tool会超出大小限制，设置next_cursor并退出循环
      next_cursor = (*it)->name();
      break;
    }

    json += tool_json;
    ++it;
  }

  if (json.back() == ',') {
    json.pop_back();
  }

  if (json.back() == '[' && !tools_.empty()) {
    // 如果没有添加任何tool，返回错误
    ESP_LOGE(TAG,
             "tools/list: Failed to add tool %s because of payload size limit",
             next_cursor.c_str());
    ReplyError(id, "Failed to add tool " + next_cursor +
                       " because of payload size limit");
    return;
  }

  if (next_cursor.empty()) {
    json += "]}";
  } else {
    json += "],\"nextCursor\":\"" + next_cursor + "\"}";
  }

  ReplyResult(id, json);
}

void McpServer::DoToolCall(int id, const std::string &tool_name,
                           const cJSON *tool_arguments) {
  auto tool_iter = std::find_if(
      tools_.begin(), tools_.end(),
      [&tool_name](const McpTool *tool) { return tool->name() == tool_name; });

  if (tool_iter == tools_.end()) {
    ESP_LOGE(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
    ReplyError(id, "Unknown tool: " + tool_name);
    return;
  }

  PropertyList arguments = (*tool_iter)->properties();
  try {
    for (auto &argument : arguments) {
      bool found = false;
      if (cJSON_IsObject(tool_arguments)) {
        auto value =
            cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
        if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
          argument.set_value<bool>(value->valueint == 1);
          found = true;
        } else if (argument.type() == kPropertyTypeInteger &&
                   cJSON_IsNumber(value)) {
          argument.set_value<int>(value->valueint);
          found = true;
        } else if (argument.type() == kPropertyTypeString &&
                   cJSON_IsString(value)) {
          argument.set_value<std::string>(value->valuestring);
          found = true;
        }
      }

      if (!argument.has_default_value() && !found) {
        ESP_LOGE(TAG, "tools/call: Missing valid argument: %s",
                 argument.name().c_str());
        ReplyError(id, "Missing valid argument: " + argument.name());
        return;
      }
    }
  } catch (const std::exception &e) {
    ESP_LOGE(TAG, "tools/call: %s", e.what());
    ReplyError(id, e.what());
    return;
  }

  // Use main thread to call the tool
  auto &app = Application::GetInstance();
  app.Schedule([this, id, tool_iter, arguments = std::move(arguments)]() {
    try {
      ReplyResult(id, (*tool_iter)->Call(arguments));
    } catch (const std::exception &e) {
      ESP_LOGE(TAG, "tools/call: %s", e.what());
      ReplyError(id, e.what());
    }
  });
}
