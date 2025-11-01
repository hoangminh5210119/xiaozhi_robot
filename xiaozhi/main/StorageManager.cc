#include "StorageManager.h"
#include "esp_log.h"
#include <algorithm>
#include <cctype>
#include <sys/time.h>
#include <sys/stat.h>
#include <cstring>

const char* StorageManager::TAG = "StorageManager";

StorageManager::StorageManager() 
    : i2c_bridge_(nullptr), initialized_(false), 
      storage_file_path_("/storage/storage.json") {
    // Initialize hardware slots
    for (int i = 0; i < 4; i++) {
        hardware_slots_[i].slot_id = i;
        hardware_slots_[i].is_open = false;
        hardware_slots_[i].has_item = false;
        hardware_slots_[i].default_item = "";
    }
}

StorageManager::~StorageManager() {
    SaveToFile();
}

bool StorageManager::Init(I2CCommandBridge* i2c_bridge) {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }
    
    if (!i2c_bridge) {
        ESP_LOGE(TAG, "I2C bridge is null");
        return false;
    }
    
    i2c_bridge_ = i2c_bridge;
    initialized_ = true;
    
    // Load storage data
    LoadFromFile(storage_file_path_);
    
    ESP_LOGI(TAG, "✅ Storage Manager initialized");
    return true;
}

// ==================== HARDWARE CONTROL ====================

bool StorageManager::OpenHardwareSlot(int slot_id) {
    if (!initialized_ || !i2c_bridge_) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    if (slot_id < 0 || slot_id > 3) {
        ESP_LOGE(TAG, "Invalid slot_id: %d", slot_id);
        return false;
    }
    
    std::string resp = i2c_bridge_->StorageOpen(slot_id);
    bool success = resp.find("error") == std::string::npos;
    
    if (success) {
        hardware_slots_[slot_id].is_open = true;
        NotifyStatus("Đã mở ô " + std::to_string(slot_id + 1));
        ESP_LOGI(TAG, "📦 Opened hardware slot %d", slot_id);
    }
    
    return success;
}

bool StorageManager::CloseHardwareSlot(int slot_id) {
    if (!initialized_ || !i2c_bridge_) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    if (slot_id < 0 || slot_id > 3) {
        ESP_LOGE(TAG, "Invalid slot_id: %d", slot_id);
        return false;
    }
    
    std::string resp = i2c_bridge_->StorageClose(slot_id);
    bool success = resp.find("error") == std::string::npos;
    
    if (success) {
        hardware_slots_[slot_id].is_open = false;
        NotifyStatus("Đã đóng ô " + std::to_string(slot_id + 1));
        ESP_LOGI(TAG, "📦 Closed hardware slot %d", slot_id);
    }
    
    return success;
}

StorageManager::HardwareSlot StorageManager::GetHardwareSlotStatus(int slot_id) {
    if (slot_id < 0 || slot_id > 3) {
        return HardwareSlot();
    }
    
    return hardware_slots_[slot_id];
}

void StorageManager::UpdateHardwareStatus(const ActuatorStatus& status) {
    for (int i = 0; i < 4; i++) {
        hardware_slots_[i].is_open = status.storage[i].is_open;
    }
    
    ESP_LOGD(TAG, "Updated hardware status from actuator");
}

// ==================== ITEM MANAGEMENT ====================

bool StorageManager::StoreItem(const std::string& item_name, const std::string& location, 
                               const std::string& description) {
    if (item_name.empty() || location.empty()) {
        ESP_LOGE(TAG, "Item name or location is empty");
        return false;
    }
    
    std::string norm_name = NormalizeItemName(item_name);
    std::string norm_location = NormalizeLocation(location);
    
    StorageItem item;
    item.name = item_name;
    item.location = norm_location;
    item.description = description;
    item.timestamp = GetCurrentTimestamp();
    
    // Check if location is hardware slot
    int slot_id = -1;
    item.is_hardware_slot = IsHardwareSlotLocation(norm_location, slot_id);
    item.hardware_slot_id = slot_id;
    
    if (item.is_hardware_slot) {
        hardware_slots_[slot_id].has_item = true;
    }
    
    items_[norm_name] = item;
    
    ESP_LOGI(TAG, "📝 Stored: '%s' at '%s' %s", 
             item_name.c_str(), location.c_str(),
             item.is_hardware_slot ? "(hardware)" : "(virtual)");
    
    NotifyStatus("Đã lưu " + item_name + " vào " + location);
    
    // Auto save
    SaveToFile();
    
    return true;
}

