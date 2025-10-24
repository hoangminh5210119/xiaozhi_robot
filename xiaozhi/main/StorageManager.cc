#include "StorageManager.h"
#include "esp_log.h"
#include <algorithm>
#include <sys/time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <cstring>

static const char *TAG = "StorageManager";

StorageManager::StorageManager() : filePath(""), initialized(false) {}

StorageManager::~StorageManager() {
  turnOffAllLEDs();
}

bool StorageManager::begin(const char *storagePath) {
  if (initialized) {
    ESP_LOGW(TAG, "Already initialized");
    return true;
  }

  this->filePath = storagePath;
  initialized = true;

  // Auto-load slots từ file
  if (loadFromFile()) {
    ESP_LOGI(TAG, "Loaded %d slots from file", getTotalSlotCount());
  } else {
    ESP_LOGI(TAG, "No slots found in file or load failed");
  }

  return true;
}

int64_t StorageManager::getCurrentUnixTime() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec;
}

const char *StorageManager::getStatusName(SlotStatus status) {
  return status == EMPTY ? "empty" : "occupied";
}

bool StorageManager::initGPIO(gpio_num_t pin) {
  if (pin == GPIO_NUM_NC) {
    return false;
  }

  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << pin);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", pin, esp_err_to_name(err));
    return false;
  }

  // Tắt LED ban đầu
  gpio_set_level(pin, 0);

  return true;
}

bool StorageManager::addSlot(int index, gpio_num_t ledPin) {
  if (slots.find(index) != slots.end()) {
    ESP_LOGW(TAG, "Slot index %d already exists", index);
    return false;
  }

  // Khởi tạo GPIO
  if (!initGPIO(ledPin)) {
    ESP_LOGE(TAG, "Failed to initialize GPIO for slot %d", index);
    return false;
  }

  Slot slot(index, ledPin);
  slots[index] = slot;

  ESP_LOGI(TAG, "Added slot index=%d, pin=%d", index, ledPin);
  return true;
}

bool StorageManager::putItem(int index, const std::string &item,
                             bool autoSave) {
  auto it = slots.find(index);
  if (it == slots.end()) {
    ESP_LOGW(TAG, "Slot index %d not found", index);
    return false;
  }

  Slot &slot = it->second;
  slot.status = OCCUPIED;
  slot.item = item;
  slot.lastUpdated = getCurrentUnixTime();

  if (autoSave)
    saveToFile();

  ESP_LOGI(TAG, "Put item '%s' into slot %d", item.c_str(), index);
  return true;
}

bool StorageManager::takeItem(int index, bool autoSave) {
  auto it = slots.find(index);
  if (it == slots.end()) {
    ESP_LOGW(TAG, "Slot index %d not found", index);
    return false;
  }

  Slot &slot = it->second;
  std::string oldItem = slot.item;

  slot.status = EMPTY;
  slot.item = "";
  slot.lastUpdated = getCurrentUnixTime();

  if (autoSave)
    saveToFile();

  ESP_LOGI(TAG, "Took item '%s' from slot %d", oldItem.c_str(), index);
  return true;
}

bool StorageManager::updateItem(int index, const std::string &item,
                                bool autoSave) {
  auto it = slots.find(index);
  if (it == slots.end()) {
    ESP_LOGW(TAG, "Slot index %d not found", index);
    return false;
  }

  Slot &slot = it->second;
  slot.item = item;
  slot.status = item.empty() ? EMPTY : OCCUPIED;
  slot.lastUpdated = getCurrentUnixTime();

  if (autoSave)
    saveToFile();

  ESP_LOGI(TAG, "Updated slot %d with item '%s'", index, item.c_str());
  return true;
}

bool StorageManager::getSlot(int index, Slot &slot) {
  auto it = slots.find(index);
  if (it == slots.end()) {
    return false;
  }

  slot = it->second;
  return true;
}

int StorageManager::getEmptySlotCount() {
  int count = 0;
  for (const auto &pair : slots) {
    if (pair.second.status == EMPTY) {
      count++;
    }
  }
  return count;
}

int StorageManager::getOccupiedSlotCount() {
  int count = 0;
  for (const auto &pair : slots) {
    if (pair.second.status == OCCUPIED) {
      count++;
    }
  }
  return count;
}

