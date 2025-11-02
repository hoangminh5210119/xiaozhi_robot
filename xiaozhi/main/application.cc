#include "application.h"
#include "I2CCommandBridge.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "mqtt_protocol.h"
#include "settings.h"
#include "system_info.h"
#include "websocket_protocol.h"

#include <arpa/inet.h>
#include <cJSON.h>
#include <cstring>
#include <driver/gpio.h>
#include <esp_log.h>
#include <font_awesome.h>
#include <wifi_station.h>

#define TAG "Application"

static const char *const STATE_STRINGS[] = {
    "unknown",    "starting",      "configuring", "idle",
    "connecting", "listening",     "speaking",    "upgrading",
    "activating", "audio_testing", "fatal_error", "invalid_state"};

Application::Application() {
  event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error                                                                         \
    "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
  aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
  aec_mode_ = kAecOnServerSide;
#else
  aec_mode_ = kAecOff;
#endif

  esp_timer_create_args_t clock_timer_args = {
      .callback =
          [](void *arg) {
            Application *app = (Application *)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
          },
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "clock_timer",
      .skip_unhandled_events = true};
  esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
  if (clock_timer_handle_ != nullptr) {
    esp_timer_stop(clock_timer_handle_);
    esp_timer_delete(clock_timer_handle_);
  }
  vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();
  auto &assets = Assets::GetInstance();

  if (!assets.partition_valid()) {
    ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
    return;
  }

  Settings settings("assets", true);
  // Check if there is a new assets need to be downloaded
  std::string download_url = settings.GetString("download_url");

  if (!download_url.empty()) {
    settings.EraseKey("download_url");

    char message[256];
    snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS,
             download_url.c_str());
    Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down",
          Lang::Sounds::OGG_UPGRADE);

    // Wait for the audio service to be idle for 3 seconds
    vTaskDelay(pdMS_TO_TICKS(3000));
    SetDeviceState(kDeviceStateUpgrading);
    board.SetPowerSaveMode(false);
    display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

    bool success = assets.Download(
        download_url, [display](int progress, size_t speed) -> void {
          std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress,
                     speed / 1024);
            display->SetChatMessage("system", buffer);
          }).detach();
        });

    board.SetPowerSaveMode(true);
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (!success) {
      Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED,
            "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
      vTaskDelay(pdMS_TO_TICKS(2000));
      return;
    }
  }

  // Apply assets
  assets.Apply();
  display->SetChatMessage("system", "");
  display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion(Ota &ota) {
  const int MAX_RETRY = 10;
  int retry_count = 0;
  int retry_delay = 10; // 初始重试延迟为10秒

  auto &board = Board::GetInstance();
  while (true) {
    SetDeviceState(kDeviceStateActivating);
    auto display = board.GetDisplay();
    display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

    if (!ota.CheckVersion()) {
      retry_count++;
      if (retry_count >= MAX_RETRY) {
        ESP_LOGE(TAG, "Too many retries, exit version check");
        return;
      }

      char buffer[256];
      snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED,
               retry_delay, ota.GetCheckVersionUrl().c_str());
      Alert(Lang::Strings::ERROR, buffer, "cloud_slash",
            Lang::Sounds::OGG_EXCLAMATION);

      ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)",
               retry_delay, retry_count, MAX_RETRY);
      for (int i = 0; i < retry_delay; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (device_state_ == kDeviceStateIdle) {
          break;
        }
      }
      retry_delay *= 2; // 每次重试后延迟时间翻倍
      continue;
    }
    retry_count = 0;
    retry_delay = 10; // 重置重试延迟时间

    if (ota.HasNewVersion()) {
      // if (UpgradeFirmware(ota)) {
      //   return; // This line will never be reached after reboot
      // }
      // If upgrade failed, continue to normal operation (don't break, just fall
      // through)
    }

    // No new version, mark the current version as valid
    ota.MarkCurrentVersionValid();
    if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
      xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
      // Exit the loop if done checking new version
      break;
    }

    display->SetStatus(Lang::Strings::ACTIVATION);
    // Activation code is shown to the user and waiting for the user to input
    if (ota.HasActivationCode()) {
      ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
    }

    // This will block the loop until the activation is done or timeout
    for (int i = 0; i < 10; ++i) {
      ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
      esp_err_t err = ota.Activate();
      if (err == ESP_OK) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
        break;
      } else if (err == ESP_ERR_TIMEOUT) {
        vTaskDelay(pdMS_TO_TICKS(3000));
      } else {
        vTaskDelay(pdMS_TO_TICKS(10000));
      }
      if (device_state_ == kDeviceStateIdle) {
        break;
      }
    }
  }
}

