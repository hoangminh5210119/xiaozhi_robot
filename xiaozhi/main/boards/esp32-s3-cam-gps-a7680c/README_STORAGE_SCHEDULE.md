# Hướng dẫn sử dụng Storage Manager & Recurring Schedule

## Tổng quan

Board ESP32-S3-CAM-GPS-A7680C đã được tích hợp 2 tính năng quan trọng cho người già:

1. **Storage Manager** - Quản lý tủ chứa đồ thông minh
2. **Recurring Schedule** - Lịch nhắc nhở định kỳ

---

## 1. Storage Manager - Tủ chứa đồ thông minh

### Mục đích
Giúp người già:
- Nhớ vị trí đồ vật đã cất
- Tìm đồ nhanh chóng bằng giọng nói
- LED báo hiệu vị trí ô chứa đồ

### Cấu hình phần cứng

Mặc định có 6 ô (0-5) với LED tương ứng:
```cpp
Ô 0 → LED GPIO 4
Ô 1 → LED GPIO 5
Ô 2 → LED GPIO 6
Ô 3 → LED GPIO 7
Ô 4 → LED GPIO 15
Ô 5 → LED GPIO 16
```

**⚠️ LƯU Ý**: Điều chỉnh GPIO pins trong `InitializeTools()` theo phần cứng thực tế của bạn!

### Các MCP Tools có sẵn

#### 1. `storage.put_item` - Lưu đồ vào ô
**Khi nào dùng**: User nói "tôi để kính vào ô số 4"

**Tham số**:
- `slot` (integer 0-5): Số ô
- `item` (string): Tên đồ vật

**Ví dụ**:
```json
{
  "slot": 4,
  "item": "kính"
}
```

**Kịch bản thực tế**:
```
User: "Tôi để kính vào ô số 4"
AI: (gọi storage.put_item với slot=4, item="kính")
AI: "Đã lưu kính vào ô số 4"
```

---

#### 2. `storage.find_item` - Tìm đồ vật
**Khi nào dùng**: User hỏi "kính của tôi ở đâu?"

**Tham số**:
- `item` (string): Tên đồ cần tìm

**Trả về**:
```json
{
  "item": "kính",
  "slots": [4],
  "count": 1
}
```

**Kịch bản thực tế**:
```
User: "Kính của tôi ở đâu?"
AI: (gọi storage.find_item với item="kính")
AI: "Kính của bạn ở ô số 4. Tôi sẽ bật đèn cho bạn"
AI: (gọi storage.led_blink với slot=4 để nhấp nháy đèn)
```

---

#### 3. `storage.led_blink` - Nhấp nháy đèn
**Khi nào dùng**: Sau khi tìm thấy đồ, để thu hút sự chú ý

**Tham số**:
- `slot` (integer 0-5): Số ô
- `times` (integer 1-10): Số lần nhấp nháy (mặc định 3)
- `delay_ms` (integer 100-2000): Độ trễ giữa các lần (mặc định 500ms)

**Ví dụ**:
```json
{
  "slot": 4,
  "times": 5,
  "delay_ms": 300
}
```

---

#### 4. `storage.led_on` / `storage.led_off`
Bật/tắt đèn LED của ô

**Tham số**:
- `slot` (integer 0-5)

---

#### 5. `storage.list_all` - Xem tất cả đồ
**Khi nào dùng**: User hỏi "tủ có gì?"

**Trả về**:
```json
{
  "total": 6,
  "empty": 4,
  "occupied": 2,
  "slots": [
    {
      "index": 0,
      "led_pin": 4,
      "status": "empty",
      "item": "",
      "last_updated": 0
    },
    {
      "index": 4,
      "led_pin": 15,
      "status": "occupied",
      "item": "kính",
      "last_updated": 1734567890
    }
  ]
}
```

---

#### 6. `storage.take_item` - Lấy đồ ra
**Khi nào dùng**: User nói "tôi lấy đồ ở ô 4 ra"

**Tham số**:
- `slot` (integer 0-5)

