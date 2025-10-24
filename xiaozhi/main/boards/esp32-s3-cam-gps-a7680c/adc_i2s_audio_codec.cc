#include "adc_i2s_audio_codec.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <driver/i2s_std.h>
#include "adc_mic.h"
#include "audio_codec_if.h"
#include "audio_codec_ctrl_if.h"
#include "audio_codec_gpio_if.h"
#include "audio_codec_vol_if.h"
#include "settings.h"

static const char TAG[] = "AdcI2sAudioCodec";

#define TIMER_TIMEOUT_US (3 * 1000 * 1000) // 3 seconds

AdcI2sAudioCodec::AdcI2sAudioCodec(int input_sample_rate, int output_sample_rate,
    uint32_t adc_mic_channel, gpio_num_t i2s_bclk, gpio_num_t i2s_ws, 
    gpio_num_t i2s_dout, gpio_num_t i2s_din) {

    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    input_enabled_ = false;
    output_enabled_ = false;
    output_volume_ = 100;
    pa_ctrl_pin_ = GPIO_NUM_NC;
    output_timer_ = nullptr;
    input_timer_ = nullptr;

    // ===== Setup ADC input device =====
    uint8_t adc_channel[1] = {(uint8_t)adc_mic_channel};
    
    audio_codec_adc_cfg_t adc_cfg = {
        .handle = NULL,
        .max_store_buf_size = 1024 * 2,
        .conv_frame_size = 1024,
        .unit_id = ADC_UNIT_1,
        .adc_channel_list = adc_channel,
        .adc_channel_num = 1,
        .atten = ADC_ATTEN_DB_12,  // 12dB attenuation (0-3.3V range) for maximum sensitivity
        .sample_rate_hz = (uint32_t)input_sample_rate,
    };
    const audio_codec_data_if_t *adc_data_if = audio_codec_new_adc_data(&adc_cfg);

    esp_codec_dev_cfg_t input_codec_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = NULL,
        .data_if = adc_data_if,
    };
    input_dev_ = esp_codec_dev_new(&input_codec_cfg);
    if (!input_dev_) {
        ESP_LOGE(TAG, "Failed to create ADC input codec device");
        return;
    }

    // ===== Setup I2S output device =====
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, nullptr));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = i2s_bclk,
            .ws = i2s_ws,
            .dout = i2s_dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = nullptr,
        .tx_handle = tx_handle_,
    };
    const audio_codec_data_if_t *i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);

    esp_codec_dev_cfg_t output_codec_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = NULL,
        .data_if = i2s_data_if,
    };
    output_dev_ = esp_codec_dev_new(&output_codec_cfg);
    if (!output_dev_) {
        ESP_LOGE(TAG, "Failed to create I2S output codec device");
        return;
    }

    // ===== Setup timers =====
    esp_timer_create_args_t output_timer_args = {
        .callback = &AdcI2sAudioCodec::OutputTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "output_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&output_timer_args, &output_timer_));

    esp_timer_create_args_t input_timer_args = {
        .callback = &AdcI2sAudioCodec::InputTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "input_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&input_timer_args, &input_timer_));

    ESP_LOGI(TAG, "AdcI2sAudioCodec initialized (ADC input + I2S output)");
}

AdcI2sAudioCodec::~AdcI2sAudioCodec() {
    if (output_timer_) {
        esp_timer_stop(output_timer_);
        esp_timer_delete(output_timer_);
        output_timer_ = nullptr;
    }
    if (input_timer_) {
        esp_timer_stop(input_timer_);
        esp_timer_delete(input_timer_);
        input_timer_ = nullptr;
    }

    if (output_dev_) {
        esp_codec_dev_close(output_dev_);
        esp_codec_dev_delete(output_dev_);
    }
    if (input_dev_) {
        esp_codec_dev_close(input_dev_);
        esp_codec_dev_delete(input_dev_);
    }
}

int AdcI2sAudioCodec::Read(int16_t* dest, int samples) {
    if (input_enabled_) {
        int ret = esp_codec_dev_read(input_dev_, (void*)dest, samples * sizeof(int16_t));
        if (ret < 0) {
            ESP_LOGE(TAG, "Read failed: %d (requested %d samples at %dHz)", ret, samples, input_sample_rate_);
        }
        // Reset input timer
        if (input_timer_) {
            esp_timer_stop(input_timer_);
            esp_timer_start_once(input_timer_, TIMER_TIMEOUT_US);
        }
    }
    return samples;
}

int AdcI2sAudioCodec::Write(const int16_t* data, int samples) {
    if (output_enabled_) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t)));
        // Reset output timer
        if (output_timer_) {
            esp_timer_stop(output_timer_);
            esp_timer_start_once(output_timer_, TIMER_TIMEOUT_US);
        }
    }
    return samples;
}

void AdcI2sAudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    ESP_LOGI(TAG, "EnableInput: %d, sample_rate=%d", enable, input_sample_rate_);
    if (enable) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
            .sample_rate = (uint32_t)input_sample_rate_,
            .mclk_multiple = 0,
        };
        ESP_ERROR_CHECK(esp_codec_dev_open(input_dev_, &fs));
        
        if (input_timer_) {
            esp_timer_start_once(input_timer_, TIMER_TIMEOUT_US);
        }
    } else {
        if (input_timer_) {
            esp_timer_stop(input_timer_);
        }
        ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    }
    input_enabled_ = enable;
    ESP_LOGI(TAG, "Input %s", enable ? "enabled" : "disabled");
}

void AdcI2sAudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    ESP_LOGI(TAG, "EnableOutput: %d, sample_rate=%d", enable, output_sample_rate_);
    if (enable) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = 0,
            .sample_rate = (uint32_t)output_sample_rate_,
            .mclk_multiple = 0,
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_open(output_dev_, &fs));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_out_vol(output_dev_, output_volume_));
        
        if (output_timer_) {
            esp_timer_start_once(output_timer_, TIMER_TIMEOUT_US);
        }
    } else {
        if (output_timer_) {
            esp_timer_stop(output_timer_);
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_close(output_dev_));
    }
    output_enabled_ = enable;
    ESP_LOGI(TAG, "Output %s", enable ? "enabled" : "disabled");
}

void AdcI2sAudioCodec::SetOutputVolume(int volume) {
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, volume));
    output_volume_ = volume;
}

void AdcI2sAudioCodec::Start() {
    Settings settings("audio", false);
    output_volume_ = settings.GetInt("output_volume", output_volume_);
    if (output_volume_ <= 0) {
        ESP_LOGW(TAG, "Output volume value (%d) is too small, setting to default (10)", output_volume_);
        output_volume_ = 10;
    }

    EnableInput(true);
    EnableOutput(true);
    ESP_LOGI(TAG, "Audio codec started");
}

void AdcI2sAudioCodec::OutputTimerCallback(void* arg) {
    AdcI2sAudioCodec* codec = static_cast<AdcI2sAudioCodec*>(arg);
    if (codec && codec->output_enabled_) {
        codec->EnableOutput(false);
    }
}

void AdcI2sAudioCodec::InputTimerCallback(void* arg) {
    AdcI2sAudioCodec* codec = static_cast<AdcI2sAudioCodec*>(arg);
    if (codec && codec->input_enabled_) {
        codec->EnableInput(false);
    }
}
