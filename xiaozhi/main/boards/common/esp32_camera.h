#ifndef ESP32_CAMERA_H
#define ESP32_CAMERA_H

#include <esp_camera.h>
#include <lvgl.h>
#include <thread>
#include <memory>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

struct TelegramPhotoInfo {
    std::string bot_token;             // Bắt buộc: token bot, vd "123456:ABCDEF..."
    std::string chat_id;               // Bắt buộc: id chat (vd "123456789" hoặc "-1001234567890")
    std::string caption;               // Tuỳ chọn: chú thích
    std::string parse_mode;            // Tuỳ chọn: "MarkdownV2" | "HTML"
    bool        disable_notification = false; // Tuỳ chọn
    int         reply_to_message_id    = 0;   // Tuỳ chọn
    int         message_thread_id      = 0;   // Tuỳ chọn (topic của supergroup)
};



class Esp32Camera : public Camera {
private:
    camera_fb_t* fb_ = nullptr;
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;

public:
    Esp32Camera(const camera_config_t& config);
    ~Esp32Camera();

    virtual void SetExplainUrl(const std::string& url, const std::string& token);
    virtual bool Capture();
    // 翻转控制函数
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual std::string Explain(const std::string& question);

    virtual std::string SendPhotoToTelegram(const TelegramPhotoInfo& tg);

};

#endif // ESP32_CAMERA_H