---

### Flow hoàn chỉnh

```
┌─────────────────────────────────────┐
│ User: "Tôi để kính vào ô số 4"     │
└──────────────┬──────────────────────┘
               ▼
┌─────────────────────────────────────┐
│ AI gọi: storage.put_item            │
│ - slot: 4                           │
│ - item: "kính"                      │
└──────────────┬──────────────────────┘
               ▼
┌─────────────────────────────────────┐
│ Lưu vào NVS Flash (tự động)         │
└─────────────────────────────────────┘

... Sau 1 ngày ...

┌─────────────────────────────────────┐
│ User: "Kính của tôi ở đâu?"         │
└──────────────┬──────────────────────┘
               ▼
┌─────────────────────────────────────┐
│ AI gọi: storage.find_item           │
│ - item: "kính"                      │
└──────────────┬──────────────────────┘
               ▼
┌─────────────────────────────────────┐
│ Trả về: {slots: [4]}                │
└──────────────┬──────────────────────┘
               ▼
┌─────────────────────────────────────┐
│ AI gọi: storage.led_blink           │
│ - slot: 4                           │
│ - times: 3                          │
└──────────────┬──────────────────────┘
               ▼
┌─────────────────────────────────────┐
│ LED ô số 4 nhấp nháy 3 lần          │
│ AI: "Kính ở ô số 4, đèn đang nhấp   │
│      nháy để bạn dễ tìm"            │
└─────────────────────────────────────┘
```

---

## 2. Recurring Schedule - Lịch nhắc nhở

### Các loại lịch

#### 1. `schedule.add_once` - Lịch 1 lần
Nhắc nhở 1 lần vào giờ cụ thể hôm nay

**Tham số**:
- `id` (integer): ID duy nhất
- `hour` (0-23): Giờ
- `minute` (0-59): Phút
- `note` (string): Nội dung nhắc

**Ví dụ**:
```
User: "Nhắc tôi uống thuốc lúc 8 giờ tối"
→ {id: 1, hour: 20, minute: 0, note: "nhắc nhở uống thuốc"}
```

---

#### 2. `schedule.add_interval` - Lịch lặp định kỳ
Lặp sau mỗi X giây/phút/giờ

**Tham số**:
- `id` (integer)
- `interval_seconds` (integer): Khoảng thời gian (giây)
- `note` (string)

**Ví dụ**:
```
User: "Kiểm tra nhiệt độ mỗi 30 phút"
→ {id: 2, interval_seconds: 1800, note: "kiểm tra nhiệt độ"}
```

---

#### 3. `schedule.add_daily` - Lịch hàng ngày
Lặp cùng giờ mỗi ngày

**Tham số**:
- `id` (integer)
- `hour` (0-23)
- `minute` (0-59)
- `note` (string)

**Ví dụ**:
```
User: "Báo thức 6 giờ 30 sáng mỗi ngày"
→ {id: 3, hour: 6, minute: 30, note: "báo thức"}
```

---

#### 4. `schedule.add_weekly` - Lịch hàng tuần
Lặp vào ngày và giờ cố định trong tuần

**Tham số**:
- `id` (integer)
- `weekday` (string): MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY, SUNDAY
- `hour` (0-23)
- `minute` (0-59)
- `note` (string)

**Ví dụ**:
```
User: "Nhắc họp mỗi thứ 2 lúc 9 giờ sáng"
→ {id: 4, weekday: "MONDAY", hour: 9, minute: 0, note: "họp team"}
```

---

### Quản lý lịch

#### `schedule.list` - Xem tất cả lịch
```json
{
  "count": 2,
  "schedules": [
    {
      "id": 1,
      "type": "daily",
      "enabled": true,
      "note": "báo thức",
      "times": [{"hour": 6, "minute": 30}],
      "next_trigger": 1734567890
    }
  ]
}
```

#### `schedule.remove` - Xóa lịch
**⚠️ Phải xác nhận với user trước!**

