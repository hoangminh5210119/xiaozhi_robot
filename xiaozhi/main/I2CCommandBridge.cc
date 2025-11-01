#include "I2CCommandBridge.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "I2CCommandBridge";

I2CCommandBridge::I2CCommandBridge() 
    : initialized_(false), owns_bus_handle_(false), bus_handle_(nullptr), dev_handle_(nullptr),
      polling_active_(false), polling_interval_ms_(1000), polling_task_handle_(nullptr),
      status_callback_(nullptr), callback_user_data_(nullptr) {
}

I2CCommandBridge::~I2CCommandBridge() {
    Deinit();
}

bool I2CCommandBridge::Init() {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    // Configure I2C master bus
    i2c_master_bus_config_t bus_config = {};
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.scl_io_num = static_cast<gpio_num_t>(I2C_MASTER_SCL_IO);
    bus_config.sda_io_num = static_cast<gpio_num_t>(I2C_MASTER_SDA_IO);
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(err));
        return false;
    }

    owns_bus_handle_ = true;

    // Configure I2C device
    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = ACTUATOR_ESP32_ADDR;
    dev_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;

    err = i2c_master_bus_add_device(bus_handle_, &dev_config, &dev_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(err));
        i2c_del_master_bus(bus_handle_);
        bus_handle_ = nullptr;
        owns_bus_handle_ = false;
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "✅ I2C Command Bridge initialized (SCL=%d, SDA=%d, Slave=0x%02X)", 
             I2C_MASTER_SCL_IO, I2C_MASTER_SDA_IO, ACTUATOR_ESP32_ADDR);
    return true;
}

bool I2CCommandBridge::InitWithExistingBus(i2c_master_bus_handle_t existing_bus_handle) {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    if (existing_bus_handle == nullptr) {
        ESP_LOGE(TAG, "Invalid existing bus handle");
        return false;
    }

    bus_handle_ = existing_bus_handle;
    owns_bus_handle_ = false;

    // Configure I2C device
    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = ACTUATOR_ESP32_ADDR;
    dev_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;

    esp_err_t err = i2c_master_bus_add_device(bus_handle_, &dev_config, &dev_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device to existing bus: %s", esp_err_to_name(err));
        bus_handle_ = nullptr;
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "✅ I2C Command Bridge initialized with shared bus (Slave=0x%02X)", 
             ACTUATOR_ESP32_ADDR);
    return true;
}

void I2CCommandBridge::Deinit() {
    if (!initialized_) return;

    // Stop polling first
    StopStatusPolling();

    if (dev_handle_) {
        i2c_master_bus_rm_device(dev_handle_);
        dev_handle_ = nullptr;
    }

    if (bus_handle_ && owns_bus_handle_) {
        i2c_del_master_bus(bus_handle_);
    }
    bus_handle_ = nullptr;
    owns_bus_handle_ = false;

    initialized_ = false;
    ESP_LOGI(TAG, "I2C Command Bridge deinitialized");
}

// ==================== VEHICLE CONTROL ====================

std::string I2CCommandBridge::VehicleMoveTime(int direction, int speed_percent, int duration_ms) {
    if (!IsSlaveOnline()) {
        ESP_LOGW(TAG, "⚠️ Slave offline, skipping vehicle command");
        return "{\"error\":\"slave_offline\"}";
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, KEY_TYPE, TYPE_VEHICLE);
    cJSON_AddNumberToObject(root, KEY_DIR, direction);
    cJSON_AddNumberToObject(root, KEY_SPEED, speed_percent);
    if (duration_ms > 0) {
        cJSON_AddNumberToObject(root, KEY_DURATION, duration_ms);
    }

    std::string response = SendI2CCommand(root);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "🚗 Vehicle: dir=%d speed=%d%% time=%dms → %s", 
             direction, speed_percent, duration_ms, response.c_str());
    
    return response;
}

