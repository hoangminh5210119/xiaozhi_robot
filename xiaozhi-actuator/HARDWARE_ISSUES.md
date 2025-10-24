# ⚠️ VẤN ĐỀ PHẦN CỨNG: MKS TinyBee V1

## Tóm tắt vấn đề

**MKS TinyBee V1 KHÔNG TƯƠNG THÍCH** với dự án robot này vì lý do kiến trúc phần cứng.

## Chi tiết vấn đề

### 1. Stepper Motor Control qua I2S Shift Register

**Vấn đề:**
- Board MKS TinyBee sử dụng **I2S shift register** (74HC595 hoặc tương tự) để điều khiển stepper motors
- Tất cả motor pins (EN, STEP, DIR) được map qua I2S outputs (i2so.0 - i2so.23)
- GPIO trực tiếp: I2S_BCK=25, I2S_WS=26, I2S_DATA=27
- AccelStepper library **KHÔNG hỗ trợ** I2S architecture

**Tại sao dùng I2S?**
- Tiết kiệm GPIO (chỉ cần 3 pins cho 24 outputs)
- Giảm tải CPU (DMA-based)
- Phù hợp cho 3D printer (nhiều motors + heaters + fans)

**Giải pháp hiện tại:**
- ❌ AccelStepper KHÔNG hoạt động được
- ✅ Cần dùng GRBL_ESP32 hoặc FluidNC firmware
- ✅ Hoặc viết I2S driver riêng (phức tạp)

### 2. Thiếu GPIO cho Servos và LEDs

**GPIO đã sử dụng:**
```
GPIO 0,1,2,3    : UART0 + Boot pins
GPIO 4,5,12-19,21,23 : LCD Display + SPI SD Card
GPIO 25,26,27   : I2S (stepper control)
GPIO 22,32,33   : Endstops (có thể repurpose)
GPIO 34,35,36,39: INPUT ONLY (ADC sensors)
```

**Cần thêm:**
- 6x Servo PWM outputs
- 6x LED outputs
- 2x I2C pins

**Kết luận:**
- Không đủ GPIO trống
- Phải bỏ LCD hoặc SD Card hoặc dùng GPIO expander

### 3. Kiến trúc không phù hợp

MKS TinyBee được thiết kế cho **3D printer**, không phải **mobile robot**:
- Nhiều features không cần: LCD, SD Card, Heated Bed
- Thiếu features cần: I2C, nhiều GPIO outputs
- I2S architecture phức tạp, không cần thiết cho robot

---

## ĐỀ XUẤT GIẢI PHÁP

### Giải pháp 1: Hardware Mod (không khuyến khích)

**Bước 1:** Bypass I2S Shift Register
- Cắt kết nối giữa ESP32 và shift register
- Nối trực tiếp stepper driver pins vào GPIO:
  - Motor X: STEP=GPIO26, DIR=GPIO25
  - Motor Y: STEP=GPIO14, DIR=GPIO12
  - Motor Z: STEP=GPIO15, DIR=GPIO13
  - Motor E0: STEP=GPIO17, DIR=GPIO16

**Bước 2:** Bỏ LCD Display
- Giải phóng GPIO 4,5,12-17,21

**Bước 3:** Thêm I2C GPIO Expander
- PCF8574 hoặc PCA9685 cho servos + LEDs
- Kết nối I2C: SDA=GPIO32, SCL=GPIO33

**Nhược điểm:**
- Mất bảo hành
- Phức tạp
- Không ổn định

### Giải pháp 2: Đổi sang board khác (KHUYẾN KHÍCH)

**Lựa chọn tốt hơn:**

#### Option A: ESP32 DevKit v1 (30-pin)
✅ **Ưu điểm:**
- 30 GPIO khả dụng
- Giá rẻ (~50k VND)
- Dễ kết nối
- Phù hợp cho robot

**Kết nối đề xuất:**
```
Stepper Motors (DRV8825/A4988):
  Motor FL: STEP=GPIO26, DIR=GPIO25, EN=GPIO33
  Motor FR: STEP=GPIO14, DIR=GPIO27, EN=GPIO33
  Motor BL: STEP=GPIO13, DIR=GPIO12, EN=GPIO33
  Motor BR: STEP=GPIO15, DIR=GPIO2,  EN=GPIO33

Servos (6 servos):
  Slot 0-5: GPIO16, 17, 5, 18, 19, 23 (PWM)

LEDs (6 LEDs):
  Slot 0-5: GPIO4, 0, 22, 32, 35, 34 (or I2C expander)

I2C:
  SDA: GPIO21
  SCL: GPIO22
```

#### Option B: ESP32-S3 DevKit
✅ **Ưu điểm:**
- 45 GPIO khả dụng
- USB OTG native
- Hiệu năng cao hơn
- Nhiều PWM channels

#### Option C: Giữ MKS TinyBee + Dùng FluidNC

✅ **Cách làm:**
1. Flash FluidNC firmware (hỗ trợ I2S)
2. Cấu hình motors qua config.yaml
3. Giao tiếp qua Serial/Telnet/WebSocket (G-code)
4. Dùng I2C expander cho servos

⚠️ **Nhược điểm:**
- Phức tạp
- Cần học G-code
- Overhead lớn

---

## KẾT LUẬN

### Khuyến nghị: ĐỔI BOARD

**Lý do:**
1. MKS TinyBee không phù hợp với thiết kế robot
2. I2S architecture quá phức tạp cho use case này
3. Thiếu GPIO
4. ESP32 DevKit rẻ hơn và đơn giản hơn

**Nếu PHẢI dùng MKS TinyBee:**
1. Sử dụng FluidNC firmware
2. Thêm I2C GPIO expander (PCA9685) cho servos/LEDs
3. Giao tiếp qua G-code thay vì JSON

**Chi phí:**
- ESP32 DevKit v1: ~50,000 VND
- PCA9685 (16-channel PWM): ~80,000 VND
- DRV8825 x4: ~120,000 VND
- **Tổng:** ~250,000 VND

---

## TÀI LIỆU THAM KHẢO

1. MKS TinyBee Datasheet: https://github.com/makerbase-mks/MKS-TinyBee
2. FluidNC: https://github.com/bdring/FluidNC
3. GRBL_ESP32: https://github.com/bdring/Grbl_Esp32
4. ESP32 I2S Parallel: https://github.com/espressif/esp-idf/tree/master/examples/peripherals/i2s

---

**Ngày cập nhật:** October 16, 2025  
**Trạng thái:** ⚠️ HARDWARE INCOMPATIBLE - CẦN ĐỔI BOARD
