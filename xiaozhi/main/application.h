#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "audio_service.h"
#include "device_state_event.h"
#include "ota.h"
#include "protocol.h"

#include "RecurringSchedule.h"
#include "StorageManager.h"
#include "cJSON.h"

#include "TelegramBot.h" // Thêm include này

#define MAIN_EVENT_SCHEDULE (1 << 0)
#define MAIN_EVENT_SEND_AUDIO (1 << 1)
#define MAIN_EVENT_WAKE_WORD_DETECTED (1 << 2)
#define MAIN_EVENT_VAD_CHANGE (1 << 3)
#define MAIN_EVENT_ERROR (1 << 4)
#define MAIN_EVENT_CHECK_NEW_VERSION_DONE (1 << 5)
#define MAIN_EVENT_CLOCK_TICK (1 << 6)

enum AecMode {
  kAecOff,
  kAecOnDeviceSide,
  kAecOnServerSide,
};

class Application {
public:
  static Application &GetInstance() {
    static Application instance;
    return instance;
  }
  // 删除拷贝构造函数和赋值运算符
  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;

  void Start();
  void MainEventLoop();
  DeviceState GetDeviceState() const { return device_state_; }
  bool IsVoiceDetected() const { return audio_service_.IsVoiceDetected(); }
  void Schedule(std::function<void()> callback);
  void SetDeviceState(DeviceState state);
  void Alert(const char *status, const char *message, const char *emotion = "",
             const std::string_view &sound = "");
  void DismissAlert();
  void AbortSpeaking(AbortReason reason);
  void ToggleChatState();
  void StartListening();
  void StopListening();
  void Reboot();
  void WakeWordInvoke(const std::string &wake_word);
  bool UpgradeFirmware(Ota &ota, const std::string &url = "");
  bool CanEnterSleepMode();
  void SendMcpMessage(const std::string &payload);
  void SetAecMode(AecMode mode);
  AecMode GetAecMode() const { return aec_mode_; }
  void PlaySound(const std::string_view &sound);
  AudioService &GetAudioService() { return audio_service_; }

  // Thêm các method cho Telegram bot
  void InitializeTelegramBot();
  void SendTelegramMessage(const std::string &message);
  // void SendSystemStatus();
  std::string GetTelegramMsgBufferAsJson() const;

  void SendTextCommandToServer(const std::string &text);

  std::string getHeartRate();
  const ActuatorStatus& GetLastActuatorStatus() const { return last_actuator_status_; }
  
  // Sensor data reporting to Telegram
  bool StartSensorReporting(uint32_t interval_seconds);
  void StopSensorReporting();
  bool IsSensorReportingEnabled() const { return sensor_report_enabled_; }
  uint32_t GetSensorReportInterval() const { return sensor_report_interval_ms_ / 1000; }
  

//   std::string GetSchedulesAsJson();
//   void add_once_schedule(int id, int64_t unixtime, const std::string &note);
//   void add_interval_schedule(int id, uint32_t intervalSeconds,
//                              const std::string &note);
//   void
//   add_daily_schedule(int id,
//                      const std::vector<RecurringSchedule::DailyTime> &times,
//                      const std::string &note);
//   void
//   add_weekly_schedule(int id,
//                       const std::vector<RecurringSchedule::WeeklyTime> &times,
//                       const std::string &note);
//   void remove_schedule(int id);
//   size_t get_schedule_count();
//   void enable_schedule(int id, bool enable);
//   void clear_all_schedules();


//   std::string GetProductStorageAsJson();
//   bool addProductStorageSlot(int index, gpio_num_t ledPin);
//   bool putItemInProductStorage(int index, const std::string &item);
//   bool takeItemFromProductStorage(int index);
//   bool updateItemInProductStorage(int index, const std::string &item);
// //   bool getProductStorageSlotInfo(int index, StorageManager::Slot &slot);
//   int getEmptyProductStorageSlotCount();
//   int getOccupiedProductStorageSlotCount();
//   int getTotalProductStorageSlotCount();
// //   std::vector<int> findItemInProductStorageByName(const std::string &itemName);
//   bool turnOnProductStorageSlotLed(int index);
//   bool turnOffProductStorageSlotLed(int index);
//   bool blinkProductStorageSlotLed(int index, int times = 3, int delayMs = 500);
//   void turnOffAllProductStorageLeds();
//   void turnOnAllProductStorageLeds();
//   bool saveProductStorageToNvs();
//   bool loadProductStorageFromNvs();
//   bool clearProductStorageNvs();
//   void resetAllProductStorage(bool autoSave = true);

//   bool add_storage_slot(int index, gpio_num_t ledPin);
//   bool put_item_in_slot(int index, const std::string &item);
//   bool take_item_from_slot(int index);
//   bool update_item_in_slot(int index, const std::string &item);
//   bool get_slot_info(int index, StorageManager::Slot &slot);
//   int get_empty_slot_count();
//   int get_occupied_slot_count();
//   int get_total_slot_count();
//   std::vector<int> find_item_by_name(const std::string &itemName);
//   bool turn_on_slot_led(int index);
//   bool turn_off_slot_led(int index);
//   bool blink_slot_led(int index, int times = 3, int delayMs = 500);
//   void turn_off_all_leds();
//   void turn_on_all_leds();
//   bool save_storage_to_nvs();
//   bool load_storage_from_nvs();
//   bool clear_storage_nvs();
//   void reset_all_storage(bool autoSave = true);

private:
  Application();
  ~Application();

  std::mutex mutex_;
  std::deque<std::function<void()>> main_tasks_;
  std::unique_ptr<Protocol> protocol_;
  EventGroupHandle_t event_group_ = nullptr;
  esp_timer_handle_t clock_timer_handle_ = nullptr;
  volatile DeviceState device_state_ = kDeviceStateUnknown;
  ListeningMode listening_mode_ = kListeningModeAutoStop;
  AecMode aec_mode_ = kAecOff;
  std::string last_error_message_;
  AudioService audio_service_;

  std::string heartrate_info_;
  ActuatorStatus last_actuator_status_;

  // Sensor data reporting task
  bool sensor_report_enabled_ = false;
  uint32_t sensor_report_interval_ms_ = 60000; // Default 60 seconds
  TaskHandle_t sensor_report_task_handle_ = nullptr;

  // Thêm member cho RecurringSchedule và StorageManager
//   RecurringSchedule scheduler;
//   StorageManager product_storage_;


  bool has_server_time_ = false;
  bool aborted_ = false;
  int clock_ticks_ = 0;
  TaskHandle_t check_new_version_task_handle_ = nullptr;
  TaskHandle_t main_event_loop_task_handle_ = nullptr;

  void OnWakeWordDetected();
  void CheckNewVersion(Ota &ota);
  void CheckAssetsVersion();
  void ShowActivationCode(const std::string &code, const std::string &message);
  void SetListeningMode(ListeningMode mode);

  // Thêm member cho Telegram bot
  std::unique_ptr<TelegramBot> telegram_bot_;
  bool telegram_initialized_ = false;

  // Thêm callback cho Telegram
  void OnTelegramMessage(const TelegramMessage &message);

  


  
};

class TaskPriorityReset {
public:
  TaskPriorityReset(BaseType_t priority) {
    original_priority_ = uxTaskPriorityGet(NULL);
    vTaskPrioritySet(NULL, priority);
  }
  ~TaskPriorityReset() { vTaskPrioritySet(NULL, original_priority_); }

private:
  BaseType_t original_priority_;
};

#endif // _APPLICATION_H_
