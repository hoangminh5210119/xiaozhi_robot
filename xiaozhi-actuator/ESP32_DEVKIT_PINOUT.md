# Pin Configuration for ESP32 DevKit v1 (30-pin)
# Recommended replacement for MKS TinyBee V1

## Board: ESP32 DevKit v1
## CPU: ESP32-WROOM-32
## GPIO: 30 pins available

## ==================== I2C CONFIGURATION ====================
I2C_SLAVE_ADDR = 0x42
I2C_SDA_PIN = 21    # Default I2C SDA
I2C_SCL_PIN = 22    # Default I2C SCL

## ==================== STEPPER MOTOR PINS ====================
# Use DRV8825 or A4988 stepper drivers
# All motors share common ENABLE pin

MOTOR_EN = 33       # Common enable (Active LOW)

# Motor FL (Front Left) - Motor X
MOTOR_FL_STEP = 26
MOTOR_FL_DIR = 25

# Motor FR (Front Right) - Motor Y
MOTOR_FR_STEP = 14
MOTOR_FR_DIR = 27

# Motor BL (Back Left) - Motor Z
MOTOR_BL_STEP = 13
MOTOR_BL_DIR = 12

# Motor BR (Back Right) - Motor E0
MOTOR_BR_STEP = 15
MOTOR_BR_DIR = 2

## ==================== SERVO PINS ====================
# 6 servos for storage compartment doors
# PWM frequency: 50Hz

SERVO_SLOT_0 = 16
SERVO_SLOT_1 = 17
SERVO_SLOT_2 = 5
SERVO_SLOT_3 = 18
SERVO_SLOT_4 = 19
SERVO_SLOT_5 = 23

## ==================== LED PINS ====================
# 6 LEDs for storage compartment indicators

LED_SLOT_0 = 4
LED_SLOT_1 = 0      # Boot button LED (can use)
LED_SLOT_2 = 32     # ADC pin (can use as output)
LED_SLOT_3 = 35     # INPUT ONLY - need I2C expander
LED_SLOT_4 = 34     # INPUT ONLY - need I2C expander  
LED_SLOT_5 = 36     # INPUT ONLY - need I2C expander

# Status LEDs
LED_STATUS = 4      # Shared with LED_SLOT_0
LED_I2C_ACTIVITY = 0 # Shared with LED_SLOT_1

## ==================== ALTERNATIVE: I2C GPIO EXPANDER ====================
# If need more outputs, use PCA9685 or PCF8574

# PCA9685 (16-channel 12-bit PWM)
# - Connect to I2C (SDA=21, SCL=22)
# - Address: 0x40 (default)
# - Use for: 6 servos + 6 LEDs + extras

# PCF8574 (8-bit GPIO expander)
# - Connect to I2C (SDA=21, SCL=22)
# - Address: 0x20 (default)
# - Use for: 6 LEDs + 2 extras

## ==================== RESERVED PINS ====================
# Do not use these pins:

GPIO_1  = TX0       # Serial TX (USB)
GPIO_3  = RX0       # Serial RX (USB)
GPIO_6-11 = FLASH   # Connected to flash memory
GPIO_34-39 = INPUT  # Input only (ADC)

## ==================== AVAILABLE SPARE PINS ====================
# Extra pins for future expansion:

SPARE_1 = 32        # ADC1_CH4 (can be output)
SPARE_2 = 35        # ADC1_CH7 (INPUT ONLY)
SPARE_3 = 34        # ADC1_CH6 (INPUT ONLY)
SPARE_4 = 36        # ADC1_CH0 (INPUT ONLY)
SPARE_5 = 39        # ADC1_CH3 (INPUT ONLY)

## ==================== WIRING DIAGRAM ====================
```
ESP32 DevKit v1
    ┌─────────────┐
    │   ESP32     │
    │             │
    │ 21 ├────────┼── SDA (I2C)
    │ 22 ├────────┼── SCL (I2C)
    │             │
    │ 33 ├────────┼── MOTOR_EN (all motors)
    │             │
    │ 26 ├────────┼── FL_STEP (DRV8825 STEP)
    │ 25 ├────────┼── FL_DIR  (DRV8825 DIR)
    │             │
    │ 14 ├────────┼── FR_STEP
    │ 27 ├────────┼── FR_DIR
    │             │
    │ 13 ├────────┼── BL_STEP
    │ 12 ├────────┼── BL_DIR
    │             │
    │ 15 ├────────┼── BR_STEP
    │  2 ├────────┼── BR_DIR
    │             │
    │ 16 ├────────┼── SERVO_0 PWM
    │ 17 ├────────┼── SERVO_1 PWM
    │  5 ├────────┼── SERVO_2 PWM
    │ 18 ├────────┼── SERVO_3 PWM
    │ 19 ├────────┼── SERVO_4 PWM
    │ 23 ├────────┼── SERVO_5 PWM
    │             │
    │  4 ├────────┼── LED_0
    │  0 ├────────┼── LED_1
    │ 32 ├────────┼── LED_2
    │             │
    │ GND├────────┼── GND (common)
    │ 5V ├────────┼── 5V (logic)
    │             │
    └─────────────┘
```

## ==================== POWER SUPPLY ====================
# ESP32: 3.3V logic, 5V input via USB/VIN
# Stepper Drivers: 8-36V motor supply, 3.3V/5V logic
# Servos: 5V-6V power supply (separate from ESP32)
# LEDs: 3.3V with current limiting resistors

⚠️ WARNING: Do NOT power motors from ESP32 VIN/5V
Use separate power supply for motors (12V-24V recommended)

## ==================== COMPONENT LIST ====================
1. ESP32 DevKit v1 (30-pin) x1
2. DRV8825 Stepper Driver x4
3. SG90 Servo (or MG996R) x6
4. LED 5mm x6
5. Resistor 220Ω x6 (for LEDs)
6. Power Supply 12V 5A (for motors)
7. Power Supply 5V 3A (for servos + ESP32)
8. Breadboard jumper wires
9. Optional: PCA9685 (16-channel PWM) for more outputs

## ==================== CODE CHANGES ====================
Update main.ino with these pin definitions:

```cpp
// I2C
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// Motors
#define MOTOR_EN 33
#define MOTOR_FL_STEP 26
#define MOTOR_FL_DIR 25
// ... (see above)

// Servos
#define SERVO_SLOT_0 16
// ... (see above)

// LEDs  
#define LED_SLOT_0 4
// ... (see above)
```

## ==================== TESTING PROCEDURE ====================
1. Connect ESP32 to USB
2. Upload test code
3. Test I2C communication
4. Test each motor individually
5. Test each servo individually
6. Test LEDs
7. Test full movement sequences

## ==================== NOTES ====================
- GPIO 34-39 are INPUT ONLY (use I2C expander for LEDs if needed)
- GPIO 0,2,12,15 are strapping pins (be careful at boot)
- Use external power for motors and servos
- Add flyback diodes on motor drivers
- Use common ground between all power supplies