void Application::ShowActivationCode(const std::string &code,
                                     const std::string &message) {
  struct digit_sound {
    char digit;
    const std::string_view &sound;
  };
  static const std::array<digit_sound, 10> digit_sounds{
      {digit_sound{'0', Lang::Sounds::OGG_0},
       digit_sound{'1', Lang::Sounds::OGG_1},
       digit_sound{'2', Lang::Sounds::OGG_2},
       digit_sound{'3', Lang::Sounds::OGG_3},
       digit_sound{'4', Lang::Sounds::OGG_4},
       digit_sound{'5', Lang::Sounds::OGG_5},
       digit_sound{'6', Lang::Sounds::OGG_6},
       digit_sound{'7', Lang::Sounds::OGG_7},
       digit_sound{'8', Lang::Sounds::OGG_8},
       digit_sound{'9', Lang::Sounds::OGG_9}}};

  // This sentence uses 9KB of SRAM, so we need to wait for it to finish
  Alert(Lang::Strings::ACTIVATION, message.c_str(), "link",
        Lang::Sounds::OGG_ACTIVATION);

  for (const auto &digit : code) {
    auto it = std::find_if(
        digit_sounds.begin(), digit_sounds.end(),
        [digit](const digit_sound &ds) { return ds.digit == digit; });
    if (it != digit_sounds.end()) {
      audio_service_.PlaySound(it->sound);
    }
  }
}

void Application::Alert(const char *status, const char *message,
                        const char *emotion, const std::string_view &sound) {
  ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
  auto display = Board::GetInstance().GetDisplay();
  display->SetStatus(status);
  display->SetEmotion(emotion);
  display->SetChatMessage("system", message);
  if (!sound.empty()) {
    audio_service_.PlaySound(sound);
  }
}

void Application::DismissAlert() {
  if (device_state_ == kDeviceStateIdle) {
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(Lang::Strings::STANDBY);
    display->SetEmotion("neutral");
    display->SetChatMessage("system", "");
  }
}

void Application::ToggleChatState() {
  if (device_state_ == kDeviceStateActivating) {
    SetDeviceState(kDeviceStateIdle);
    return;
  } else if (device_state_ == kDeviceStateWifiConfiguring) {
    audio_service_.EnableAudioTesting(true);
    SetDeviceState(kDeviceStateAudioTesting);
    return;
  } else if (device_state_ == kDeviceStateAudioTesting) {
    audio_service_.EnableAudioTesting(false);
    SetDeviceState(kDeviceStateWifiConfiguring);
    return;
  }

  if (!protocol_) {
    ESP_LOGE(TAG, "Protocol not initialized");
    return;
  }

  if (device_state_ == kDeviceStateIdle) {
    Schedule([this]() {
      if (!protocol_->IsAudioChannelOpened()) {
        SetDeviceState(kDeviceStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
          return;
        }
      }

      SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                            : kListeningModeRealtime);
    });
  } else if (device_state_ == kDeviceStateSpeaking) {
    Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
  } else if (device_state_ == kDeviceStateListening) {
    Schedule([this]() { protocol_->CloseAudioChannel(); });
  }
}

void Application::StartListening() {
  if (device_state_ == kDeviceStateActivating) {
    SetDeviceState(kDeviceStateIdle);
    return;
  } else if (device_state_ == kDeviceStateWifiConfiguring) {
    audio_service_.EnableAudioTesting(true);
    SetDeviceState(kDeviceStateAudioTesting);
    return;
  }

  if (!protocol_) {
    ESP_LOGE(TAG, "Protocol not initialized");
    return;
  }

  if (device_state_ == kDeviceStateIdle) {
    Schedule([this]() {
      if (!protocol_->IsAudioChannelOpened()) {
        SetDeviceState(kDeviceStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
          return;
        }
      }

      SetListeningMode(kListeningModeManualStop);
    });
  } else if (device_state_ == kDeviceStateSpeaking) {
    Schedule([this]() {
      AbortSpeaking(kAbortReasonNone);
      SetListeningMode(kListeningModeManualStop);
    });
  }
}

void Application::StopListening() {
  if (device_state_ == kDeviceStateAudioTesting) {
    audio_service_.EnableAudioTesting(false);
    SetDeviceState(kDeviceStateWifiConfiguring);
    return;
  }

  const std::array<int, 3> valid_states = {
      kDeviceStateListening,
      kDeviceStateSpeaking,
      kDeviceStateIdle,
  };
  // If not valid, do nothing
  if (std::find(valid_states.begin(), valid_states.end(), device_state_) ==
      valid_states.end()) {
    return;
  }

  Schedule([this]() {
    if (device_state_ == kDeviceStateListening) {
      protocol_->SendStopListening();
      SetDeviceState(kDeviceStateIdle);
    }
  });
}

