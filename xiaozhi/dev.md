python scripts/release.py esp32-s3-cam-gps-a7680c

# IDF Path
```sh
pushd /Users/dominh/esp/esp-idf && . ./export.sh && popd
idf.py set-target esp32s3
idf.py build flash monitor


pushd /Users/dominh/esp/esp-adf && . ./export.sh && popd
```
```sh
cd /Users/dominh/Desktop/Esp32AiCameraServer/esp32s3main
```

1. Cập nhật `CMakeLists.txt` để thêm định nghĩa cho board mới:
```bash

set(SOURCES "audio/audio_codec.cc"
            .....
            "TelegramBot.cc"
            "boards/esp32-s3-cam-gps-a7680c/nmea_parser.c"
            )

idf_component_register(SRCS ${SOURCES}
                    EMBED_FILES ${LANG_SOUNDS} ${COMMON_SOUNDS}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    WHOLE_ARCHIVE
                    EMBED_TXTFILES telegram_certificate.pem
                    )
# Use target_compile_definitions to define BOARD_TYPE, BOARD_NAME
if(CONFIG_BOARD_TYPE_CUSTOM_S3_SSD1306_CAM_ML307)
    set(BOARD_TYPE "esp32-s3-cam-gps-a7680c")
    set(LVGL_TEXT_FONT ${FONT_PUHUI_BASIC_14_1})
    set(LVGL_ICON_FONT ${FONT_AWESOME_14_1})
    set(DEFAULT_ASSETS ${ASSETS_XIAOZHI_PUHUI_COMMON_14_1})
endif()
```

2. Cập nhật  `Kconfig.projbuild`

```bash
choice BOARD_TYPE

    ....

    config BOARD_TYPE_YUNLIAO_S3
        bool "小智云聊-S3"
        depends on IDF_TARGET_ESP32S3
    config BOARD_TYPE_CUSTOM_S3_SSD1306_CAM_ML307
        bool "Custom S3 + SSD1306 + Cam + A7680"
        depends on IDF_TARGET_ESP32S3
```

```bash

choice DISPLAY_OLED_TYPE
    depends on BOARD_TYPE_BREAD_COMPACT_WIFI || BOARD_TYPE_BREAD_COMPACT_ML307 || BOARD_TYPE_BREAD_COMPACT_ESP32 || BOARD_TYPE_CUSTOM_S3_SSD1306_CAM_ML307
    prompt "OLED Type"
    default OLED_SSD1306_128X32
```

`/main/boards/common/esp32_camera.h`

