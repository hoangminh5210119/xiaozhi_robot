#pragma once

#include "I2CCommandBridge.h"
#include "cJSON.h"
#include <map>
#include <string>
#include <vector>
#include <functional>

/**
 * @brief Storage Manager
 * 
 * Quản lý storage với:
 * - 4 hardware slots (ô vật lý trong máy qua I2C): 0-3
 * - Unlimited virtual locations (vị trí ảo): "trên bàn", "trong túi", "ở tủ lạnh", etc.
 * 
 * Hardware slots có thể mở/đóng vật lý qua I2C.
 * Virtual locations chỉ để ghi nhớ và trả lời.
 */
class StorageManager {
public:
    static StorageManager& GetInstance() {
        static StorageManager instance;
        return instance;
    }
    
    /**
     * @brief Storage item - đồ vật được lưu trữ
     */
    struct StorageItem {
        std::string name;           // Tên đồ vật: "kính", "gương", "chìa khóa"
        std::string location;       // Vị trí: "slot_0", "slot_1", "trên bàn", "trong túi"
        bool is_hardware_slot;      // true = ô vật lý, false = vị trí ảo
        int hardware_slot_id;       // Số ô vật lý (0-3), -1 nếu không phải hardware
        std::string description;    // Mô tả thêm
        uint64_t timestamp;         // Thời gian lưu (epoch ms)
        
        StorageItem() 
            : is_hardware_slot(false), hardware_slot_id(-1), timestamp(0) {}
    };
    
    /**
     * @brief Hardware slot info
     */
    struct HardwareSlot {
        int slot_id;                // 0-3
        bool is_open;               // Trạng thái mở/đóng
        std::string default_item;   // Đồ vật mặc định trong ô này
        bool has_item;              // Có đồ không
        
        HardwareSlot() : slot_id(-1), is_open(false), has_item(false) {}
    };
    
    typedef std::function<void(const std::string& message)> StatusCallback;

    ~StorageManager();

    // ==================== INITIALIZATION ====================
    
    /**
     * @brief Khởi tạo với I2C bridge
     */
    bool Init(I2CCommandBridge* i2c_bridge);
    
    /**
     * @brief Load storage data từ file
     */
    bool LoadFromFile(const std::string& filepath = "/storage/storage.json");
    
    /**
     * @brief Save storage data ra file
     */
    bool SaveToFile(const std::string& filepath = "/storage/storage.json");

    // ==================== HARDWARE CONTROL ====================
    
    /**
     * @brief Mở ô vật lý
     */
    bool OpenHardwareSlot(int slot_id);
    
    /**
     * @brief Đóng ô vật lý
     */
    bool CloseHardwareSlot(int slot_id);
    
    /**
     * @brief Lấy trạng thái ô vật lý
     */
    HardwareSlot GetHardwareSlotStatus(int slot_id);
    
    /**
     * @brief Cập nhật trạng thái từ actuator status
     */
    void UpdateHardwareStatus(const ActuatorStatus& status);

    // ==================== ITEM MANAGEMENT ====================
    
    /**
     * @brief Lưu đồ vật vào storage
     * @param item_name Tên đồ vật
     * @param location Vị trí: "slot_0", "slot_1", "trên bàn", "trong túi", etc.
     * @param description Mô tả thêm (optional)
     */
    bool StoreItem(const std::string& item_name, const std::string& location, 
                   const std::string& description = "");
    
    /**
     * @brief Xóa đồ vật khỏi storage
     */
    bool RemoveItem(const std::string& item_name);
    
    /**
     * @brief Tìm vị trí của đồ vật
     * @return Vị trí, hoặc empty string nếu không tìm thấy
     */
    std::string FindItemLocation(const std::string& item_name);
    
    /**
     * @brief Lấy thông tin chi tiết của đồ vật
     */
    StorageItem GetItemInfo(const std::string& item_name);
    