std::string I2CCommandBridge::VehicleMoveDistance(int direction, int speed_percent, int distance_mm) {
    if (!IsSlaveOnline()) {
        ESP_LOGW(TAG, "⚠️ Slave offline, skipping vehicle command");
        return "{\"error\":\"slave_offline\"}";
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, KEY_TYPE, TYPE_VEHICLE);
    cJSON_AddNumberToObject(root, KEY_DIR, direction);
    cJSON_AddNumberToObject(root, KEY_SPEED, speed_percent);
    cJSON_AddNumberToObject(root, KEY_DISTANCE, distance_mm);

    std::string response = SendI2CCommand(root);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "� Vehicle: dir=%d speed=%d%% dist=%dmm → %s", 
             direction, speed_percent, distance_mm, response.c_str());
    
    return response;
}

std::string I2CCommandBridge::VehicleStop() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, KEY_TYPE, TYPE_VEHICLE);
    cJSON_AddNumberToObject(root, KEY_DIR, DIR_STOP);

    std::string response = SendI2CCommand(root);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "🛑 Vehicle: STOP → %s", response.c_str());
    
    return response;
}

// ==================== STORAGE CONTROL ====================

std::string I2CCommandBridge::StorageControl(int slot, int action) {
    if (!IsSlaveOnline()) {
        ESP_LOGW(TAG, "⚠️ Slave offline, skipping storage command");
        return "{\"error\":\"slave_offline\"}";
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, KEY_TYPE, TYPE_STORAGE);
    cJSON_AddNumberToObject(root, KEY_SLOT, slot);
    cJSON_AddNumberToObject(root, KEY_ACTION, action);

    std::string response = SendI2CCommand(root);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "� Storage: slot=%d action=%s → %s", 
             slot, (action == ACT_OPEN ? "OPEN" : "CLOSE"), response.c_str());
    
    return response;
}

std::string I2CCommandBridge::StorageOpen(int slot) {
    return StorageControl(slot, ACT_OPEN);
}

std::string I2CCommandBridge::StorageClose(int slot) {
    return StorageControl(slot, ACT_CLOSE);
}

// ==================== STATUS ====================

std::string I2CCommandBridge::GetStatus() {
    if (!IsSlaveOnline()) {
        ESP_LOGW(TAG, "⚠️ Slave offline, skipping status request");
        return "{\"error\":\"slave_offline\"}";
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, KEY_TYPE, TYPE_STATUS);

    std::string response = SendI2CCommand(root);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "� Status: %s", response.c_str());
    
    return response;
}

// ==================== LEGACY COMPATIBILITY ====================

std::string I2CCommandBridge::SendVehicleCommand(const std::string& direction, int speed, int duration_ms) {
    int dir = DIR_STOP;
    
    if (direction == "forward") dir = DIR_FORWARD;
    else if (direction == "backward") dir = DIR_BACKWARD;
    else if (direction == "left") dir = DIR_LEFT;
    else if (direction == "right") dir = DIR_RIGHT;
    else if (direction == "rotate_left") dir = DIR_ROTATE_LEFT;
    else if (direction == "rotate_right") dir = DIR_ROTATE_RIGHT;
    else if (direction == "stop") dir = DIR_STOP;
    
    if (dir == DIR_STOP) {
        return VehicleStop();
    }
    
    return VehicleMoveTime(dir, speed, duration_ms);
}

std::string I2CCommandBridge::SendStorageCommand(int slot, const std::string& action) {
    int act = ACT_CLOSE;
    
    if (action == "open") act = ACT_OPEN;
    else if (action == "close") act = ACT_CLOSE;
    
    return StorageControl(slot, act);
}

// ==================== I2C COMMUNICATION ====================


