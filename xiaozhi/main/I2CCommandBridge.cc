#include "I2CCommandBridge.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "I2CCommandBridge";

I2CCommandBridge::I2CCommandBridge() 
    : initialized_(false), owns_bus_handle_(false), bus_handle_(nullptr), dev_handle_(nullptr) {
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

    owns_bus_handle_ = true;  // We created the bus, so we own it

    // Configure I2C device (slave address)
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
    ESP_LOGI(TAG, "I2C Command Bridge initialized with own bus (SCL=%d, SDA=%d, Slave=0x%02X)", 
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
    owns_bus_handle_ = false;  // We don't own this bus

    // Configure I2C device (slave address)
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
    ESP_LOGI(TAG, "I2C Command Bridge initialized with shared bus (Slave=0x%02X)", 
             ACTUATOR_ESP32_ADDR);
    return true;
}

void I2CCommandBridge::Deinit() {
    if (!initialized_) return;

    if (dev_handle_) {
        i2c_master_bus_rm_device(dev_handle_);
        dev_handle_ = nullptr;
    }

    // Only delete the bus if we own it
    if (bus_handle_ && owns_bus_handle_) {
        i2c_del_master_bus(bus_handle_);
    }
    bus_handle_ = nullptr;
    owns_bus_handle_ = false;

    initialized_ = false;
    ESP_LOGI(TAG, "I2C Command Bridge deinitialized");
}

std::string I2CCommandBridge::SendVehicleCommand(const std::string& direction, int speed, int duration_ms) {
    // Check if slave is online first to prevent bus hang
    if (!IsSlaveOnline()) {
        ESP_LOGW(TAG, "⚠️ Slave is offline, skipping vehicle command");
        return "{\"error\":\"slave_offline\",\"status\":\"skipped\"}";
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", CMD_TYPE_VEHICLE_MOVE);
    cJSON_AddStringToObject(root, "direction", direction.c_str());
    cJSON_AddNumberToObject(root, "speed", speed);
    cJSON_AddNumberToObject(root, "duration_ms", duration_ms);

    std::string response = SendI2CCommand(root);
    cJSON_Delete(root);
    return response;
}

std::string I2CCommandBridge::SendStorageCommand(int slot, const std::string& action) {
    // Check if slave is online first to prevent bus hang
    if (!IsSlaveOnline()) {
        ESP_LOGW(TAG, "⚠️ Slave is offline, skipping storage command");
        return "{\"error\":\"slave_offline\",\"status\":\"skipped\"}";
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", CMD_TYPE_STORAGE_CONTROL);
    cJSON_AddNumberToObject(root, "slot", slot);
    cJSON_AddStringToObject(root, "action", action.c_str());

    std::string response = SendI2CCommand(root);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "📦 Storage command: slot=%d, action=%s → %s", 
             slot, action.c_str(), response.c_str());
    
    return response;
}

std::string I2CCommandBridge::GetStatus() {
    // Check if slave is online first to prevent bus hang
    if (!IsSlaveOnline()) {
        ESP_LOGW(TAG, "⚠️ Slave is offline, skipping status request");
        return "{\"error\":\"slave_offline\",\"status\":\"skipped\"}";
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", CMD_TYPE_GET_STATUS);

    std::string response = SendI2CCommand(root);
    cJSON_Delete(root);
    return response;
}

std::string I2CCommandBridge::SendVehicleCommandDistance(const std::string& direction, int speed, int distance_mm) {
    // Check if slave is online first to prevent bus hang
    if (!IsSlaveOnline()) {
        ESP_LOGW(TAG, "⚠️ Slave is offline, skipping vehicle distance command");
        return "{\"error\":\"slave_offline\",\"status\":\"skipped\"}";
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", CMD_TYPE_VEHICLE_MOVE);
    cJSON_AddStringToObject(root, "direction", direction.c_str());
    cJSON_AddNumberToObject(root, "speed", speed);
    cJSON_AddNumberToObject(root, "distance_mm", distance_mm);

    std::string response = SendI2CCommand(root);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "🚗 Vehicle distance command: %s %dmm speed=%d → %s", 
             direction.c_str(), distance_mm, speed, response.c_str());
    
    return response;
}

std::string I2CCommandBridge::SendVehicleSequence(const std::vector<std::string>& commands) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "vehicle.sequence");
    
    cJSON* commandsArray = cJSON_CreateArray();
    for (const auto& cmd : commands) {
        cJSON_AddItemToArray(commandsArray, cJSON_CreateString(cmd.c_str()));
    }
    cJSON_AddItemToObject(root, "commands", commandsArray);

    std::string response = SendI2CCommand(root);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "🚗 Vehicle sequence: %d commands → %s", 
             (int)commands.size(), response.c_str());
    
    return response;
}

std::string I2CCommandBridge::SendVehicleDefaultMove(const std::string& direction, int speed) {
    // Mặc định di chuyển 500mm (0.5m)
    return SendVehicleCommandDistance(direction, speed, 500);
}

std::string I2CCommandBridge::SendVehicleUntilObstacle(const std::string& direction, int speed) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", CMD_TYPE_VEHICLE_MOVE);
    cJSON_AddStringToObject(root, "direction", direction.c_str());
    cJSON_AddNumberToObject(root, "speed", speed);
    cJSON_AddBoolToObject(root, "until_obstacle", true);

    std::string response = SendI2CCommand(root);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "🚗 Vehicle until obstacle: %s speed=%d → %s", 
             direction.c_str(), speed, response.c_str());
    
    return response;
}

