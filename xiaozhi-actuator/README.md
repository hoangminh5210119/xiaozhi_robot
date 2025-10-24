# Xiaozhi Actuator ESP32 - MKS TinyBee V1

ESP32 phụ điều khiển cơ cấu chấp hành cho robot Xiaozhi.

## Hardware

- **Board**: MKS TinyBee V1 (ESP32 WROOM-32)
- **4 động cơ bước** (Stepper motors) cho bánh Mecanum
- **6 servo** cho cửa tủ đồ
- **6 LED** chỉ báo trạng thái tủ

## Chức năng

### 1. Điều khiển xe (Mecanum Wheels - Stepper Motors)
- **4 động cơ bước** sử dụng driver A4988/DRV8825 trên MKS TinyBee
- Thư viện: **AccelStepper** cho điều khiển mượt mà
- Hỗ trợ 7 hướng di chuyển:
  - `forward` - Tiến
  - `backward` - Lùi
  - `left` - Sang trái (strafe)
  - `right` - Sang phải (strafe)
  - `rotate_left` - Xoay trái tại chỗ
  - `rotate_right` - Xoay phải tại chỗ
  - `stop` - Dừng

### 2. Điều khiển tủ đồ (Storage Compartments)
- **6 ngăn tủ**, mỗi ngăn có:
  - 1 servo điều khiển cửa
  - 1 LED chỉ báo
- Hành động:
  - `open` - Mở cửa (0°) + bật LED
  - `close` - Đóng cửa (90°) + tắt LED
  - `led_on` / `led_off` - Điều khiển LED riêng
  - `led_blink` - Nhấp nháy LED

### 3. I2C Slave Communication
- Địa chỉ: `0x42`
- Nhận lệnh JSON từ ESP32 chính (Xiaozhi)
- Trả về JSON response

## Sơ đồ kết nối - MKS TinyBee V1

### Stepper Motors (4 trục)

| Motor | Trục MKS | STEP Pin | DIR Pin | Vị trí xe |
|-------|----------|----------|---------|-----------|
| FL    | X        | GPIO27   | GPIO26  | Front-Left |
| FR    | Y        | GPIO33   | GPIO32  | Front-Right |
| BL    | Z        | GPIO14   | GPIO25  | Back-Left |
| BR    | E0       | GPIO16   | GPIO17  | Back-Right |

**Enable (EN)**: GPIO12 (chung cho tất cả motor, Active LOW)

### Servo & LED (6 ngăn tủ)

| Slot | Servo Pin | LED Pin | Ghi chú |
|------|-----------|---------|---------|
| 0    | GPIO13    | GPIO19  | X_MIN endstop |
| 1    | GPIO15    | GPIO23  | Y_MIN endstop |
| 2    | GPIO2     | GPIO0   | Boot pin |
| 3    | GPIO4     | GPIO2   | Z_MIN endstop (shared) |
| 4    | GPIO5     | GPIO4   | Shared với servo |
| 5    | GPIO18    | GPIO5   | Shared với servo |

⚠️ **Lưu ý**: 
- Slot 3-5: LED dùng chung pin với servo, cần I2C LED driver (PCA9685) hoặc transistor riêng
- GPIO0, 2: Boot pins - cẩn thận khi boot

### I2C
```
SDA: GPIO21 (default)
SCL: GPIO22 (default)
Address: 0x42
```

### Các GPIO không dùng được
- **GPIO34, 35, 36, 39**: INPUT ONLY (dùng cho thermistor ADC trên board)
- **GPIO1, 3**: UART TX/RX (dùng cho Serial)
- **GPIO6-11**: Flash SPI (không động được)

## Protocol I2C

### Request Format (từ Xiaozhi)

#### 1. Vehicle Move Command
```json
{
  "type": "vehicle.move",
  "direction": "forward",
  "speed": 50,
  "duration_ms": 1000
}
```

**Parameters**:
- `direction`: forward/backward/left/right/rotate_left/rotate_right/stop
- `speed`: 0-100 (mapped to 0-2000 steps/sec)
- `duration_ms`: Thời gian di chuyển (0 = liên tục)

