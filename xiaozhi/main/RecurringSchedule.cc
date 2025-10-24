#include "RecurringSchedule.h"
#include "esp_log.h"
#include <algorithm>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <cstring>
#include <inttypes.h>

static const char *TAG = "RecurringSchedule";

RecurringSchedule::RecurringSchedule()
    : callback(nullptr), filePath(""), initialized(false), jsonDirty(true) {}

RecurringSchedule::~RecurringSchedule() {
  for (auto &pair : schedules) {
    if (pair.second->timer) {
      esp_timer_stop(pair.second->timer);
      esp_timer_delete(pair.second->timer);
    }
    delete pair.second;
  }
  schedules.clear();
}

bool RecurringSchedule::begin(const char *storagePath) {
  if (initialized) {
    ESP_LOGW(TAG, "Already initialized");
    return true;
  }

  this->filePath = storagePath;
  initialized = true;

  if (loadFromFile()) {
    ESP_LOGI(TAG, "Loaded schedules from file");
  } else {
    ESP_LOGI(TAG, "No schedules found in file or load failed");
  }

  return true;
}

void RecurringSchedule::setCallback(ScheduleCallback cb) {
  callback = cb;
  ESP_LOGI(TAG, "Callback set");
}

size_t RecurringSchedule::getCount() const { return schedules.size(); }

int64_t RecurringSchedule::getCurrentUnixTime() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec;
}

void RecurringSchedule::getCurrentTime(int &year, int &month, int &day,
                                      int &hour, int &min, int &sec,
                                      int &wday) {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  year = timeinfo.tm_year + 1900;
  month = timeinfo.tm_mon + 1;
  day = timeinfo.tm_mday;
  hour = timeinfo.tm_hour;
  min = timeinfo.tm_min;
  sec = timeinfo.tm_sec;
  wday = timeinfo.tm_wday;
}

const char *RecurringSchedule::getTypeName(ScheduleType type) {
  switch (type) {
  case ONCE:
    return "once";
  case INTERVAL:
    return "interval";
  case DAILY:
    return "daily";
  case WEEKLY:
    return "weekly";
  default:
    return "unknown";
  }
}

const char *RecurringSchedule::getWeekDayName(WeekDay day) {
  switch (day) {
  case SUNDAY:
    return "sunday";
  case MONDAY:
    return "monday";
  case TUESDAY:
    return "tuesday";
  case WEDNESDAY:
    return "wednesday";
  case THURSDAY:
    return "thursday";
  case FRIDAY:
    return "friday";
  case SATURDAY:
    return "saturday";
  default:
    return "unknown";
  }
}

void RecurringSchedule::timerCallback(void *arg) {
  ScheduleData *data = static_cast<ScheduleData *>(arg);
  ESP_LOGI(TAG, "Timer callback fired for schedule %d", data ? data->id : -1);
  if (data && data->scheduler) {
    data->scheduler->handleTimerTrigger(data);
  }
}

void RecurringSchedule::handleTimerTrigger(ScheduleData *data) {
  if (!data->enabled) {
    return;
  }

  ESP_LOGI(TAG, "Schedule %d triggered: %s", data->id, data->note.c_str());

  if (callback) {
    callback(data->id, data->note);
  }

  if (data->type == ONCE) {
    removeSchedule(data->id, true);
    return;
  }

  int64_t nextTrigger = calculateNextTriggerTime(data);
  if (nextTrigger > 0) {
    createAndStartTimer(data, nextTrigger * 1000000LL);
  }
}

int64_t RecurringSchedule::calculateNextTriggerTime(ScheduleData *data) {
  switch (data->type) {
  case INTERVAL:
    return data->intervalSeconds;

  case DAILY: {
    int year, month, day, hour, min, sec, wday;
    getCurrentTime(year, month, day, hour, min, sec, wday);
    
    int64_t minDelay = INT64_MAX;
    for (const auto &t : data->dailyTimes) {
      int targetSec = t.hour * 3600 + t.minute * 60;
      int currentSec = hour * 3600 + min * 60 + sec;
      
      int64_t delay;
      if (targetSec > currentSec) {
        delay = targetSec - currentSec;
      } else {
        delay = 86400 - currentSec + targetSec;
      }
      
      if (delay < minDelay) {
        minDelay = delay;
      }
    }
    return minDelay;
  }

  case WEEKLY: {
    int year, month, day, hour, min, sec, wday;
    getCurrentTime(year, month, day, hour, min, sec, wday);
    
    int64_t minDelay = INT64_MAX;
    for (const auto &t : data->weeklyTimes) {
      int targetDay = t.day;
      int targetSec = t.hour * 3600 + t.minute * 60;
      int currentSec = hour * 3600 + min * 60 + sec;
      
      int dayDiff = (targetDay - wday + 7) % 7;
      if (dayDiff == 0 && targetSec <= currentSec) {
        dayDiff = 7;
      }
      
      int64_t delay = dayDiff * 86400 + (targetSec - currentSec);
      if (delay < 0) {
        delay += 7 * 86400;
      }
      
      if (delay < minDelay) {
        minDelay = delay;
      }
    }
    return minDelay;
  }

  default:
    return -1;
  }
}

