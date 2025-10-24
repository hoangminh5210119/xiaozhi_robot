#ifndef _ADC_I2S_AUDIO_CODEC_H
#define _ADC_I2S_AUDIO_CODEC_H

#include "audio_codec.h"
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <esp_timer.h>
#include <driver/i2s_std.h>

class AdcI2sAudioCodec : public AudioCodec {
private:
    esp_codec_dev_handle_t input_dev_ = nullptr;
    esp_codec_dev_handle_t output_dev_ = nullptr;
    i2s_chan_handle_t tx_handle_ = nullptr;
    esp_timer_handle_t output_timer_ = nullptr;
    esp_timer_handle_t input_timer_ = nullptr;
    gpio_num_t pa_ctrl_pin_ = GPIO_NUM_NC;

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

    static void OutputTimerCallback(void* arg);
    static void InputTimerCallback(void* arg);

public:
    AdcI2sAudioCodec(int input_sample_rate, int output_sample_rate,
        uint32_t adc_mic_channel, 
        gpio_num_t i2s_bclk, gpio_num_t i2s_ws, 
        gpio_num_t i2s_dout, gpio_num_t i2s_din);
    virtual ~AdcI2sAudioCodec();

    virtual void Start() override;
    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;
};

#endif // _ADC_I2S_AUDIO_CODEC_H
