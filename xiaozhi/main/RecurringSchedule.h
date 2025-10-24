#ifndef RECURRING_SCHEDULE_H
#define RECURRING_SCHEDULE_H

#include "cJSON.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

class RecurringSchedule {
public:
  static RecurringSchedule &GetInstance() {
    static RecurringSchedule instance;
    return instance;
  }

  enum ScheduleType { ONCE = 0, INTERVAL = 1, DAILY = 2, WEEKLY = 3 };

  enum WeekDay {
    SUNDAY = 0,
    MONDAY = 1,
    TUESDAY = 2,
    WEDNESDAY = 3,
    THURSDAY = 4,
    FRIDAY = 5,
    SATURDAY = 6
  };

  using ScheduleCallback = std::function<void(int id, const std::string &note)>;

  struct DailyTime {
    uint8_t hour;
    uint8_t minute;
    DailyTime(uint8_t h = 0, uint8_t m = 0) : hour(h), minute(m) {}
  };

  struct WeeklyTime {
    WeekDay day;
    uint8_t hour;
    uint8_t minute;
    WeeklyTime(WeekDay d = MONDAY, uint8_t h = 0, uint8_t m = 0)
        : day(d), hour(h), minute(m) {}
  };

  // Constructor & Destructor
  RecurringSchedule();
  ~RecurringSchedule();

  // Initialization
  bool begin(const char *filePath = "/storage/schedule.json");
  void setCallback(ScheduleCallback callback);

  // API Functions
  bool addOnceAtUnix(int id, int64_t unixtime, const std::string &note,
                     bool autoSave = true);
  bool addOnceAtTime(int id, uint8_t hour, uint8_t minute,
                     const std::string &note, bool autoSave = true);
  bool addOnceAfterDelay(int id, uint32_t delaySeconds, const std::string &note,
                         bool autoSave = true);
  bool addOnceSchedule(int id, uint32_t secondsInDay, const std::string &note,
                       bool autoSave = true);
  bool addIntervalSchedule(int id, uint32_t intervalSeconds,
                           const std::string &note, bool autoSave = true);
  bool addDailySchedule(int id, const std::vector<DailyTime> &times,
                        const std::string &note, bool autoSave = true);
  bool addWeeklySchedule(int id, const std::vector<WeeklyTime> &times,
                         const std::string &note, bool autoSave = true);
  bool removeSchedule(int id, bool autoSave = true);
  void clearAll(bool autoSave = true);
  bool enableSchedule(int id, bool enable, bool autoSave = true);

  // Get cached JSON
  std::string getSchedulesJSON();

  size_t getCount() const;
  bool saveToFile();
  bool loadFromFile();
  bool clearFile();

private:
  struct ScheduleData {
    int id;
    std::string note;
    ScheduleType type;
    RecurringSchedule *scheduler;
    union {
      int64_t onceTime;
      uint32_t intervalSeconds;
    };
    std::vector<DailyTime> dailyTimes;
    std::vector<WeeklyTime> weeklyTimes;
    bool enabled;
    esp_timer_handle_t timer;
  };

  // Internal data
  std::map<int, ScheduleData *> schedules;
  ScheduleCallback callback;
  std::string filePath;
  bool initialized;

  // JSON Cache
  std::string cachedJSON;
  bool jsonDirty;

  // Helper functions
  int64_t calculateNextTriggerTime(ScheduleData *data);
  bool createAndStartTimer(ScheduleData *data, int64_t delayMicroseconds);
  void rebuildJSONCache();
  int64_t getCurrentUnixTime();
  void getCurrentTime(int &year, int &month, int &day, int &hour, int &min,
                      int &sec, int &wday);
  cJSON *scheduleToJSON(const ScheduleData *data);
  bool scheduleFromJSON(cJSON *json);
  const char *getTypeName(ScheduleType type);
  const char *getWeekDayName(WeekDay day);
  void handleTimerTrigger(ScheduleData *data);

  // Timer callback
  static void timerCallback(void *arg);
};

#endif // RECURRING_SCHEDULE_H