bool RecurringSchedule::createAndStartTimer(ScheduleData *data,
                                           int64_t delayMicroseconds) {
  if (data->timer) {
    esp_timer_stop(data->timer);
    esp_timer_delete(data->timer);
    data->timer = nullptr;
  }

  esp_timer_create_args_t timerArgs = {};
  timerArgs.callback = timerCallback;
  timerArgs.arg = data;
  timerArgs.dispatch_method = ESP_TIMER_TASK;
  timerArgs.name = "schedule_timer";

  esp_err_t err = esp_timer_create(&timerArgs, &data->timer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_timer_start_once(data->timer, delayMicroseconds);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(err));
    esp_timer_delete(data->timer);
    data->timer = nullptr;
    return false;
  }
  
  ESP_LOGI(TAG, "Timer started successfully, will fire in %" PRId64 " us (%.1f sec)", 
           delayMicroseconds, delayMicroseconds / 1000000.0);
  return true;
}

bool RecurringSchedule::addOnceAtUnix(int id, int64_t unixtime,
                                     const std::string &note, bool autoSave) {
  removeSchedule(id, false);

  ScheduleData *data = new ScheduleData();
  data->id = id;
  data->note = note;
  data->type = ONCE;
  data->scheduler = this;
  data->onceTime = unixtime;
  data->enabled = true;
  data->timer = nullptr;

  schedules[id] = data;

  int64_t now = getCurrentUnixTime();
  int64_t delay = unixtime - now;
  ESP_LOGI(TAG, "ONCE schedule: now=%" PRId64 ", target=%" PRId64 ", delay=%" PRId64 " sec", now, unixtime, delay);
  if (delay > 0) {
    createAndStartTimer(data, delay * 1000000LL);
    ESP_LOGI(TAG, "Timer created for schedule %d, will fire in %" PRId64 " seconds", id, delay);
  } else {
    ESP_LOGW(TAG, "Schedule %d is in the past, not creating timer", id);
  }

  jsonDirty = true;
  if (autoSave) {
    saveToFile();
  }

  ESP_LOGI(TAG, "Added ONCE schedule %d at %" PRId64, id, unixtime);
  return true;
}

bool RecurringSchedule::addOnceAfterDelay(int id, uint32_t delaySeconds,
                                          const std::string &note, bool autoSave) {
  int64_t now = getCurrentUnixTime();
  int64_t targetTime = now + delaySeconds;
  
  ESP_LOGI(TAG, "Adding schedule to run after %" PRIu32 " seconds (at %" PRId64 ")", 
           delaySeconds, targetTime);
  
  return addOnceAtUnix(id, targetTime, note, autoSave);
}

bool RecurringSchedule::addOnceAtTime(int id, uint8_t hour, uint8_t minute,
                                     const std::string &note, bool autoSave) {
  int year, mon, day, h, m, s, wday;
  getCurrentTime(year, mon, day, h, m, s, wday);

  int targetSec = hour * 3600 + minute * 60;
  int currentSec = h * 3600 + m * 60 + s;

  int64_t now = getCurrentUnixTime();
  int64_t unixtime;

  if (targetSec > currentSec) {
    unixtime = now + (targetSec - currentSec);
  } else {
    unixtime = now + (86400 - currentSec + targetSec);
  }

  return addOnceAtUnix(id, unixtime, note, autoSave);
}

bool RecurringSchedule::addOnceSchedule(int id, uint32_t secondsInDay,
                                       const std::string &note,
                                       bool autoSave) {
  uint8_t hour = secondsInDay / 3600;
  uint8_t minute = (secondsInDay % 3600) / 60;
  return addOnceAtTime(id, hour, minute, note, autoSave);
}