bool StorageManager::RemoveItem(const std::string& item_name) {
    std::string norm_name = NormalizeItemName(item_name);
    
    auto it = items_.find(norm_name);
    if (it == items_.end()) {
        ESP_LOGW(TAG, "Item '%s' not found", item_name.c_str());
        return false;
    }
    
    // Update hardware slot if needed
    if (it->second.is_hardware_slot && it->second.hardware_slot_id >= 0) {
        hardware_slots_[it->second.hardware_slot_id].has_item = false;
    }
    
    items_.erase(it);
    
    ESP_LOGI(TAG, "🗑️ Removed: '%s'", item_name.c_str());
    NotifyStatus("Đã xóa " + item_name);
    
    SaveToFile();
    
    return true;
}

std::string StorageManager::FindItemLocation(const std::string& item_name) {
    std::string norm_name = NormalizeItemName(item_name);
    
    auto it = items_.find(norm_name);
    if (it == items_.end()) {
        return "";
    }
    
    return it->second.location;
}

StorageManager::StorageItem StorageManager::GetItemInfo(const std::string& item_name) {
    std::string norm_name = NormalizeItemName(item_name);
    
    auto it = items_.find(norm_name);
    if (it == items_.end()) {
        return StorageItem();
    }
    
    return it->second;
}

std::vector<StorageManager::StorageItem> StorageManager::GetAllItems() {
    std::vector<StorageItem> result;
    for (const auto& pair : items_) {
        result.push_back(pair.second);
    }
    return result;
}

