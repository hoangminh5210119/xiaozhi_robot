# Custom ESP32-S3 Board

## Hardware Configuration

### Components Required
- ESP32-S3 module
- SSD1306 OLED Display (128x32)
- I2S Audio Codec
- ML307 Communication Module
- GPS Module
- Buttons and LED indicators

### Pin Configuration

#### Audio (I2S)
- MCLK: Not Connected
- WS (LRCK): GPIO21
- BCLK: GPIO39
- DIN: GPIO47
- DOUT: GPIO48

#### Display (I2C)
- SDA: GPIO41
- SCL: GPIO42

#### Buttons
- Capture Button: GPIO3
- Mode Button: GPIO46
- Send Button: GPIO14

#### Communication
- ML307 UART:
  - RX: GPIO11
  - TX: GPIO12
- GPS UART:
  - RX: GPIO1

#### Indicators
- SOS LED: GPIO2
- Vibration Motor: GPIO45

### Additional Parameters
- RX_DISTANCE: 40 (Reception threshold)

## Setup Instructions
1. Select this board in menuconfig under "Xiaozhi Assistant" -> "Board Selection" -> "Custom ESP32-S3 Board"
2. Configure any additional parameters as needed
3. Build and flash the firmware

## Notes
- The display is configured in mirror mode for both X and Y axes
- Audio sampling rate is set to 24KHz for both input and output