bool RecurringSchedule::addIntervalSchedule(int id, uint32_t intervalSeconds,
                                           const std::string &note,
                                           bool autoSave) {
  removeSchedule(id, false);

  ScheduleData *data = new ScheduleData();
  data->id = id;
  data->note = note;
  data->type = INTERVAL;
  data->scheduler = this;
  data->intervalSeconds = intervalSeconds;
  data->enabled = true;
  data->timer = nullptr;

  schedules[id] = data;

  createAndStartTimer(data, intervalSeconds * 1000000LL);

  jsonDirty = true;
  if (autoSave) {
    saveToFile();
  }

  ESP_LOGI(TAG, "Added INTERVAL schedule %d: %" PRIu32 " seconds", id, intervalSeconds);
  return true;
}

bool RecurringSchedule::addDailySchedule(int id,
                                        const std::vector<DailyTime> &times,
                                        const std::string &note,
                                        bool autoSave) {
  removeSchedule(id, false);

  ScheduleData *data = new ScheduleData();
  data->id = id;
  data->note = note;
  data->type = DAILY;
  data->scheduler = this;
  data->dailyTimes = times;
  data->enabled = true;
  data->timer = nullptr;

  schedules[id] = data;

  int64_t nextTrigger = calculateNextTriggerTime(data);
  if (nextTrigger > 0) {
    createAndStartTimer(data, nextTrigger * 1000000LL);
  }

  jsonDirty = true;
  if (autoSave) {
    saveToFile();
  }

  ESP_LOGI(TAG, "Added DAILY schedule %d", id);
  return true;
}

bool RecurringSchedule::addWeeklySchedule(int id,
                                         const std::vector<WeeklyTime> &times,
                                         const std::string &note,
                                         bool autoSave) {
  removeSchedule(id, false);

  ScheduleData *data = new ScheduleData();
  data->id = id;
  data->note = note;
  data->type = WEEKLY;
  data->scheduler = this;
  data->weeklyTimes = times;
  data->enabled = true;
  data->timer = nullptr;

  schedules[id] = data;

  int64_t nextTrigger = calculateNextTriggerTime(data);
  if (nextTrigger > 0) {
    createAndStartTimer(data, nextTrigger * 1000000LL);
  }

  jsonDirty = true;
  if (autoSave) {
    saveToFile();
  }

  ESP_LOGI(TAG, "Added WEEKLY schedule %d", id);
  return true;
}

bool RecurringSchedule::removeSchedule(int id, bool autoSave) {
  auto it = schedules.find(id);
  if (it == schedules.end()) {
    return false;
  }

  ScheduleData *data = it->second;
  if (data->timer) {
    esp_timer_stop(data->timer);
    esp_timer_delete(data->timer);
  }
  delete data;
  schedules.erase(it);

  jsonDirty = true;
  if (autoSave) {
    saveToFile();
  }

  ESP_LOGI(TAG, "Removed schedule %d", id);
  return true;
}

void RecurringSchedule::clearAll(bool autoSave) {
  for (auto &pair : schedules) {
    if (pair.second->timer) {
      esp_timer_stop(pair.second->timer);
      esp_timer_delete(pair.second->timer);
    }
    delete pair.second;
  }
  schedules.clear();

  jsonDirty = true;
  if (autoSave) {
    saveToFile();
  }

  ESP_LOGI(TAG, "Cleared all schedules");
}

bool RecurringSchedule::enableSchedule(int id, bool enable, bool autoSave) {
  auto it = schedules.find(id);
  if (it == schedules.end()) {
    return false;
  }

  ScheduleData *data = it->second;
  data->enabled = enable;

  if (enable) {
    int64_t nextTrigger = calculateNextTriggerTime(data);
    if (nextTrigger > 0) {
      createAndStartTimer(data, nextTrigger * 1000000LL);
    }
  } else {
    if (data->timer) {
      esp_timer_stop(data->timer);
    }
  }

  jsonDirty = true;
  if (autoSave) {
    saveToFile();
  }

  ESP_LOGI(TAG, "Schedule %d %s", id, enable ? "enabled" : "disabled");
  return true;
}

std::string RecurringSchedule::getSchedulesJSON() {
  if (jsonDirty) {
    rebuildJSONCache();
  }
  return cachedJSON;
}