std::string I2CCommandBridge::SendI2CCommand(cJSON* jsonCmd) {
    if (!initialized_) {
        ESP_LOGE(TAG, "I2C not initialized");
        return "{\"error\":\"not_initialized\"}";
    }

    if (!jsonCmd) {
        ESP_LOGE(TAG, "NULL JSON command");
        return "{\"error\":\"null_command\"}";
    }

    // Convert JSON to string
    char* jsonString = cJSON_PrintUnformatted(jsonCmd);
    if (!jsonString) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return "{\"error\":\"json_serialize_failed\"}";
    }

    size_t jsonLen = strlen(jsonString);
    ESP_LOGI(TAG, "Sending I2C command: %s (len=%d)", jsonString, jsonLen);

    // ========== SIMPLIFIED PROTOCOL: Send JSON directly (no length prefix) ==========
    // I2C master_transmit already handles length internally
    const int SHORT_TIMEOUT_MS = 100;  // Shorter timeout than default
    
    // Send JSON data directly
    esp_err_t err = i2c_master_transmit(dev_handle_, (uint8_t*)jsonString, jsonLen, SHORT_TIMEOUT_MS);
    free(jsonString);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ Failed to send JSON data: %s (slave may be offline)", esp_err_to_name(err));
        // Don't crash, just return error - display will continue working
        return "{\"error\":\"i2c_transmit_data_failed\",\"slave_offline\":true}";
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // Wait for slave to process

    // ========== SIMPLIFIED PROTOCOL: Receive JSON directly (no length prefix) ==========
    uint8_t responseBuffer[256];  // Smaller buffer to reduce garbage data
    memset(responseBuffer, 0, sizeof(responseBuffer));

    // Read response - try smaller chunk first (most responses are < 100 bytes)
    const size_t readSize = 128;  // Read at most 128 bytes
    err = i2c_master_receive(dev_handle_, responseBuffer, readSize, SHORT_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ Failed to receive response: %s (slave may be busy)", esp_err_to_name(err));
        // Return partial success - command was sent but no response
        return "{\"status\":\"sent\",\"response\":\"timeout\"}";
    }

    // Find actual response length by looking for closing brace '}' or null terminator
    size_t respLength = 0;
    bool foundEnd = false;
    
    for (size_t i = 0; i < readSize; i++) {
        if (responseBuffer[i] == '}') {
            // Found end of JSON
            respLength = i + 1;  // Include the closing brace
            foundEnd = true;
            break;
        }
        if (responseBuffer[i] == '\0' && i > 0) {
            // Found null terminator (but not at position 0)
            respLength = i;
            foundEnd = true;
            break;
        }
    }
    
    if (!foundEnd || respLength == 0) {
        ESP_LOGW(TAG, "⚠️ Invalid response - no JSON found");
        return "{\"error\":\"invalid_response\"}";
    }

    // Validate response length is reasonable
    if (respLength > 200) {
        ESP_LOGW(TAG, "⚠️ Response too long (%zu bytes), truncating", respLength);
        respLength = 200;
    }

    // Sanitize buffer - replace any non-printable chars (except valid JSON chars)
    for (size_t i = 0; i < respLength; i++) {
        uint8_t c = responseBuffer[i];
        // Allow: printable ASCII (0x20-0x7E), newline, tab
        if (c < 0x20 && c != '\n' && c != '\t' && c != '\r') {
            responseBuffer[i] = '?';  // Replace invalid chars
        }
        if (c > 0x7E) {
            responseBuffer[i] = '?';  // Replace non-ASCII
        }
    }

    // Null-terminate at the correct position
    responseBuffer[respLength] = '\0';
    
    // Create string safely - use try-catch to handle any issues
    std::string response;
    try {
        response.assign((char*)responseBuffer, respLength);
    } catch (...) {
        ESP_LOGE(TAG, "❌ Failed to create response string");
        return "{\"error\":\"string_creation_failed\"}";
    }
    
    // Log safely - print raw bytes first to debug
    ESP_LOGI(TAG, "✅ Received response (%zu bytes)", respLength);
    
    // Only log content if it looks like valid JSON
    if (response.length() > 0 && response[0] == '{') {
        // Truncate log to avoid issues
        if (response.length() > 100) {
            ESP_LOGI(TAG, "   Content: %.100s...", response.c_str());
        } else {
            ESP_LOGI(TAG, "   Content: %s", response.c_str());
        }
    } else {
        ESP_LOGW(TAG, "   Content: [not JSON]");
    }

    return response;
}

bool I2CCommandBridge::IsSlaveOnline() {
    if (!initialized_) {
        return false;
    }
    
    // Try to probe the slave address with very short timeout
    uint8_t dummy = 0;
    esp_err_t err = i2c_master_transmit(dev_handle_, &dummy, 1, 50);
    
    // ESP_ERR_TIMEOUT or ESP_ERR_NOT_FOUND means slave is offline
    // ESP_OK or ESP_ERR_INVALID_STATE means slave responded (online)
    bool online = (err == ESP_OK);
    
    if (!online) {
        ESP_LOGD(TAG, "Slave probe result: %s (offline)", esp_err_to_name(err));
    }
    
    return online;
}

bool I2CCommandBridge::SendRawData(const uint8_t* data, size_t size) {
    if (!initialized_) {
        ESP_LOGE(TAG, "I2C not initialized");
        return false;
    }

    esp_err_t err = i2c_master_transmit(dev_handle_, data, size, I2C_MASTER_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send raw data: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

int I2CCommandBridge::ReceiveResponse(uint8_t* buffer, size_t maxSize) {
    if (!initialized_) {
        ESP_LOGE(TAG, "I2C not initialized");
        return -1;
    }

    esp_err_t err = i2c_master_receive(dev_handle_, buffer, maxSize, I2C_MASTER_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive data: %s", esp_err_to_name(err));
        return -1;
    }

    return maxSize;
}