std::vector<StorageManager::StorageItem> StorageManager::GetItemsInSlot(int slot_id) {
    std::vector<StorageItem> result;
    std::string slot_location = SlotIdToLocation(slot_id);
    
    for (const auto& pair : items_) {
        if (pair.second.hardware_slot_id == slot_id) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<StorageManager::StorageItem> StorageManager::GetItemsAtLocation(const std::string& location) {
    std::vector<StorageItem> result;
    std::string norm_location = NormalizeLocation(location);
    
    for (const auto& pair : items_) {
        if (pair.second.location == norm_location) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

bool StorageManager::MoveItem(const std::string& item_name, const std::string& new_location) {
    std::string norm_name = NormalizeItemName(item_name);
    
    auto it = items_.find(norm_name);
    if (it == items_.end()) {
        ESP_LOGW(TAG, "Item '%s' not found", item_name.c_str());
        return false;
    }
    
    // Update old hardware slot if needed
    if (it->second.is_hardware_slot && it->second.hardware_slot_id >= 0) {
        hardware_slots_[it->second.hardware_slot_id].has_item = false;
    }
    
    std::string norm_location = NormalizeLocation(new_location);
    it->second.location = norm_location;
    it->second.timestamp = GetCurrentTimestamp();
    
    // Check if new location is hardware slot
    int slot_id = -1;
    it->second.is_hardware_slot = IsHardwareSlotLocation(norm_location, slot_id);
    it->second.hardware_slot_id = slot_id;
    
    if (it->second.is_hardware_slot) {
        hardware_slots_[slot_id].has_item = true;
    }
    
    ESP_LOGI(TAG, "🔄 Moved: '%s' to '%s'", item_name.c_str(), new_location.c_str());
    NotifyStatus("Đã chuyển " + item_name + " sang " + new_location);
    
    SaveToFile();
    
    return true;
}

bool StorageManager::HasItem(const std::string& item_name) {
    std::string norm_name = NormalizeItemName(item_name);
    return items_.find(norm_name) != items_.end();
}

// ==================== NATURAL LANGUAGE PROCESSING ====================

std::string StorageManager::ProcessNaturalCommand(const std::string& command) {
    std::string cmd_lower = command;
    std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(), ::tolower);
    
    // Mở ô: "mở ô 1", "mở ô số 2"
    if (cmd_lower.find("mở") != std::string::npos && cmd_lower.find("ô") != std::string::npos) {
        for (int i = 1; i <= 4; i++) {
            if (cmd_lower.find(std::to_string(i)) != std::string::npos) {
                if (OpenHardwareSlot(i - 1)) {
                    return "Đang mở ô " + std::to_string(i);
                } else {
                    return "Không thể mở ô " + std::to_string(i);
                }
            }
        }
        return "Không hiểu ô nào cần mở";
    }
    
    // Đóng ô: "đóng ô 1", "đóng ô số 2"
    if (cmd_lower.find("đóng") != std::string::npos && cmd_lower.find("ô") != std::string::npos) {
        for (int i = 1; i <= 4; i++) {
            if (cmd_lower.find(std::to_string(i)) != std::string::npos) {
                if (CloseHardwareSlot(i - 1)) {
                    return "Đang đóng ô " + std::to_string(i);
                } else {
                    return "Không thể đóng ô " + std::to_string(i);
                }
            }
        }
        return "Không hiểu ô nào cần đóng";
    }
    
    // Lưu vật phẩm: "để kính vào ô 1", "tôi để chìa khóa trên bàn"
    if (cmd_lower.find("để") != std::string::npos || cmd_lower.find("đặt") != std::string::npos) {
        size_t vao_pos = cmd_lower.find("vào");
        size_t tren_pos = cmd_lower.find("trên");
        size_t trong_pos = cmd_lower.find("trong");
        
        std::string item_name;
        std::string location;
        
        if (vao_pos != std::string::npos) {
            // Extract item name (between "để/đặt" and "vào")
            size_t start = cmd_lower.find("để");
            if (start == std::string::npos) start = cmd_lower.find("đặt");
            item_name = command.substr(start + 3, vao_pos - start - 3);
            location = command.substr(vao_pos + 4);
        } else if (tren_pos != std::string::npos) {
            size_t start = cmd_lower.find("để");
            if (start == std::string::npos) start = cmd_lower.find("đặt");
            item_name = command.substr(start + 3, tren_pos - start - 3);
            location = command.substr(tren_pos);
        } else if (trong_pos != std::string::npos) {
            size_t start = cmd_lower.find("để");
            if (start == std::string::npos) start = cmd_lower.find("đặt");
            item_name = command.substr(start + 3, trong_pos - start - 3);
            location = command.substr(trong_pos);
        }
        
        // Trim whitespace
        item_name.erase(0, item_name.find_first_not_of(" \t"));
        item_name.erase(item_name.find_last_not_of(" \t") + 1);
        location.erase(0, location.find_first_not_of(" \t"));
        location.erase(location.find_last_not_of(" \t") + 1);
        
        if (!item_name.empty() && !location.empty()) {
            if (StoreItem(item_name, location)) {
                return "Đã ghi nhớ " + item_name + " ở " + location;
            }
        }
    }
    
    // Tìm vật phẩm: "kính ở đâu", "chìa khóa của tôi ở đâu"
    if (cmd_lower.find("ở đâu") != std::string::npos || cmd_lower.find("tìm") != std::string::npos) {
        std::string item_name = command;
        
        // Remove question words
        size_t odau_pos = item_name.find("ở đâu");
        if (odau_pos != std::string::npos) {
            item_name = item_name.substr(0, odau_pos);
        }
        
        size_t tim_pos = item_name.find("tìm");
        if (tim_pos != std::string::npos) {
            item_name = item_name.substr(tim_pos + 4);
        }
        
        // Remove "của tôi", "của em"
        size_t cua_pos = item_name.find("của");
        if (cua_pos != std::string::npos) {
            item_name = item_name.substr(0, cua_pos);
        }
        
        // Trim
        item_name.erase(0, item_name.find_first_not_of(" \t"));
        item_name.erase(item_name.find_last_not_of(" \t") + 1);
        
        if (!item_name.empty()) {
            return AnswerLocationQuery(item_name);
        }
    }
    
    // Lấy vật phẩm: "lấy kính", "lấy kính ra"
    if (cmd_lower.find("lấy") != std::string::npos) {
        std::string item_name = command.substr(cmd_lower.find("lấy") + 4);
        
        // Remove "ra"
        size_t ra_pos = item_name.find("ra");
        if (ra_pos != std::string::npos) {
            item_name = item_name.substr(0, ra_pos);
        }
        
        item_name.erase(0, item_name.find_first_not_of(" \t"));
        item_name.erase(item_name.find_last_not_of(" \t") + 1);
        
        if (!item_name.empty()) {
            std::string location = FindItemLocation(item_name);
            if (!location.empty()) {
                int slot_id = -1;
                if (IsHardwareSlotLocation(location, slot_id)) {
                    OpenHardwareSlot(slot_id);
                    return "Đang mở ô " + std::to_string(slot_id + 1) + " để lấy " + item_name;
                } else {
                    return item_name + " ở " + location + ", không phải trong ô cứng";
                }
            } else {
                return "Không tìm thấy " + item_name;
            }
        }
    }
    
    return "Xin lỗi, tôi không hiểu lệnh này";
}

std::string StorageManager::AnswerLocationQuery(const std::string& item_name) {
    std::string location = FindItemLocation(item_name);
    
    if (location.empty()) {
        // Try fuzzy search
        std::string norm_query = NormalizeItemName(item_name);
        for (const auto& pair : items_) {
            if (pair.first.find(norm_query) != std::string::npos) {
                return pair.second.name + " ở " + pair.second.location;
            }
        }
        return "Tôi không nhớ " + item_name + " ở đâu";
    }
    
    StorageItem item = GetItemInfo(item_name);
    
    std::string answer = item.name + " ở " + item.location;
    
    if (item.is_hardware_slot) {
        answer += " (ô số " + std::to_string(item.hardware_slot_id + 1) + ")";
    }
    
    if (!item.description.empty()) {
        answer += " - " + item.description;
    }
    
    return answer;
}

// ==================== CONFIGURATION ====================

bool StorageManager::SetDefaultSlotItem(int slot_id, const std::string& item_name) {
    if (slot_id < 0 || slot_id > 3) {
        return false;
    }
    
    hardware_slots_[slot_id].default_item = item_name;
    ESP_LOGI(TAG, "Set default item for slot %d: %s", slot_id, item_name.c_str());
    
    SaveToFile();
    return true;
}

void StorageManager::SetStatusCallback(StatusCallback callback) {
    status_callback_ = callback;
}

int StorageManager::GetHardwareItemCount() {
    int count = 0;
    for (const auto& pair : items_) {
        if (pair.second.is_hardware_slot) {
            count++;
        }
    }
    return count;
}

int StorageManager::GetVirtualItemCount() {
    int count = 0;
    for (const auto& pair : items_) {
        if (!pair.second.is_hardware_slot) {
            count++;
        }
    }
    return count;
}

// ==================== FILE I/O ====================

bool StorageManager::SaveToFile(const std::string& filepath) {
    std::string file_path = filepath.empty() ? storage_file_path_ : filepath;
    
    if (!initialized_ || file_path.empty()) {
        ESP_LOGW(TAG, "Not initialized or no file path");
        return false;
    }
    
    cJSON* root = cJSON_CreateObject();
    
    // Save items
    cJSON* items_array = cJSON_CreateArray();
    for (const auto& pair : items_) {
        const StorageItem& item = pair.second;
        
        cJSON* item_json = cJSON_CreateObject();
        cJSON_AddStringToObject(item_json, "name", item.name.c_str());
        cJSON_AddStringToObject(item_json, "location", item.location.c_str());
        cJSON_AddBoolToObject(item_json, "is_hardware", item.is_hardware_slot);
        cJSON_AddNumberToObject(item_json, "slot_id", item.hardware_slot_id);
        cJSON_AddStringToObject(item_json, "description", item.description.c_str());
        cJSON_AddNumberToObject(item_json, "timestamp", item.timestamp);
        
        cJSON_AddItemToArray(items_array, item_json);
    }
    cJSON_AddItemToObject(root, "items", items_array);
    
    // Save hardware slot configs
    cJSON* slots_array = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON* slot_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(slot_json, "slot_id", hardware_slots_[i].slot_id);
        cJSON_AddStringToObject(slot_json, "default_item", hardware_slots_[i].default_item.c_str());
        
        cJSON_AddItemToArray(slots_array, slot_json);
    }
    cJSON_AddItemToObject(root, "hardware_slots", slots_array);
    
    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to create JSON");
        return false;
    }
    
    FILE* f = fopen(file_path.c_str(), "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path.c_str());
        cJSON_free(json_str);
        return false;
    }
    
    size_t written = fwrite(json_str, 1, strlen(json_str), f);
    fclose(f);
    cJSON_free(json_str);
    
    if (written == 0) {
        ESP_LOGE(TAG, "Failed to write to file");
        return false;
    }
    
    ESP_LOGI(TAG, "💾 Saved %d items to file", (int)items_.size());
    return true;
}

bool StorageManager::LoadFromFile(const std::string& filepath) {
    std::string file_path = filepath.empty() ? storage_file_path_ : filepath;
    
    if (!initialized_ || file_path.empty()) {
        ESP_LOGW(TAG, "Not initialized or no file path");
        return false;
    }
    
    struct stat st;
    if (stat(file_path.c_str(), &st) != 0) {
        ESP_LOGW(TAG, "Storage file does not exist, starting fresh");
        return false;
    }
    
    FILE* f = fopen(file_path.c_str(), "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path.c_str());
        return false;
    }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize <= 0) {
        fclose(f);
        ESP_LOGW(TAG, "File is empty");
        return false;
    }
    
    char* json_str = (char*)malloc(fsize + 1);
    size_t read_size = fread(json_str, 1, fsize, f);
    fclose(f);
    json_str[read_size] = '\0';
    
    if (read_size == 0) {
        free(json_str);
        ESP_LOGE(TAG, "Failed to read file");
        return false;
    }
    
    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return false;
    }
    
    // Load items
    cJSON* items_array = cJSON_GetObjectItem(root, "items");
    if (items_array) {
        cJSON* item_json = nullptr;
        cJSON_ArrayForEach(item_json, items_array) {
            StorageItem item;
            
            cJSON* name = cJSON_GetObjectItem(item_json, "name");
            cJSON* location = cJSON_GetObjectItem(item_json, "location");
            cJSON* is_hw = cJSON_GetObjectItem(item_json, "is_hardware");
            cJSON* slot_id = cJSON_GetObjectItem(item_json, "slot_id");
            cJSON* desc = cJSON_GetObjectItem(item_json, "description");
            cJSON* ts = cJSON_GetObjectItem(item_json, "timestamp");
            
            if (name) item.name = name->valuestring;
            if (location) item.location = location->valuestring;
            if (is_hw) item.is_hardware_slot = cJSON_IsTrue(is_hw);
            if (slot_id) item.hardware_slot_id = slot_id->valueint;
            if (desc) item.description = desc->valuestring;
            if (ts) item.timestamp = ts->valuedouble;
            
            std::string norm_name = NormalizeItemName(item.name);
            items_[norm_name] = item;
            
            if (item.is_hardware_slot && item.hardware_slot_id >= 0 && item.hardware_slot_id < 4) {
                hardware_slots_[item.hardware_slot_id].has_item = true;
            }
            
            ESP_LOGI(TAG, "Loaded: %s at %s", item.name.c_str(), item.location.c_str());
        }
    }
    
    // Load hardware slot configs
    cJSON* slots_array = cJSON_GetObjectItem(root, "hardware_slots");
    if (slots_array) {
        cJSON* slot_json = nullptr;
        cJSON_ArrayForEach(slot_json, slots_array) {
            cJSON* slot_id = cJSON_GetObjectItem(slot_json, "slot_id");
            cJSON* default_item = cJSON_GetObjectItem(slot_json, "default_item");
            
            if (slot_id && slot_id->valueint >= 0 && slot_id->valueint < 4) {
                if (default_item) {
                    hardware_slots_[slot_id->valueint].default_item = default_item->valuestring;
                }
            }
        }
    }
    
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "📂 Loaded %d items from file", (int)items_.size());
    return true;
}