void Application::Start() {
  auto &board = Board::GetInstance();
  SetDeviceState(kDeviceStateStarting);

  /* Setup the display */
  auto display = board.GetDisplay();

  // Print board name/version info
  display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

  /* Setup the audio service */
  auto codec = board.GetAudioCodec();
  audio_service_.Initialize(codec);
  audio_service_.Start();

  AudioServiceCallbacks callbacks;
  callbacks.on_send_queue_available = [this]() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
  };
  callbacks.on_wake_word_detected = [this](const std::string &wake_word) {
    xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
  };
  callbacks.on_vad_change = [this](bool speaking) {
    xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
  };
  audio_service_.SetCallbacks(callbacks);

  // Start the main event loop task with priority 3
  xTaskCreate(
      [](void *arg) {
        ((Application *)arg)->MainEventLoop();
        vTaskDelete(NULL);
      },
      "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

  /* Start the clock timer to update the status bar */
  esp_timer_start_periodic(clock_timer_handle_, 1000000);

  /* Wait for the network to be ready */
  board.StartNetwork();

  // Update the status bar immediately to show the network state
  display->UpdateStatusBar(true);

  // Check for new assets version
  CheckAssetsVersion();

  // Check for new firmware version or get the MQTT broker address
  Ota ota;
  CheckNewVersion(ota);

  // Initialize the protocol
  display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

  // Add MCP common tools before initializing the protocol
  auto &mcp_server = McpServer::GetInstance();
  mcp_server.AddCommonTools();
  mcp_server.AddUserOnlyTools();

  if (ota.HasMqttConfig()) {
    protocol_ = std::make_unique<MqttProtocol>();
  } else if (ota.HasWebsocketConfig()) {
    protocol_ = std::make_unique<WebsocketProtocol>();
  } else {
    ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
    protocol_ = std::make_unique<MqttProtocol>();
  }

  protocol_ = std::make_unique<MqttProtocol>();

  protocol_->OnConnected([this]() { DismissAlert(); });

  protocol_->OnNetworkError([this](const std::string &message) {
    last_error_message_ = message;
    xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
  });
  protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
    if (device_state_ == kDeviceStateSpeaking) {
      audio_service_.PushPacketToDecodeQueue(std::move(packet));
    }
  });
  protocol_->OnAudioChannelOpened([this, codec, &board]() {
    board.SetPowerSaveMode(false);
    if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
      ESP_LOGW(TAG,
               "Server sample rate %d does not match device output sample rate "
               "%d, resampling may cause distortion",
               protocol_->server_sample_rate(), codec->output_sample_rate());
    }
  });
  protocol_->OnAudioChannelClosed([this, &board]() {
    board.SetPowerSaveMode(true);
    Schedule([this]() {
      auto display = Board::GetInstance().GetDisplay();
      display->SetChatMessage("system", "");
      SetDeviceState(kDeviceStateIdle);
    });
  });
  protocol_->OnIncomingJson([this, display](const cJSON *root) {
    // Parse JSON data
    auto type = cJSON_GetObjectItem(root, "type");
    if (strcmp(type->valuestring, "tts") == 0) {
      auto state = cJSON_GetObjectItem(root, "state");
      if (strcmp(state->valuestring, "start") == 0) {
        Schedule([this]() {
          aborted_ = false;
          if (device_state_ == kDeviceStateIdle ||
              device_state_ == kDeviceStateListening) {
            SetDeviceState(kDeviceStateSpeaking);
          }
        });
      } else if (strcmp(state->valuestring, "stop") == 0) {
        Schedule([this]() {
          if (device_state_ == kDeviceStateSpeaking) {
            if (listening_mode_ == kListeningModeManualStop) {
              SetDeviceState(kDeviceStateIdle);
            } else {
              SetDeviceState(kDeviceStateListening);
            }
          }
        });
      } else if (strcmp(state->valuestring, "sentence_start") == 0) {
        auto text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text)) {
          ESP_LOGI(TAG, "<< %s", text->valuestring);
          Schedule([this, display, message = std::string(text->valuestring)]() {
            display->SetChatMessage("assistant", message.c_str());
          });
        }
      }
    } else if (strcmp(type->valuestring, "stt") == 0) {
      auto text = cJSON_GetObjectItem(root, "text");
      if (cJSON_IsString(text)) {
        ESP_LOGI(TAG, ">> %s", text->valuestring);
        Schedule([this, display, message = std::string(text->valuestring)]() {
          display->SetChatMessage("user", message.c_str());
        });
      }
    } else if (strcmp(type->valuestring, "llm") == 0) {
      auto emotion = cJSON_GetObjectItem(root, "emotion");
      if (cJSON_IsString(emotion)) {
        Schedule(
            [this, display, emotion_str = std::string(emotion->valuestring)]() {
              display->SetEmotion(emotion_str.c_str());
            });
      }
    } else if (strcmp(type->valuestring, "mcp") == 0) {
      auto payload = cJSON_GetObjectItem(root, "payload");
      if (cJSON_IsObject(payload)) {
        McpServer::GetInstance().ParseMessage(payload);
      }
    } else if (strcmp(type->valuestring, "system") == 0) {
      auto command = cJSON_GetObjectItem(root, "command");
      if (cJSON_IsString(command)) {
        ESP_LOGI(TAG, "System command: %s", command->valuestring);
        if (strcmp(command->valuestring, "reboot") == 0) {
          // Do a reboot if user requests a OTA update
          Schedule([this]() { Reboot(); });
        } else {
          ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
        }
      }
    } else if (strcmp(type->valuestring, "alert") == 0) {
      auto status = cJSON_GetObjectItem(root, "status");
      auto message = cJSON_GetObjectItem(root, "message");
      auto emotion = cJSON_GetObjectItem(root, "emotion");
      if (cJSON_IsString(status) && cJSON_IsString(message) &&
          cJSON_IsString(emotion)) {
        Alert(status->valuestring, message->valuestring, emotion->valuestring,
              Lang::Sounds::OGG_VIBRATION);
      } else {
        ESP_LOGW(TAG, "Alert command requires status, message and emotion");
      }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
    } else if (strcmp(type->valuestring, "custom") == 0) {
      auto payload = cJSON_GetObjectItem(root, "payload");
      ESP_LOGI(TAG, "Received custom message: %s",
               cJSON_PrintUnformatted(root));
      if (cJSON_IsObject(payload)) {
        Schedule(
            [this, display,
             payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
              display->SetChatMessage("system", payload_str.c_str());
            });
      } else {
        ESP_LOGW(TAG, "Invalid custom message format: missing payload");
      }
#endif
    } else {
      ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
    }
  });
  bool protocol_started = protocol_->Start();

  SystemInfo::PrintHeapStats();
  SetDeviceState(kDeviceStateIdle);

  has_server_time_ = ota.HasServerTime();
  if (protocol_started) {
    std::string message =
        std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");
    // Play the success sound to indicate the device is ready
    audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
  }

  auto scheduler_ = &RecurringSchedule::GetInstance();
  // scheduler_->begin();
  scheduler_->setCallback([this](int id, const std::string &note) {
    ESP_LOGI(TAG, "⏰ Schedule reminder: %s", note.c_str());

    // Thêm prefix để AI chỉ đọc lại, không trả lời thêm
    std::string tts_text = "Thông báo nhắc nhở: " + note;

    auto &app = Application::GetInstance();
    app.SendTextCommandToServer(tts_text);
  });

  // ==================== I2C COMMAND BRIDGE INITIALIZATION ====================
  // Khởi tạo I2C Command Bridge để giao tiếp với Arduino actuator
  // ESP_LOGI(TAG, "🔧 Initializing I2C Command Bridge...");
  auto &i2c_bridge = I2CCommandBridge::GetInstance();

  if (!i2c_bridge.Init()) {
    ESP_LOGW(TAG, "⚠️  I2C Command Bridge init failed, continuing without "
                  "actuator control");
  } else {
    ESP_LOGI(TAG, "✅ I2C Command Bridge initialized");

    // Đăng ký callback để nhận status updates từ actuator
    // Lambda không capture có thể convert thành function pointer
    i2c_bridge.SetStatusCallback(
        [](const ActuatorStatus &status, void *user_data) {
          ESP_LOGI(TAG, "📊 Actuator Status Update:");
          ESP_LOGI(TAG, "   Battery: %.1fV, BLE: %d, Heart Rate: %d",
                   status.battery, status.ble_connected, status.heart_rate);
          ESP_LOGI(TAG, "   Storage Slots: [%d, %d, %d, %d]",
                   status.storage[0].is_open, status.storage[1].is_open,
                   status.storage[2].is_open, status.storage[3].is_open);

          // Lưu actuator status và heartrate info vào Application instance
          if (user_data) {
            auto *app = static_cast<Application *>(user_data);
            app->last_actuator_status_ = status; // Lưu toàn bộ status
            app->heartrate_info_ =
                "{ \"ble_connected\": " + std::to_string(status.ble_connected) +
                ", \"heart_rate\": " + std::to_string(status.heart_rate) + " }";
          }
        },
        this); // Pass 'this' as user_data

    // Bật polling để tự động nhận status mỗi 2 giây
    if (i2c_bridge.StartStatusPolling(2000)) {
      ESP_LOGI(TAG, "✅ I2C status polling started (interval: 2s)");
    } else {
      ESP_LOGW(TAG, "⚠️  Failed to start status polling");
    }
  }

  InitializeTelegramBot();
}
std::string Application::getHeartRate() { return heartrate_info_; }

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    main_tasks_.push_back(std::move(callback));
  }
  xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
  while (true) {
    auto bits = xEventGroupWaitBits(
        event_group_,
        MAIN_EVENT_SCHEDULE | MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED | MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK | MAIN_EVENT_ERROR,
        pdTRUE, pdFALSE, portMAX_DELAY);

    if (bits & MAIN_EVENT_ERROR) {
      SetDeviceState(kDeviceStateIdle);
      Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark",
            Lang::Sounds::OGG_EXCLAMATION);
    }

    if (bits & MAIN_EVENT_SEND_AUDIO) {
      while (auto packet = audio_service_.PopPacketFromSendQueue()) {
        ESP_LOGD(TAG, "Sending audio packet, size()");
        if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
          ESP_LOGD(TAG, "SEND audio DONE");
          break;
        }
      }
    }

    if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
      OnWakeWordDetected();
    }

    if (bits & MAIN_EVENT_VAD_CHANGE) {
      if (device_state_ == kDeviceStateListening) {
        auto led = Board::GetInstance().GetLed();
        led->OnStateChanged();
      }
    }

    if (bits & MAIN_EVENT_SCHEDULE) {
      std::unique_lock<std::mutex> lock(mutex_);
      auto tasks = std::move(main_tasks_);
      lock.unlock();
      for (auto &task : tasks) {
        task();
      }
    }

    if (bits & MAIN_EVENT_CLOCK_TICK) {
      clock_ticks_++;
      auto display = Board::GetInstance().GetDisplay();
      display->UpdateStatusBar();

      // Print the debug info every 10 seconds
      if (clock_ticks_ % 10 == 0) {
        // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
        // SystemInfo::PrintTaskList();
        SystemInfo::PrintHeapStats();
      }
    }
  }
}