#### 2. Storage Control Command
```json
{
  "type": "storage.control",
  "slot": 0,
  "action": "open"
}
```

**Parameters**:
- `slot`: 0-5 (internal index)
- `action`: open/close/led_on/led_off/led_blink

#### 3. Status Query
```json
{
  "type": "status.get"
}
```

### Response Format (từ Actuator)

#### Success Response
```json
{
  "status": "ok",
  "message": "Vehicle moving"
}
```

#### Error Response
```json
{
  "status": "error",
  "message": "Invalid JSON"
}
```

#### Status Response
```json
{
  "battery": 12.5,
  "motors_enabled": true,
  "is_moving": false
}
```

## Mecanum Wheel Math

```
Forward:     FL=+, FR=+, BL=+, BR=+
Backward:    FL=-, FR=-, BL=-, BR=-
Left:        FL=-, FR=+, BL=+, BR=-
Right:       FL=+, FR=-, BL=-, BR=+
Rotate Left: FL=-, FR=+, BL=-, BR=+
Rotate Right:FL=+, FR=-, BL=+, BR=-
```

## Build & Upload

### Với PlatformIO
```bash
cd xiaozhi-actuator
pio run --target upload
pio device monitor
```

### Với Arduino IDE
1. Mở `src/main.cpp`
2. Chọn board: **ESP32 Dev Module**
3. Chọn port: `/dev/cu.usbserial-*` (macOS) hoặc `COM*` (Windows)
4. Upload

## Dependencies

- **ArduinoJson** v7.2.1 - JSON parsing
- **AccelStepper** v1.64 - Stepper motor control

## Configuration

### AccelStepper Parameters
```cpp
#define MAX_SPEED         2000.0  // steps/sec (max)
#define ACCELERATION      1000.0  // steps/sec^2
#define DEFAULT_SPEED     1000.0  // steps/sec
```

### Servo Parameters
- Frequency: 50Hz
- Pulse width: 1ms-2ms (0-180°)
- Closed: 90°
- Open: 0°

## Troubleshooting

### I2C không hoạt động
```bash
# Trên ESP32 chính (Xiaozhi), chạy i2c scan:
i2cdetect -y 0
# Phải thấy device ở address 0x42
```

- Kiểm tra pull-up resistor 4.7kΩ trên SDA/SCL
- Kiểm tra GND chung giữa 2 ESP32
- Kiểm tra nguồn 3.3V/5V ổn định

### Motor không quay
- Kiểm tra EN pin (GPIO12): LOW = enabled, HIGH = disabled
- Kiểm tra nguồn motor driver (12V/24V tùy driver)
- Kiểm tra microstepping jumper trên MKS TinyBee
- Đảm bảo GND chung giữa ESP32 và motor driver
- Kiểm tra dây nối STEP/DIR không bị đảo

### Servo không hoạt động
- Kiểm tra nguồn servo (5V, tối thiểu 2A cho 6 servo)
- Kiểm tra PWM frequency (50Hz)
- Kiểm tra pulse width (1-2ms)
- GND chung với board

### LED không sáng
- Slot 0-2: Có pin riêng, kiểm tra GPIO
- Slot 3-5: Dùng chung pin với servo, cần mạch điều khiển riêng
- Thêm I2C LED driver (PCA9685) hoặc shift register

## Pinout Reference - MKS TinyBee V1

```
Motor Drivers:
├── X:  STEP=27, DIR=26, EN=12
├── Y:  STEP=33, DIR=32, EN=12
├── Z:  STEP=14, DIR=25, EN=12
└── E0: STEP=16, DIR=17, EN=12

Endstops (repurposed for servos):
├── X_MIN: GPIO13
├── Y_MIN: GPIO15
├── Z_MIN: GPIO4
└── E0_MIN: GPIO35 (input only)

Thermistors (input only - không dùng):
├── T0:  GPIO34
├── T1:  GPIO35
└── BED: GPIO36

Power:
├── 12/24V IN: Screw terminal
├── 5V:  Onboard regulator
└── 3.3V: ESP32 regulator

I2C:
├── SDA: GPIO21
└── SCL: GPIO22
```

## License

MIT License