void RecurringSchedule::rebuildJSONCache() {
  cJSON *root = cJSON_CreateArray();

  for (const auto &pair : schedules) {
    cJSON *item = scheduleToJSON(pair.second);
    cJSON_AddItemToArray(root, item);
  }

  char *jsonStr = cJSON_Print(root);
  cachedJSON = jsonStr ? jsonStr : "[]";

  cJSON_free(jsonStr);
  cJSON_Delete(root);

  jsonDirty = false;
}

cJSON *RecurringSchedule::scheduleToJSON(const ScheduleData *data) {
  cJSON *json = cJSON_CreateObject();

  cJSON_AddNumberToObject(json, "id", data->id);
  cJSON_AddStringToObject(json, "note", data->note.c_str());
  cJSON_AddStringToObject(json, "type", getTypeName(data->type));
  cJSON_AddBoolToObject(json, "enabled", data->enabled);

  switch (data->type) {
  case ONCE:
    cJSON_AddNumberToObject(json, "time", data->onceTime);
    break;

  case INTERVAL:
    cJSON_AddNumberToObject(json, "interval", data->intervalSeconds);
    break;

  case DAILY: {
    cJSON *times = cJSON_CreateArray();
    for (const auto &t : data->dailyTimes) {
      cJSON *timeObj = cJSON_CreateObject();
      cJSON_AddNumberToObject(timeObj, "hour", t.hour);
      cJSON_AddNumberToObject(timeObj, "minute", t.minute);
      cJSON_AddItemToArray(times, timeObj);
    }
    cJSON_AddItemToObject(json, "times", times);
    break;
  }

  case WEEKLY: {
    cJSON *times = cJSON_CreateArray();
    for (const auto &t : data->weeklyTimes) {
      cJSON *timeObj = cJSON_CreateObject();
      cJSON_AddStringToObject(timeObj, "day", getWeekDayName(t.day));
      cJSON_AddNumberToObject(timeObj, "hour", t.hour);
      cJSON_AddNumberToObject(timeObj, "minute", t.minute);
      cJSON_AddItemToArray(times, timeObj);
    }
    cJSON_AddItemToObject(json, "times", times);
    break;
  }
  }

  return json;
}

bool RecurringSchedule::scheduleFromJSON(cJSON *json) {
  cJSON *idItem = cJSON_GetObjectItem(json, "id");
  cJSON *noteItem = cJSON_GetObjectItem(json, "note");
  cJSON *typeItem = cJSON_GetObjectItem(json, "type");
  cJSON *enabledItem = cJSON_GetObjectItem(json, "enabled");

  if (!idItem || !noteItem || !typeItem) {
    return false;
  }

  int id = idItem->valueint;
  std::string note = noteItem->valuestring;
  std::string typeStr = typeItem->valuestring;
  bool enabled = enabledItem ? enabledItem->valueint : true;

  ScheduleType type;
  if (typeStr == "once") {
    type = ONCE;
  } else if (typeStr == "interval") {
    type = INTERVAL;
  } else if (typeStr == "daily") {
    type = DAILY;
  } else if (typeStr == "weekly") {
    type = WEEKLY;
  } else {
    return false;
  }

  ScheduleData *data = new ScheduleData();
  data->id = id;
  data->note = note;
  data->type = type;
  data->scheduler = this;
  data->enabled = enabled;
  data->timer = nullptr;

  switch (type) {
  case ONCE: {
    cJSON *timeItem = cJSON_GetObjectItem(json, "time");
    if (!timeItem) {
      delete data;
      return false;
    }
    data->onceTime = timeItem->valuedouble;
    break;
  }

  case INTERVAL: {
    cJSON *intervalItem = cJSON_GetObjectItem(json, "interval");
    if (!intervalItem) {
      delete data;
      return false;
    }
    data->intervalSeconds = intervalItem->valueint;
    break;
  }

  case DAILY: {
    cJSON *timesArray = cJSON_GetObjectItem(json, "times");
    if (timesArray) {
      cJSON *timeObj = nullptr;
      cJSON_ArrayForEach(timeObj, timesArray) {
        cJSON *hourItem = cJSON_GetObjectItem(timeObj, "hour");
        cJSON *minItem = cJSON_GetObjectItem(timeObj, "minute");
        if (hourItem && minItem) {
          data->dailyTimes.push_back(
              DailyTime(hourItem->valueint, minItem->valueint));
        }
      }
    }
    break;
  }

  case WEEKLY: {
    cJSON *timesArray = cJSON_GetObjectItem(json, "times");
    if (timesArray) {
      cJSON *timeObj = nullptr;
      cJSON_ArrayForEach(timeObj, timesArray) {
        cJSON *dayItem = cJSON_GetObjectItem(timeObj, "day");
        cJSON *hourItem = cJSON_GetObjectItem(timeObj, "hour");
        cJSON *minItem = cJSON_GetObjectItem(timeObj, "minute");

        if (dayItem && hourItem && minItem) {
          std::string dayStr = dayItem->valuestring;
          WeekDay day = MONDAY;
          if (dayStr == "sunday")
            day = SUNDAY;
          else if (dayStr == "monday")
            day = MONDAY;
          else if (dayStr == "tuesday")
            day = TUESDAY;
          else if (dayStr == "wednesday")
            day = WEDNESDAY;
          else if (dayStr == "thursday")
            day = THURSDAY;
          else if (dayStr == "friday")
            day = FRIDAY;
          else if (dayStr == "saturday")
            day = SATURDAY;

          data->weeklyTimes.push_back(
              WeeklyTime(day, hourItem->valueint, minItem->valueint));
        }
      }
    }
    break;
  }
  }

  schedules[id] = data;

  if (enabled) {
    int64_t nextTrigger = calculateNextTriggerTime(data);
    if (nextTrigger > 0) {
      createAndStartTimer(data, nextTrigger * 1000000LL);
    }
  }

  return true;
}