void Application::OnWakeWordDetected() {
  if (!protocol_) {
    return;
  }

  if (device_state_ == kDeviceStateIdle) {
    audio_service_.EncodeWakeWord();

    if (!protocol_->IsAudioChannelOpened()) {
      SetDeviceState(kDeviceStateConnecting);
      if (!protocol_->OpenAudioChannel()) {
        audio_service_.EnableWakeWordDetection(true);
        return;
      }
    }

    auto wake_word = audio_service_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
    // Encode and send the wake word data to the server
    while (auto packet = audio_service_.PopWakeWordPacket()) {
      protocol_->SendAudio(std::move(packet));
    }
    // Set the chat state to wake word detected
    protocol_->SendWakeWordDetected(wake_word);
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                          : kListeningModeRealtime);
#else
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                          : kListeningModeRealtime);
    // Play the pop up sound to indicate the wake word is detected
    audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
  } else if (device_state_ == kDeviceStateSpeaking) {
    AbortSpeaking(kAbortReasonWakeWordDetected);
  } else if (device_state_ == kDeviceStateActivating) {
    SetDeviceState(kDeviceStateIdle);
  }
}

void Application::AbortSpeaking(AbortReason reason) {
  ESP_LOGI(TAG, "Abort speaking");
  aborted_ = true;
  if (protocol_) {
    protocol_->SendAbortSpeaking(reason);
  }
}

