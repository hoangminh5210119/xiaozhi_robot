#include "application.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "system_reset.h"
#include "wifi_board.h"

#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <wifi_station.h>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stepper_motor_encoder.h"

#define TAG "CompactWifiBoardLCD"

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
     (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                 0x04, 0x12, 0x14, 0x1f},
     14, 0},
    {0xf1,
     (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                 0x0C, 0x1A, 0x14, 0x1E},
     14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif

class CompactWifiBoardLCD : public WifiBoard {
private:
  Button boot_button_;
  Button function_button_;
  LcdDisplay *display_;

  //   MotorControl motor_control_; // Thêm đối tượng MotorControl

  void InitializeSpi() {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
    buscfg.miso_io_num = GPIO_NUM_NC;
    buscfg.sclk_io_num = DISPLAY_CLK_PIN;
    buscfg.quadwp_io_num = GPIO_NUM_NC;
    buscfg.quadhd_io_num = GPIO_NUM_NC;
    buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
  }

  void InitializeLcdDisplay() {
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    // 液晶屏控制IO初始化
    ESP_LOGD(TAG, "Install panel IO");
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = DISPLAY_CS_PIN;
    io_config.dc_gpio_num = DISPLAY_DC_PIN;
    io_config.spi_mode = DISPLAY_SPI_MODE;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

    // 初始化液晶屏驱动芯片
    ESP_LOGD(TAG, "Install LCD driver");
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = DISPLAY_RST_PIN;
    panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
    panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
    gc9a01_vendor_config_t gc9107_vendor_config = {
        .init_cmds = gc9107_lcd_init_cmds,
        .init_cmds_size =
            sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
    };
#else
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif

    esp_lcd_panel_reset(panel);

    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
    esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
    esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef LCD_TYPE_GC9A01_SERIAL
    panel_config.vendor_config = &gc9107_vendor_config;
#endif
    display_ = new SpiLcdDisplay(
        panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
        DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
  }

  void InitializeButtons() {
    boot_button_.OnClick([this]() {
      ESP_LOGD(TAG, "Boot button clicked");
      auto &app = Application::GetInstance();
      if (app.GetDeviceState() == kDeviceStateStarting &&
          !WifiStation::GetInstance().IsConnected()) {
        ResetWifiConfiguration();
      }
      app.ToggleChatState();
    });

    function_button_.OnClick([this]() {
      ESP_LOGD(TAG, "mode_button_ button clicked");

      auto codec = GetAudioCodec();
      auto volume = codec->output_volume() + 10;
      if (volume > 100) {
        volume = 100;
      }
      codec->SetOutputVolume(volume);
      GetDisplay()->ShowNotification(Lang::Strings::VOLUME +
                                     std::to_string(volume));
    });
  }

  // 物联网初始化，添加对 AI 可见设备
  void InitializeTools() {
    static LampController lamp(LAMP_GPIO);

    auto &mcp = McpServer::GetInstance();

    // motor_control_.Initialize(); // Khởi tạo MotorControl

    // mcp.AddTool("vehicle.move_forward",
    //             "ĐIỀU KHIỂN XE bằng lệnh tự nhiên.\n lệnh này di chuyển xe về
    //             " "phía trước một khoảng cách nhất định.\n"

    //             PropertyList({Property("command", kPropertyTypeString)}),
    //             [motor_control_](const PropertyList &props) -> ReturnValue {
    //               motor_control_.MoveForward(
    //                   100, 200,
    //                   0); // Di chuyển về phía trước 200mm
    //               return "Thực hiện lệnh thành công";
    //             });

    mcp.AddTool("vehicle.move_forward",
                "ĐIỀU KHIỂN XE bằng lệnh tự nhiên.\n lệnh này di chuyển xe về "
                "phía trước một khoảng cách nhất định..",
                PropertyList(), [this](const PropertyList &) -> ReturnValue {
                  //   motor_control_.MoveForward(100, 3000, 1000);
                  return "Thực hiện lệnh thành công";
                });
  }

  void setup_motor() {
    ESP_LOGI(TAG, "===== Motor Control Setup =====");

    // Tạo task để khởi tạo và chạy motor
    // Tất cả khởi tạo motor sẽ được thực hiện trong task để tránh block
    // constructor
    ESP_LOGI(TAG, "Creating motor demo task...");
    xTaskCreatePinnedToCore(motor_demo_task, "MotorDemoTask",
                            8192, // Tăng stack size
                            NULL,
                            1, // Priority thấp hơn
                            NULL,
                            1 // Run on core 1
    );

    ESP_LOGI(TAG, "Motor setup completed - initialization will happen in task");
  }

public:
  CompactWifiBoardLCD()
      : boot_button_(BOOT_BUTTON_GPIO, false, 2000, 50, false),
        mode_button_(FUNCTION_BUTTON_GPIO, false, 2000, 50, false) {
    InitializeSpi();
    InitializeLcdDisplay();
    InitializeButtons();
    InitializeTools();
    setup_motor();
    if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
      GetBacklight()->RestoreBrightness();
    }
  }

  virtual Led *GetLed() override {
    static SingleLed led(BUILTIN_LED_GPIO);
    return &led;
  }

  virtual AudioCodec *GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
    static NoAudioCodecSimplex audio_codec(
        AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
        AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
        AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS,
        AUDIO_I2S_MIC_GPIO_DIN);
#else
    static NoAudioCodecDuplex audio_codec(
        AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_BCLK,
        AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
    return &audio_codec;
  }

  virtual Display *GetDisplay() override { return display_; }

  virtual Backlight *GetBacklight() override {
    if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
      static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN,
                                    DISPLAY_BACKLIGHT_OUTPUT_INVERT);
      return &backlight;
    }
    return nullptr;
  }
};

DECLARE_BOARD(CompactWifiBoardLCD);