```cpp
struct TelegramPhotoInfo {
    std::string bot_token;             // Bắt buộc: token bot, vd "123456:ABCDEF..."
    std::string chat_id;               // Bắt buộc: id chat (vd "123456789" hoặc "-1001234567890")
    std::string caption;               // Tuỳ chọn: chú thích
    std::string parse_mode;            // Tuỳ chọn: "MarkdownV2" | "HTML"
    bool        disable_notification = false; // Tuỳ chọn
    int         reply_to_message_id    = 0;   // Tuỳ chọn
    int         message_thread_id      = 0;   // Tuỳ chọn (topic của supergroup)
};

virtual std::string SendPhotoToTelegram(const TelegramPhotoInfo& tg);




std::string Esp32Camera::SendPhotoToTelegram(const TelegramPhotoInfo& tg) {
    if (tg.bot_token.empty() || tg.chat_id.empty()) {
        throw std::runtime_error("Telegram bot_token or chat_id is empty");
    }
    if (!fb_) {
        // Tuỳ bạn: có thể auto chụp, ở đây mình báo lỗi rõ ràng
        throw std::runtime_error("No camera frame available. Call Capture() first.");
    }

    // 1) Tạo queue chứa các mảnh JPEG, thêm sentinel khi encode xong
    QueueHandle_t jpeg_queue = xQueueCreate(40, sizeof(JpegChunk));
    if (!jpeg_queue) {
        ESP_LOGE(TAG, "Failed to create JPEG queue");
        throw std::runtime_error("Failed to create JPEG queue");
    }

    encoder_thread_ = std::thread([this, jpeg_queue]() {
        // Encode JPEG theo từng chunk (quality 80)
        frame2jpg_cb(fb_, 80,
            [](void* arg, size_t index, const void* data, size_t len) -> unsigned int {
                auto q = static_cast<QueueHandle_t>(arg);
                JpegChunk c{
                    .data = (uint8_t*)heap_caps_aligned_alloc(16, len, MALLOC_CAP_SPIRAM),
                    .len  = len
                };
                if (!c.data) return 0;
                memcpy(c.data, data, len);
                xQueueSend(q, &c, portMAX_DELAY);
                return (unsigned int)len;
            },
            jpeg_queue
        );
        // Đẩy sentinel để consumer biết là hết dữ liệu
        JpegChunk end{ .data = nullptr, .len = 0 };
        xQueueSend(jpeg_queue, &end, portMAX_DELAY);
    });

    // 2) Chuẩn bị HTTP tới Telegram
    const std::string boundary = "----ESP32_TG_BOUNDARY";
    const std::string url = "https://api.telegram.org/bot" + tg.bot_token + "/sendPhoto";

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(10); // timeout 10s (tuỳ bạn)

    // GHI CHÚ: cần TLS CA cho api.telegram.org (tuỳ HTTP wrapper của bạn).
    // Nếu wrapper hỗ trợ, hãy đảm bảo đã set CA/verify. Tránh tắt verify trong sản phẩm.

    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked");   // stream từng phần

    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to open Telegram URL");
        if (encoder_thread_.joinable()) encoder_thread_.join();
        // Xả queue non-blocking để free chunk nếu có
        JpegChunk c;
        while (xQueueReceive(jpeg_queue, &c, 0) == pdPASS) {
            if (c.data) heap_caps_free(c.data);
        }
        vQueueDelete(jpeg_queue);
        throw std::runtime_error("Failed to connect to Telegram");
    }

    auto write_kv = [&](const char* name, const std::string& value) {
        if (value.empty()) return;
        std::string part;
        part += "--" + boundary + "\r\n";
        part += "Content-Disposition: form-data; name=\"";
        part += name;
        part += "\"\r\n\r\n";
        part += value;
        part += "\r\n";
        http->Write(part.c_str(), part.size());
    };
    auto write_kv_bool = [&](const char* name, bool b) {
        std::string v = b ? "true" : "false";
        std::string part;
        part += "--" + boundary + "\r\n";
        part += "Content-Disposition: form-data; name=\"";
        part += name;
        part += "\"\r\n\r\n";
        part += v + "\r\n";
        http->Write(part.c_str(), part.size());
    };
    auto write_kv_int = [&](const char* name, int v) {
        if (v == 0) return;
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", v);
        write_kv(name, buf);
    };

    // 3) Các field text bắt buộc/tuỳ chọn của Telegram
    write_kv("chat_id", tg.chat_id);
    if (!tg.caption.empty())     write_kv("caption", tg.caption);
    if (!tg.parse_mode.empty())  write_kv("parse_mode", tg.parse_mode);
    if (tg.disable_notification) write_kv_bool("disable_notification", tg.disable_notification);
    write_kv_int("reply_to_message_id", tg.reply_to_message_id);
    write_kv_int("message_thread_id",    tg.message_thread_id);

    // 4) Header của phần file "photo"
    {
        std::string hdr;
        hdr += "--" + boundary + "\r\n";
        hdr += "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n";
        hdr += "Content-Type: image/jpeg\r\n\r\n";
        http->Write(hdr.c_str(), hdr.size());
    }

    // 5) Stream dữ liệu JPEG từ queue
    size_t total_sent = 0;
    while (true) {
        JpegChunk c;
        if (xQueueReceive(jpeg_queue, &c, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Queue receive failed");
            break;
        }
        if (!c.data) break; // gặp sentinel
        http->Write((const char*)c.data, c.len);
        total_sent += c.len;
        heap_caps_free(c.data);
    }

    // 6) Kết multipart + kết thúc chunked
    if (encoder_thread_.joinable()) encoder_thread_.join();
    vQueueDelete(jpeg_queue);

    {
        const std::string tail = "\r\n--" + boundary + "--\r\n";
        http->Write(tail.c_str(), tail.size());
    }
    http->Write("", 0); // chốt chunked (tuỳ wrapper: gửi 0\r\n\r\n)

    // 7) Đọc phản hồi
    int code = http->GetStatusCode();
    std::string body = http->ReadAll();
    http->Close();

    if (code / 100 != 2) { // không phải 2xx
        ESP_LOGE(TAG, "Telegram sendPhoto failed: HTTP %d\n%s", code, body.c_str());
        throw std::runtime_error("Telegram sendPhoto failed");
    }

    ESP_LOGI(TAG, "Telegram sendPhoto OK, sent=%u bytes", (unsigned)total_sent);
    return body; // JSON từ Telegram (ok, result...)
}


```