```
User: "Xóa lịch báo thức"
AI: "Tôi thấy lịch ID=3 là báo thức 6h30 sáng. Bạn có chắc muốn xóa không?"
User: "Có"
AI: (gọi schedule.remove với id=3)
```

#### `schedule.enable` - Bật/tắt tạm thời
Tạm dừng mà không xóa

```json
{
  "id": 3,
  "enable": false
}
```

---

## Kết hợp Storage + Schedule

### Use case thực tế

```
User: "Tôi để thuốc vào ô số 2. Nhắc tôi uống thuốc mỗi ngày lúc 8 giờ sáng"

AI thực hiện:
1. storage.put_item(slot=2, item="thuốc")
2. schedule.add_daily(id=10, hour=8, minute=0, note="nhắc nhở: thuốc ở ô số 2")

Khi đến 8h sáng:
→ Schedule callback gọi → AI nói: "Đã đến giờ uống thuốc. Thuốc của bạn ở ô số 2"
→ AI tự động gọi storage.led_blink(slot=2) để báo hiệu
```

---

## Lưu ý kỹ thuật

### 1. Cấu hình GPIO
Trong `InitializeTools()`, điều chỉnh GPIO pins:

```cpp
storage.addSlot(0, GPIO_NUM_4);  // Thay GPIO_NUM_4 thành pin thực tế
storage.addSlot(1, GPIO_NUM_5);
// ... tương tự cho các ô khác
```

### 2. NVS Flash
- Cả Storage và Schedule đều tự động lưu vào NVS
- Dữ liệu được khôi phục sau khi khởi động lại
- Namespace: `"storage_nvs"` và `"schedule_nvs"`

### 3. Thread-safe
- Storage Manager sử dụng mutex để đảm bảo thread-safe
- Schedule sử dụng FreeRTOS queue và task

### 4. Callback Schedule
Khi lịch được trigger:
```cpp
scheduler.setCallback([&app](int id, const std::string &note) {
  ESP_LOGI(TAG, "⏰ Schedule triggered: id=%d, note=%s", id, note.c_str());
  app.SendTextCommandToServer(note);  // Gửi lệnh về server để AI xử lý
});
```

---

## Testing

### Test Storage Manager

```bash
# 1. Lưu đồ
AI: storage.put_item(slot=0, item="chìa khóa")

# 2. Kiểm tra danh sách
AI: storage.list_all()

# 3. Tìm đồ
AI: storage.find_item(item="chìa khóa")

# 4. Bật LED
AI: storage.led_blink(slot=0, times=3, delay_ms=500)
```

### Test Schedule

```bash
# 1. Thêm lịch test (1 phút sau)
AI: schedule.add_once(id=999, hour=<current_hour>, minute=<current_minute+1>, note="test schedule")

# 2. Xem danh sách
AI: schedule.list()

# 3. Đợi 1 phút → xem log ESP32 có thông báo trigger không

# 4. Xóa lịch test
AI: schedule.remove(id=999)
```

---

## Troubleshooting

### Vấn đề thường gặp

1. **LED không sáng**
   - Kiểm tra GPIO pins có đúng không
   - Kiểm tra hardware LED có hoạt động không
   - Xem ESP32 log: `ESP_LOGI(TAG, "💡 Turning ON LED for slot %d", slot)`

2. **Lịch không trigger**
   - Kiểm tra thời gian hệ thống (NTP sync chưa?)
   - Xem schedule.list() có lịch không
   - Check ESP32 log: `ESP_LOGI(TAG, "⏰ Schedule triggered...")`

3. **Dữ liệu mất sau reboot**
   - Kiểm tra NVS partition đã được cấu hình trong `partitions.csv`
   - Xem log: `ESP_LOGI(TAG, "Loaded slots from NVS")`

---

## Tài liệu tham khảo

- RecurringSchedule.h/cc
- StorageManager.h/cc
- esp32s3cam.cc (InitializeTools)

---

**Phát triển bởi**: Dominh
**Board**: ESP32-S3-CAM-GPS-A7680C
**Ngày**: 2025