void Application::SetListeningMode(ListeningMode mode) {
  listening_mode_ = mode;
  SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
  if (device_state_ == state) {
    return;
  }

  clock_ticks_ = 0;
  auto previous_state = device_state_;
  device_state_ = state;
  ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

  // Send the state change event
  DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state,
                                                              state);

  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();
  auto led = board.GetLed();
  led->OnStateChanged();

  // if (previous_state == kDeviceStateIdle && state != kDeviceStateIdle) {
  //   auto music = board.GetMusic();
  //   if (music) {
  //     ESP_LOGI(TAG, "Stopping music streaming due to state change: %s -> %s",
  //              STATE_STRINGS[previous_state], STATE_STRINGS[state]);
  //     music->StopStreaming();
  //   }
  // }

  switch (state) {
  case kDeviceStateUnknown:
  case kDeviceStateIdle:
    display->SetStatus(Lang::Strings::STANDBY);
    display->SetEmotion("neutral");
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(true);
    break;
  case kDeviceStateConnecting:
    display->SetStatus(Lang::Strings::CONNECTING);
    display->SetEmotion("neutral");
    display->SetChatMessage("system", "");
    break;
  case kDeviceStateListening:
    display->SetStatus(Lang::Strings::LISTENING);
    display->SetEmotion("neutral");

    // Make sure the audio processor is running
    if (!audio_service_.IsAudioProcessorRunning()) {
      // Send the start listening command
      protocol_->SendStartListening(listening_mode_);
      audio_service_.EnableVoiceProcessing(true);
      audio_service_.EnableWakeWordDetection(false);
    }
    break;
  case kDeviceStateSpeaking:
    display->SetStatus(Lang::Strings::SPEAKING);

    if (listening_mode_ != kListeningModeRealtime) {
      audio_service_.EnableVoiceProcessing(false);
      // Only AFE wake word can be detected in speaking mode
      audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
    }
    audio_service_.ResetDecoder();
    break;
  default:
    // Do nothing
    break;
  }
}

void Application::Reboot() {
  ESP_LOGI(TAG, "Rebooting...");
  // Disconnect the audio channel
  if (protocol_ && protocol_->IsAudioChannelOpened()) {
    protocol_->CloseAudioChannel();
  }
  protocol_.reset();
  audio_service_.Stop();

  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
}