std::string I2CCommandBridge::SendI2CCommand(cJSON* jsonCmd) {
    if (!initialized_) {
        ESP_LOGE(TAG, "I2C not initialized");
        return "{\"error\":\"not_initialized\"}";
    }

    if (!jsonCmd) {
        ESP_LOGE(TAG, "NULL JSON command");
        return "{\"error\":\"null_command\"}";
    }

    // Convert JSON to compact string
    char* jsonString = cJSON_PrintUnformatted(jsonCmd);
    if (!jsonString) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return "{\"error\":\"json_serialize_failed\"}";
    }

    size_t jsonLen = strlen(jsonString);
    ESP_LOGI(TAG, "📤 Sending: %s (len=%d)", jsonString, jsonLen);

    // Send JSON directly
    const int TIMEOUT_MS = 100;
    esp_err_t err = i2c_master_transmit(dev_handle_, (uint8_t*)jsonString, jsonLen, TIMEOUT_MS);
    free(jsonString);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ Failed to send command: %s", esp_err_to_name(err));
        return "{\"error\":\"i2c_transmit_failed\",\"slave_offline\":true}";
    }

    // Wait for slave to process
    vTaskDelay(pdMS_TO_TICKS(50));

    // Receive response
    uint8_t responseBuffer[128];
    memset(responseBuffer, 0, sizeof(responseBuffer));

    err = i2c_master_receive(dev_handle_, responseBuffer, sizeof(responseBuffer), TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ Failed to receive response: %s", esp_err_to_name(err));
        return "{\"status\":\"sent\",\"response\":\"timeout\"}";
    }

    // Find JSON end by looking for '}'
    size_t respLength = 0;
    for (size_t i = 0; i < sizeof(responseBuffer); i++) {
        if (responseBuffer[i] == '}') {
            respLength = i + 1;
            break;
        }
        if (responseBuffer[i] == '\0' && i > 0) {
            respLength = i;
            break;
        }
    }

    if (respLength == 0 || respLength > 100) {
        ESP_LOGW(TAG, "⚠️ Invalid response length: %zu", respLength);
        return "{\"error\":\"invalid_response\"}";
    }

    // Sanitize response - replace non-printable chars
    for (size_t i = 0; i < respLength; i++) {
        uint8_t c = responseBuffer[i];
        if ((c < 0x20 && c != '\n' && c != '\t' && c != '\r') || c > 0x7E) {
            responseBuffer[i] = '?';
        }
    }

    responseBuffer[respLength] = '\0';
    
    std::string response;
    try {
        response.assign((char*)responseBuffer, respLength);
    } catch (...) {
        ESP_LOGE(TAG, "❌ Failed to create response string");
        return "{\"error\":\"string_creation_failed\"}";
    }
    
    ESP_LOGI(TAG, "📥 Response: %s", response.c_str());

    return response;
}

bool I2CCommandBridge::IsSlaveOnline() {
    if (!initialized_) {
        return false;
    }
    
    // Probe slave with minimal data
    uint8_t dummy = 0;
    esp_err_t err = i2c_master_transmit(dev_handle_, &dummy, 1, 50);
    
    bool online = (err == ESP_OK);
    
    if (!online) {
        ESP_LOGD(TAG, "Slave probe: %s (offline)", esp_err_to_name(err));
    }
    
    return online;
}

// ==================== STATUS CALLBACK & POLLING ====================

void I2CCommandBridge::SetStatusCallback(ActuatorStatusCallback callback, void* user_data) {
    status_callback_ = callback;
    callback_user_data_ = user_data;
    ESP_LOGI(TAG, "Status callback registered");
}

bool I2CCommandBridge::StartStatusPolling(uint32_t interval_ms) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Cannot start polling - not initialized");
        return false;
    }
    
    if (polling_active_) {
        ESP_LOGW(TAG, "Polling already active");
        return true;
    }
    
    if (!status_callback_) {
        ESP_LOGW(TAG, "No callback registered, polling will have no effect");
    }
    
    polling_interval_ms_ = interval_ms;
    polling_active_ = true;
    
    // Create FreeRTOS task for polling
    BaseType_t result = xTaskCreate(
        StatusPollingTask,
        "StatusPolling",
        4096,  // Stack size
        this,  // Pass 'this' pointer as parameter
        5,     // Priority
        &polling_task_handle_
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create polling task");
        polling_active_ = false;
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Status polling started (interval=%dms)", interval_ms);
    return true;
}