int StorageManager::getTotalSlotCount() const { return slots.size(); }

std::vector<int> StorageManager::findItemByName(const std::string &itemName) {
  std::vector<int> results;
  
  for (const auto &pair : slots) {
    if (pair.second.item.find(itemName) != std::string::npos) {
      results.push_back(pair.first);
    }
  }

  return results;
}

bool StorageManager::turnOnLED(int index) {
  auto it = slots.find(index);
  if (it == slots.end()) {
    return false;
  }

  gpio_num_t pin = it->second.ledPin;
  
  if (pin == GPIO_NUM_NC) {
    return false;
  }

  gpio_set_level(pin, 1);
  ESP_LOGI(TAG, "LED ON for slot %d (GPIO %d)", index, pin);
  return true;
}

bool StorageManager::turnOffLED(int index) {
  auto it = slots.find(index);
  if (it == slots.end()) {
    return false;
  }

  gpio_num_t pin = it->second.ledPin;
  
  if (pin == GPIO_NUM_NC) {
    return false;
  }

  gpio_set_level(pin, 0);
  ESP_LOGI(TAG, "LED OFF for slot %d (GPIO %d)", index, pin);
  return true;
}

bool StorageManager::blinkLED(int index, int times, int delayMs) {
  auto it = slots.find(index);
  if (it == slots.end()) {
    return false;
  }

  gpio_num_t pin = it->second.ledPin;
  
  if (pin == GPIO_NUM_NC) {
    return false;
  }

  ESP_LOGI(TAG, "Blinking LED for slot %d", index);

  for (int i = 0; i < times; i++) {
    gpio_set_level(pin, 1);
    vTaskDelay(pdMS_TO_TICKS(delayMs));
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(delayMs));
  }

  return true;
}

void StorageManager::turnOffAllLEDs() {
  for (const auto &pair : slots) {
    if (pair.second.ledPin != GPIO_NUM_NC) {
      gpio_set_level(pair.second.ledPin, 0);
    }
  }
  ESP_LOGI(TAG, "All LEDs turned OFF");
}

void StorageManager::turnOnAllLEDs() {
  for (const auto &pair : slots) {
    if (pair.second.ledPin != GPIO_NUM_NC) {
      gpio_set_level(pair.second.ledPin, 1);
    }
  }
  ESP_LOGI(TAG, "All LEDs turned ON");
}

void StorageManager::resetAll(bool autoSave) {
  for (auto &pair : slots) {
    pair.second.status = EMPTY;
    pair.second.item = "";
    pair.second.lastUpdated = getCurrentUnixTime();
  }

  turnOffAllLEDs();

  if (autoSave)
    saveToFile();

  ESP_LOGI(TAG, "All slots reset to empty");
}

cJSON *StorageManager::slotToJSON(const Slot &slot) {
  cJSON *json = cJSON_CreateObject();

  // Convert internal index (0-3) to user-facing index (1-4)
  cJSON_AddNumberToObject(json, "index", slot.index + 1);
  cJSON_AddNumberToObject(json, "led_pin", slot.ledPin);
  cJSON_AddStringToObject(json, "status", getStatusName(slot.status));
  cJSON_AddStringToObject(json, "item", slot.item.c_str());
  cJSON_AddNumberToObject(json, "last_updated", slot.lastUpdated);

  return json;
}

std::string StorageManager::getSlotsJSON() {
  cJSON *root = cJSON_CreateObject();

  cJSON_AddNumberToObject(root, "total", slots.size());
  cJSON_AddNumberToObject(root, "empty", getEmptySlotCount());
  cJSON_AddNumberToObject(root, "occupied", getOccupiedSlotCount());

  cJSON *slotsArray = cJSON_CreateArray();

  for (const auto &pair : slots) {
    cJSON *slotJson = slotToJSON(pair.second);
    cJSON_AddItemToArray(slotsArray, slotJson);
  }

  cJSON_AddItemToObject(root, "slots", slotsArray);

  char *jsonStr = cJSON_Print(root);
  std::string result(jsonStr);

  cJSON_free(jsonStr);
  cJSON_Delete(root);

  return result;
}