bool Application::UpgradeFirmware(Ota &ota, const std::string &url) {
  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();

  // Use provided URL or get from OTA object
  std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
  std::string version_info =
      url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";

  // Close audio channel if it's open
  if (protocol_ && protocol_->IsAudioChannelOpened()) {
    ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
    protocol_->CloseAudioChannel();
  }
  ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

  Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download",
        Lang::Sounds::OGG_UPGRADE);
  vTaskDelay(pdMS_TO_TICKS(3000));

  SetDeviceState(kDeviceStateUpgrading);

  std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
  display->SetChatMessage("system", message.c_str());

  board.SetPowerSaveMode(false);
  audio_service_.Stop();
  vTaskDelay(pdMS_TO_TICKS(1000));

  bool upgrade_success = ota.StartUpgradeFromUrl(
      upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
          char buffer[32];
          snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress,
                   speed / 1024);
          display->SetChatMessage("system", buffer);
        }).detach();
      });

  if (!upgrade_success) {
    // Upgrade failed, restart audio service and continue running
    ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and "
                  "continuing operation...");
    audio_service_.Start();       // Restart audio service
    board.SetPowerSaveMode(true); // Restore power save mode
    Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark",
          Lang::Sounds::OGG_EXCLAMATION);
    vTaskDelay(pdMS_TO_TICKS(3000));
    return false;
  } else {
    // Upgrade success, reboot immediately
    ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
    display->SetChatMessage("system", "Upgrade successful, rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
    Reboot();
    return true;
  }
}

void Application::WakeWordInvoke(const std::string &wake_word) {
  if (device_state_ == kDeviceStateIdle) {
    ToggleChatState();
    Schedule([this, wake_word]() {
      if (protocol_) {
        protocol_->SendWakeWordDetected(wake_word);
      }
    });
  } else if (device_state_ == kDeviceStateSpeaking) {
    Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
  } else if (device_state_ == kDeviceStateListening) {
    Schedule([this]() {
      if (protocol_) {
        protocol_->CloseAudioChannel();
      }
    });
  }
}

bool Application::CanEnterSleepMode() {
  if (device_state_ != kDeviceStateIdle) {
    return false;
  }

  if (protocol_ && protocol_->IsAudioChannelOpened()) {
    return false;
  }

  if (!audio_service_.IsIdle()) {
    return false;
  }

  // Now it is safe to enter sleep mode
  return true;
}

void Application::SendMcpMessage(const std::string &payload) {
  if (protocol_ == nullptr) {
    return;
  }

  // Make sure you are using main thread to send MCP message
  if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
    protocol_->SendMcpMessage(payload);
  } else {
    Schedule([this, payload = std::move(payload)]() {
      protocol_->SendMcpMessage(payload);
    });
  }
}

void Application::SetAecMode(AecMode mode) {
  aec_mode_ = mode;
  Schedule([this]() {
    auto &board = Board::GetInstance();
    auto display = board.GetDisplay();
    switch (aec_mode_) {
    case kAecOff:
      audio_service_.EnableDeviceAec(false);
      display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
      break;
    case kAecOnServerSide:
      audio_service_.EnableDeviceAec(false);
      display->ShowNotification(Lang::Strings::RTC_MODE_ON);
      break;
    case kAecOnDeviceSide:
      audio_service_.EnableDeviceAec(true);
      display->ShowNotification(Lang::Strings::RTC_MODE_ON);
      break;
    }

    // If the AEC mode is changed, close the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
      protocol_->CloseAudioChannel();
    }
  });
}

void Application::PlaySound(const std::string_view &sound) {
  audio_service_.PlaySound(sound);
}

// Thêm method khởi tạo Telegram bot
void Application::InitializeTelegramBot() {
  if (telegram_initialized_) {
    return;
  }

  ESP_LOGI(TAG, "🤖 Initializing Telegram bot...");

  // telegram_bot_ = std::make_unique<TelegramBot>(BOT_TOKEN, CHAT_ID);
  // telegram_bot_ = TelegramBot();

  telegram_bot_ = std::make_unique<TelegramBot>("", "");

  // Set message callback
  telegram_bot_->setMessageCallback([this](const TelegramMessage &message) {
    this->OnTelegramMessage(message);
  });

  // Add commands to bot menu
  // telegram_bot_->addCommand("start", "Start the bot");
  // telegram_bot_->addCommand("help", "Show help message");
  // telegram_bot_->addCommand("status", "Get system status");
  // telegram_bot_->addCommand("test", "Test bot functionality");
  // telegram_bot_->addCommand("restart", "Restart the device");
  // telegram_bot_->addCommand("state", "Get device state");
  // telegram_bot_->addCommand("listen", "Start listening");
  // telegram_bot_->addCommand("stop", "Stop listening");

  // Initialize bot
  esp_err_t err = telegram_bot_->init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "❌ Failed to initialize Telegram bot");
    return;
  }

  // Upload commands to Telegram
  err = telegram_bot_->uploadCommands();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "⚠️ Failed to upload commands");
  }

  // Start message polling
  err = telegram_bot_->startPolling();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "❌ Failed to start polling");
    return;
  }

  telegram_initialized_ = true;
  ESP_LOGI(TAG, "✅ Telegram bot initialized successfully");

  // Send startup notification
  // SendTelegramMessage("🚀 ESP32 device started and connected!");
}