    /**
     * @brief Lấy danh sách tất cả đồ vật
     */
    std::vector<StorageItem> GetAllItems();
    
    /**
     * @brief Lấy danh sách đồ vật trong hardware slot cụ thể
     */
    std::vector<StorageItem> GetItemsInSlot(int slot_id);
    
    /**
     * @brief Lấy danh sách đồ vật ở virtual location
     */
    std::vector<StorageItem> GetItemsAtLocation(const std::string& location);
    
    /**
     * @brief Di chuyển đồ vật sang vị trí khác
     */
    bool MoveItem(const std::string& item_name, const std::string& new_location);
    
    /**
     * @brief Kiểm tra đồ vật có tồn tại không
     */
    bool HasItem(const std::string& item_name);

    // ==================== NATURAL LANGUAGE INTERFACE ====================
    
    /**
     * @brief Parse lệnh tự nhiên
     * Ví dụ: 
     * - "để kính vào ô 1" -> StoreItem("kính", "slot_0")
     * - "để chìa khóa trên bàn" -> StoreItem("chìa khóa", "trên bàn")
     * - "kính ở đâu?" -> FindItemLocation("kính")
     * - "mở ô 1" -> OpenHardwareSlot(0)
     */
    std::string ProcessNaturalCommand(const std::string& command);
    
    /**
     * @brief Trả lời câu hỏi về vị trí
     */
    std::string AnswerLocationQuery(const std::string& item_name);

    // ==================== CONFIGURATION ====================
    
    /**
     * @brief Đặt default items cho hardware slots
     */
    bool SetDefaultSlotItem(int slot_id, const std::string& item_name);
    
    /**
     * @brief Set callback để nhận status
     */
    void SetStatusCallback(StatusCallback callback);
    
    /**
     * @brief Lấy tổng số items
     */
    int GetTotalItemCount() const { return items_.size(); }
    
    /**
     * @brief Lấy số items trong hardware slots
     */
    int GetHardwareItemCount();
    
    /**
     * @brief Lấy số items ở virtual locations
     */
    int GetVirtualItemCount();
    
    // ==================== SMART STORAGE (PENDING STATE) ====================
    
    /**
     * @brief Đặt item đang chờ được bỏ vào ô
     * @param slot_id ID của ô (0-3)
     * @param item_name Tên item sẽ được bỏ vào
     */
    void SetPendingItem(int slot_id, const std::string& item_name);
    
    /**
     * @brief Lấy tên item đang chờ ở ô
     * @param slot_id ID của ô (0-3)
     * @return Tên item hoặc chuỗi rỗng nếu không có
     */
    std::string GetPendingItem(int slot_id) const;
    
    /**
     * @brief Xóa pending state của ô
     * @param slot_id ID của ô (0-3)
     */
    void ClearPendingItem(int slot_id);
    
    /**
     * @brief Lấy pointer tới hardware slot (for direct access)
     * @param slot_id ID của ô (0-3)
     * @return Pointer to HardwareSlot hoặc nullptr nếu invalid
     */
    const HardwareSlot* GetHardwareSlot(int slot_id) const;

private:
    StorageManager();
    
    I2CCommandBridge* i2c_bridge_;
    bool initialized_;
    std::string storage_file_path_;
    
    // Storage data
    std::map<std::string, StorageItem> items_;  // Key: item name (lowercase)
    HardwareSlot hardware_slots_[4];
    
    // Pending state: item đang chờ được bỏ vào ô
    std::map<int, std::string> pending_items_;  // Key: slot_id, Value: item_name
    
    StatusCallback status_callback_;
    
    // Helper functions
    void NotifyStatus(const std::string& message);
    std::string NormalizeItemName(const std::string& name);
    std::string NormalizeLocation(const std::string& location);
    bool IsHardwareSlotLocation(const std::string& location, int& slot_id);
    std::string SlotIdToLocation(int slot_id);
    uint64_t GetCurrentTimestamp();
    
    static const char* TAG;
};