#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "cJSON.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <map>
#include <string>
#include <vector>

class StorageManager {
public:
  static StorageManager &GetInstance() {
    static StorageManager instance;
    return instance;
  }
  // Trạng thái ô
  enum SlotStatus {
    EMPTY = 0,   // Ô trống
    OCCUPIED = 1 // Có đồ
  };

  // Cấu trúc thông tin 1 ô
  struct Slot {
    int index;           // Vị trí ô (0, 1, 2, 3, 4, 5)
    gpio_num_t ledPin;   // Chân GPIO điều khiển đèn
    SlotStatus status;   // Trạng thái ô
    std::string item;    // Tên đồ vật đang chứa
    int64_t lastUpdated; // Thời gian cập nhật cuối (Unix timestamp)

    Slot()
        : index(0), ledPin(GPIO_NUM_NC), status(EMPTY), item(""),
          lastUpdated(0) {}

    Slot(int idx, gpio_num_t pin)
        : index(idx), ledPin(pin), status(EMPTY), item(""), lastUpdated(0) {}
  };

  StorageManager();
  ~StorageManager();

  // Khởi tạo - auto load từ NVS
  bool begin(const char *storagePath = "/storage/storage.json");

  // Thêm/đăng ký ô mới
  bool addSlot(int index, gpio_num_t ledPin);

  // Thêm đồ vào ô (đánh dấu ô có đồ)
  bool putItem(int index, const std::string &item, bool autoSave = true);

  // Lấy đồ ra (đánh dấu ô trống)
  bool takeItem(int index, bool autoSave = true);

  // Cập nhật thông tin đồ vật trong ô
  bool updateItem(int index, const std::string &item, bool autoSave = true);

  // Lấy thông tin 1 ô
  bool getSlot(int index, Slot &slot);

  // Lấy tất cả slots dạng JSON
  std::string getSlotsJSON();

  // Đếm số ô trống
  int getEmptySlotCount();

  // Đếm số ô có đồ
  int getOccupiedSlotCount();

  // Lấy tổng số ô
  int getTotalSlotCount() const;

  // Tìm ô theo tên đồ vật
  std::vector<int> findItemByName(const std::string &itemName);

  // Bật đèn LED của ô (giúp định vị)
  bool turnOnLED(int index);

  // Tắt đèn LED của ô
  bool turnOffLED(int index);

  // Nhấp nháy đèn LED (để thu hút sự chú ý)
  bool blinkLED(int index, int times = 3, int delayMs = 500);

  // Tắt tất cả đèn
  void turnOffAllLEDs();

  // Bật tất cả đèn
  void turnOnAllLEDs();

  // Lưu tất cả vào file
  bool saveToFile();

  // Load tất cả từ file
  bool loadFromFile();

  // Xóa file
  bool clearFile();

  // Reset tất cả ô về trạng thái trống
  void resetAll(bool autoSave = true);

private:
  std::map<int, Slot> slots;
  std::string filePath;
  bool initialized;

  // Helper functions
  cJSON *slotToJSON(const Slot &slot);
  int64_t getCurrentUnixTime();
  const char *getStatusName(SlotStatus status);

  // GPIO helpers
  bool initGPIO(gpio_num_t pin);
};

#endif // STORAGE_MANAGER_H