// Callback cho tin nhắn Telegram
void Application::OnTelegramMessage(const TelegramMessage &message) {
  ESP_LOGI(TAG, "📱 New message from @%s: %s", message.username.c_str(),
           message.text.c_str());

  // Handle commands
  if (message.text == "/start") {
    telegram_bot_->sendMessage(
        message.chat_id,
        "🤖 Hello! I'm your ESP32 bot. Send /help for commands.");
  } else if (message.text == "/help") {
    std::string help_text = "🔧 Available commands:\n"
                            "/start - Start the bot\n"
                            "/heartrate - get heartrate\n"
                            "/help - Show this help\n"
                            "/status - Get system status\n"
                            "/state - Get device state\n"
                            "/listen - Start listening\n"
                            "/stop - Stop listening\n"
                            "/restart - Restart the device\n"
                            "/test - Test message";
    telegram_bot_->sendMessage(message.chat_id, help_text);
  } else if (message.text == "/status") {
    // std::string status = "📊 System Status:\n";
    // status += "State: " + std::string(STATE_STRINGS[device_state_]) + "\n";
    // status += "WiFi: " + Board::GetInstance().GetNetworkStatus() + "\n";
    // status += "Free Heap: " + std::to_string(esp_get_free_heap_size() / 1024)
    // + " KB\n"; status += "Uptime: " + std::to_string(clock_ticks_ / 60) + "
    // minutes\n"; telegram_bot_->sendMessage(message.chat_id, status);
  } else if (message.text == "/heartrate") {
    telegram_bot_->sendMessage(message.chat_id,
                               "📊 Heartrate Info: " + getHeartRate());
  } else if (message.text == "/state") {
    std::string state_msg =
        "🔄 Current device state: " + std::string(STATE_STRINGS[device_state_]);
    telegram_bot_->sendMessage(message.chat_id, state_msg);
  } else if (message.text == "/listen") {
    Schedule([this]() { StartListening(); });
    telegram_bot_->sendMessage(message.chat_id, "🎤 Starting to listen...");
  } else if (message.text == "/stop") {
    Schedule([this]() { StopListening(); });
    telegram_bot_->sendMessage(message.chat_id, "⏹️ Stopping listening...");
  } else if (message.text == "/test") {
    telegram_bot_->sendMessage(message.chat_id,
                               "✅ Test successful! Bot is working fine.");
  } else if (message.text == "/restart") {
    telegram_bot_->sendMessage(message.chat_id, "🔄 Restarting device...");
    Schedule([this]() {
      vTaskDelay(pdMS_TO_TICKS(2000)); // Delay để message được gửi
      Reboot();
    });
  } else {
    // Echo the message
    // telegram_bot_->sendMessage(message.chat_id, "🔄 Echo: " + message.text);
    ESP_LOGI(TAG, "receive msg: %s", message.text.c_str());
  }
}

std::string Application::GetTelegramMsgBufferAsJson() const {
  if (telegram_bot_) {
    return telegram_bot_->getAllMessagesAsJson();
  }
  return "{}";
}

void Application::SendTelegramMessage(const std::string &message) {
  if (telegram_bot_ && telegram_initialized_) {
    telegram_bot_->sendMessage(message);
  }
}

void Application::SendTextCommandToServer(const std::string &text) {
  // if (!protocol_) {
  //   ESP_LOGE(TAG, "Protocol not initialized");
  //   return;
  // }

  // Schedule([this, text]() {
  //   // Kiểm tra xem audio channel có đang mở không
  //   if (!protocol_->IsAudioChannelOpened()) {
  //     ESP_LOGI(TAG,
  //              "Audio channel closed, opening before sending text command");
  //     SetDeviceState(kDeviceStateConnecting);

  //     // Mở audio channel để lấy session_id mới
  //     if (!protocol_->OpenAudioChannel()) {
  //       ESP_LOGE(TAG, "Failed to open audio channel");
  //       return;
  //     }
  //   }

  //   // protocol_->SendStartListening(kListeningModeRealtime);

  //   // ✅ QUAN TRỌNG: Gửi start listening mode TRƯỚC khi gửi text
  //   // Theo protocol WebSocket: start → detect (cho text dài)
  //   ESP_LOGI(TAG, "📤 Sending start listening before text command");
  //   protocol_->SendStartListening(kListeningModeAutoStop);
  //   // this->startListening();

  //   // Đợi server chuẩn bị (200ms - đủ thời gian server xử lý start)
  //   vTaskDelay(pdMS_TO_TICKS(200));

  //   // Bây giờ mới gửi text command (detect state)
  //   ESP_LOGI(TAG, "📤 Sending text command: %s", text.c_str());
  //   protocol_->SendTextCommand(text);

  //   // ✅ QUAN TRỌNG: Giữ ở state Listening để nhận TTS audio từ server
  //   // Server sẽ tự động gửi audio về và chuyển sang Speaking state
  //   // KHÔNG đóng channel ngay, đợi server xử lý xong
  //   // SetDeviceState(kDeviceStateListening);

  //   ESP_LOGI(TAG, "⏳ Waiting for server TTS response...");
  // });

  if (!protocol_) {
    ESP_LOGE(TAG, "Protocol not initialized");
    return;
  }

  Schedule([this, text]() {
    // Kiểm tra xem audio channel có đang mở không
    if (!protocol_->IsAudioChannelOpened()) {
      ESP_LOGI(TAG,
               "Audio channel closed, opening before sending text command");
      SetDeviceState(kDeviceStateConnecting);

      // Mở audio channel để lấy session_id mới
      if (!protocol_->OpenAudioChannel()) {
        ESP_LOGE(TAG, "Failed to open audio channel");
        return;
      }
    }
    SetDeviceState(kDeviceStateListening);

    // Bây giờ session_id_ đã hợp lệ, gửi text command
    protocol_->SendTextCommand(text);

    // Chuyển sang trạng thái listening hoặc speaking tùy logic của bạn
  });
}