void I2CCommandBridge::StopStatusPolling() {
    if (!polling_active_) {
        return;
    }
    
    polling_active_ = false;
    
    // Wait for task to finish
    if (polling_task_handle_) {
        vTaskDelay(pdMS_TO_TICKS(100)); // Give task time to exit
        vTaskDelete(polling_task_handle_);
        polling_task_handle_ = nullptr;
    }
    
    ESP_LOGI(TAG, "Status polling stopped");
}

void I2CCommandBridge::StatusPollingTask(void* param) {
    I2CCommandBridge* bridge = static_cast<I2CCommandBridge*>(param);
    
    ESP_LOGI(TAG, "📊 Status polling task started");
    
    while (bridge->polling_active_) {
        // Check if slave is online first
        if (!bridge->IsSlaveOnline()) {
            ESP_LOGD(TAG, "Slave offline, skipping poll");
            vTaskDelay(pdMS_TO_TICKS(bridge->polling_interval_ms_));
            continue;
        }
        
        // Request status
        std::string response = bridge->GetStatus();
        
        // Parse and callback if successful
        if (response.find("error") == std::string::npos && bridge->status_callback_) {
            ActuatorStatus status;
            if (bridge->ParseStatusResponse(response, status)) {
                bridge->status_callback_(status, bridge->callback_user_data_);
            }
        }
        
        // Wait for next poll
        vTaskDelay(pdMS_TO_TICKS(bridge->polling_interval_ms_));
    }
    
    ESP_LOGI(TAG, "📊 Status polling task exiting");
    vTaskDelete(NULL);
}

bool I2CCommandBridge::ParseStatusResponse(const std::string& jsonResponse, ActuatorStatus& status) {
    cJSON* root = cJSON_Parse(jsonResponse.c_str());
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse status JSON");
        return false;
    }
    
    // Parse main fields
    cJSON* s = cJSON_GetObjectItem(root, "s");
    cJSON* b = cJSON_GetObjectItem(root, "b");
    cJSON* c = cJSON_GetObjectItem(root, "c");
    cJSON* h = cJSON_GetObjectItem(root, "h");
    cJSON* m = cJSON_GetObjectItem(root, "m");
    cJSON* v = cJSON_GetObjectItem(root, "v");
    
    status.status = s ? s->valueint : -1;
    status.battery = b ? (float)b->valuedouble : 0.0f;
    status.ble_connected = c ? (c->valueint == 1) : false;
    status.heart_rate = h ? h->valueint : 0;
    status.motor_enabled = m ? (m->valueint == 1) : false;
    status.is_moving = v ? (v->valueint == 1) : false;
    
    // Parse storage array
    cJSON* storage = cJSON_GetObjectItem(root, "g");
    if (storage && cJSON_IsArray(storage)) {
        int count = cJSON_GetArraySize(storage);
        for (int i = 0; i < count && i < 4; i++) {
            cJSON* item = cJSON_GetArrayItem(storage, i);
            if (item) {
                cJSON* slot = cJSON_GetObjectItem(item, "i");
                cJSON* state = cJSON_GetObjectItem(item, "o");
                
                int slot_num = slot ? slot->valueint : i;
                if (slot_num >= 0 && slot_num < 4) {
                    status.storage[slot_num].slot = slot_num;
                    status.storage[slot_num].is_open = state ? (state->valueint == 1) : false;
                }
            }
        }
    }
    
    cJSON_Delete(root);
    
    ESP_LOGD(TAG, "📊 Parsed status: battery=%.1fV, HR=%d, moving=%d, ble=%d", 
             status.battery, status.heart_rate, status.is_moving, status.ble_connected);
    
    return true;
}