bool StorageManager::saveToFile() {
  if (!initialized || filePath.empty()) {
    ESP_LOGE(TAG, "Not initialized or no file path");
    return false;
  }

  // Tạo JSON chỉ chứa dữ liệu cần lưu (không lưu GPIO config)
  cJSON *root = cJSON_CreateArray();

  for (const auto &pair : slots) {
    const Slot &slot = pair.second;
    
    // Chỉ lưu nếu slot có dữ liệu
    if (slot.status == OCCUPIED && !slot.item.empty()) {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddNumberToObject(item, "index", slot.index);
      cJSON_AddStringToObject(item, "item", slot.item.c_str());
      cJSON_AddNumberToObject(item, "last_updated", slot.lastUpdated);
      cJSON_AddItemToArray(root, item);
    }
  }

  char *jsonStr = cJSON_Print(root);
  cJSON_Delete(root);

  if (!jsonStr) {
    ESP_LOGE(TAG, "Failed to create JSON string");
    return false;
  }

  // Ghi vào file
  FILE *f = fopen(filePath.c_str(), "w");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open file for writing: %s", filePath.c_str());
    cJSON_free(jsonStr);
    return false;
  }

  size_t written = fwrite(jsonStr, 1, strlen(jsonStr), f);
  fclose(f);
  cJSON_free(jsonStr);

  if (written == 0) {
    ESP_LOGE(TAG, "Failed to write to file");
    return false;
  }

  ESP_LOGI(TAG, "Saved %d occupied slots to file: %s", getOccupiedSlotCount(), filePath.c_str());
  return true;
}

bool StorageManager::loadFromFile() {
  if (!initialized || filePath.empty()) {
    ESP_LOGE(TAG, "Not initialized or no file path");
    return false;
  }

  // Kiểm tra file có tồn tại không
  struct stat st;
  if (stat(filePath.c_str(), &st) != 0) {
    ESP_LOGW(TAG, "File does not exist: %s", filePath.c_str());
    return false;
  }

  // Đọc file
  FILE *f = fopen(filePath.c_str(), "r");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open file for reading: %s", filePath.c_str());
    return false;
  }

  // Lấy kích thước file
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (fsize <= 0) {
    fclose(f);
    ESP_LOGW(TAG, "File is empty");
    return false;
  }

  // Đọc nội dung
  char *jsonStr = (char *)malloc(fsize + 1);
  size_t read = fread(jsonStr, 1, fsize, f);
  fclose(f);
  jsonStr[read] = '\0';

  if (read == 0) {
    free(jsonStr);
    ESP_LOGE(TAG, "Failed to read file");
    return false;
  }

  // Parse JSON
  cJSON *root = cJSON_Parse(jsonStr);
  free(jsonStr);

  if (!root) {
    ESP_LOGE(TAG, "Failed to parse JSON");
    return false;
  }

  // Load dữ liệu vào các slot đã được khởi tạo
  cJSON *item = nullptr;
  cJSON_ArrayForEach(item, root) {
    cJSON *indexItem = cJSON_GetObjectItem(item, "index");
    cJSON *itemNameItem = cJSON_GetObjectItem(item, "item");
    cJSON *lastUpdatedItem = cJSON_GetObjectItem(item, "last_updated");

    if (indexItem && itemNameItem) {
      int index = indexItem->valueint;
      auto it = slots.find(index);
      
      if (it != slots.end()) {
        it->second.status = OCCUPIED;
        it->second.item = itemNameItem->valuestring;
        if (lastUpdatedItem) {
          it->second.lastUpdated = lastUpdatedItem->valuedouble;
        }
        ESP_LOGI(TAG, "Loaded slot %d: '%s'", index, it->second.item.c_str());
      }
    }
  }

  cJSON_Delete(root);

  ESP_LOGI(TAG, "Loaded data from file: %s", filePath.c_str());
  return true;
}

bool StorageManager::clearFile() {
  if (!initialized || filePath.empty()) {
    return false;
  }

  if (remove(filePath.c_str()) == 0) {
    ESP_LOGI(TAG, "Deleted file: %s", filePath.c_str());
    return true;
  }
  
  ESP_LOGW(TAG, "Failed to delete file: %s", filePath.c_str());
  return false;
}