// ==================== SENSOR REPORTING TO TELEGRAM ====================

static void SensorReportingTask(void *param) {
  Application *app = static_cast<Application *>(param);
  const char *TAG_SENSOR = "SensorReport";

  ESP_LOGI(TAG_SENSOR, "📊 Sensor reporting task started (interval: %us)",
           app->GetSensorReportInterval());

  while (app->IsSensorReportingEnabled()) {
    // Thu thập dữ liệu cảm biến
    std::string report;
    report.reserve(512); // Pre-allocate để giảm reallocation
    report = "📊 **Báo cáo cảm biến**\n\n";

    // Lấy actuator status qua getter
    const ActuatorStatus& status = app->GetLastActuatorStatus();

    // 1. Heart rate & BLE từ actuator
    report += "❤️ **Nhịp tim**: ";
    if (status.ble_connected) {
      report += std::to_string(status.heart_rate) + " bpm\n";
      report += "Trạng thái cảm biến: Đã kết nối\n";
    } else {
      report += "Không kết nối\n";
      report += "Trạng thái cảm biến: Ngắt kết nối\n";
    }

    // 2. Battery
    // report += "🔋 Pin: " + std::to_string(status.battery) + "V\n";

    // 3. System info
    report += "\n**Hệ thống**:\n";
    report += "💾 Free Heap: " +
              std::to_string(esp_get_free_heap_size() / 1024) + " KB\n";

    // 4. Timestamp
    time_t now;
    time(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    report += "\n🕐 Thời gian: ";
    report += time_str;

    // Gửi report qua Telegram
    ESP_LOGI(TAG_SENSOR, "📤 Sending sensor report...");
    app->SendTelegramMessage(report);

    // Đợi interval trước khi gửi lần tiếp theo
    uint32_t interval_ms = app->GetSensorReportInterval() * 1000;
    for (uint32_t i = 0; i < interval_ms / 1000; i++) {
      if (!app->IsSensorReportingEnabled()) {
        break; // Exit early if disabled
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  ESP_LOGI(TAG_SENSOR, "📊 Sensor reporting task stopped");
  vTaskDelete(NULL);
}

bool Application::StartSensorReporting(uint32_t interval_seconds) {
  if (sensor_report_enabled_) {
    ESP_LOGW(TAG, "Sensor reporting already enabled");
    return false;
  }

  if (interval_seconds < 10) {
    ESP_LOGW(TAG, "Interval too short, minimum 10 seconds");
    interval_seconds = 10;
  }

  sensor_report_interval_ms_ = interval_seconds * 1000;
  sensor_report_enabled_ = true;

  BaseType_t result = xTaskCreate(SensorReportingTask, "SensorReport",
                                  8192, // Stack size (increased from 4096)
                                  this, // Pass 'this' as parameter
                                  3,    // Priority
                                  &sensor_report_task_handle_);

  if (result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create sensor reporting task");
    sensor_report_enabled_ = false;
    return false;
  }

  ESP_LOGI(TAG, "✅ Sensor reporting started (interval: %us)",
           interval_seconds);
  return true;
}

void Application::StopSensorReporting() {
  if (!sensor_report_enabled_) {
    ESP_LOGW(TAG, "Sensor reporting not enabled");
    return;
  }

  sensor_report_enabled_ = false;

  // Wait for task to finish
  if (sensor_report_task_handle_) {
    vTaskDelay(pdMS_TO_TICKS(100)); // Give task time to exit
    // Task will delete itself
    sensor_report_task_handle_ = nullptr;
  }

  ESP_LOGI(TAG, "✅ Sensor reporting stopped");
}