bool RecurringSchedule::saveToFile() {
  if (!initialized || filePath.empty()) {
    ESP_LOGE(TAG, "Not initialized or no file path");
    return false;
  }

  cJSON *root = cJSON_CreateArray();

  for (const auto &pair : schedules) {
    cJSON *item = scheduleToJSON(pair.second);
    cJSON_AddItemToArray(root, item);
  }

  char *jsonStr = cJSON_Print(root);
  cJSON_Delete(root);

  if (!jsonStr) {
    ESP_LOGE(TAG, "Failed to create JSON string");
    return false;
  }

  FILE *f = fopen(filePath.c_str(), "w");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open file for writing: %s", filePath.c_str());
    cJSON_free(jsonStr);
    return false;
  }

  size_t written = fwrite(jsonStr, 1, strlen(jsonStr), f);
  fclose(f);
  cJSON_free(jsonStr);

  if (written == 0) {
    ESP_LOGE(TAG, "Failed to write to file");
    return false;
  }

  ESP_LOGI(TAG, "Saved schedules to file: %s", filePath.c_str());
  return true;
}

bool RecurringSchedule::loadFromFile() {
  if (!initialized || filePath.empty()) {
    ESP_LOGE(TAG, "Not initialized or no file path");
    return false;
  }

  struct stat st;
  if (stat(filePath.c_str(), &st) != 0) {
    ESP_LOGW(TAG, "File does not exist: %s", filePath.c_str());
    return false;
  }

  FILE *f = fopen(filePath.c_str(), "r");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open file for reading: %s", filePath.c_str());
    return false;
  }

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (fsize <= 0) {
    fclose(f);
    ESP_LOGW(TAG, "File is empty");
    return false;
  }

  char *jsonStr = (char *)malloc(fsize + 1);
  size_t read = fread(jsonStr, 1, fsize, f);
  fclose(f);
  jsonStr[read] = '\0';

  if (read == 0) {
    free(jsonStr);
    ESP_LOGE(TAG, "Failed to read file");
    return false;
  }

  cJSON *root = cJSON_Parse(jsonStr);
  free(jsonStr);

  if (!root) {
    ESP_LOGE(TAG, "Failed to parse JSON");
    return false;
  }

  cJSON *item = nullptr;
  cJSON_ArrayForEach(item, root) { scheduleFromJSON(item); }

  cJSON_Delete(root);

  jsonDirty = true;
  ESP_LOGI(TAG, "Loaded data from file: %s", filePath.c_str());
  return true;
}

bool RecurringSchedule::clearFile() {
  if (!initialized || filePath.empty()) {
    return false;
  }

  if (remove(filePath.c_str()) == 0) {
    ESP_LOGI(TAG, "Deleted file: %s", filePath.c_str());
    return true;
  }

  ESP_LOGW(TAG, "Failed to delete file: %s", filePath.c_str());
  return false;
}