// ==================== HELPER FUNCTIONS ====================

std::string StorageManager::NormalizeItemName(const std::string& name) {
    std::string result = name;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    
    // Trim whitespace
    result.erase(0, result.find_first_not_of(" \t\n\r"));
    result.erase(result.find_last_not_of(" \t\n\r") + 1);
    
    return result;
}

std::string StorageManager::NormalizeLocation(const std::string& location) {
    std::string result = location;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    
    // Trim whitespace
    result.erase(0, result.find_first_not_of(" \t\n\r"));
    result.erase(result.find_last_not_of(" \t\n\r") + 1);
    
    // Convert various slot formats to standard format
    // "ô 1", "ô số 1", "ô 0", "slot 0" -> "slot_0"
    for (int i = 0; i <= 4; i++) {
        std::string patterns[] = {
            "ô " + std::to_string(i),
            "ô số " + std::to_string(i),
            "slot " + std::to_string(i),
            "slot" + std::to_string(i)
        };
        
        for (const auto& pattern : patterns) {
            if (result.find(pattern) != std::string::npos) {
                // Convert 1-based to 0-based if needed
                int slot_id = (i == 0) ? 0 : i - 1;
                if (i >= 1 && i <= 4) {
                    slot_id = i - 1;
                }
                return "slot_" + std::to_string(slot_id);
            }
        }
    }
    
    return result;
}

bool StorageManager::IsHardwareSlotLocation(const std::string& location, int& slot_id) {
    // Check format: "slot_0", "slot_1", "slot_2", "slot_3"
    if (location.find("slot_") == 0 && location.length() == 6) {
        char id_char = location[5];
        if (id_char >= '0' && id_char <= '3') {
            slot_id = id_char - '0';
            return true;
        }
    }
    
    slot_id = -1;
    return false;
}

std::string StorageManager::SlotIdToLocation(int slot_id) {
    if (slot_id >= 0 && slot_id <= 3) {
        return "slot_" + std::to_string(slot_id);
    }
    return "";
}

uint64_t StorageManager::GetCurrentTimestamp() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

void StorageManager::NotifyStatus(const std::string& message) {
    if (status_callback_) {
        status_callback_(message);
    }
}