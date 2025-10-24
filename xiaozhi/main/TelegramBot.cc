#include "TelegramBot.h"
#include "telegram_manager.h" // Add this include
#include <cstdio>
#include <cstring>
#include <ctime>
#include "board.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_tls.h"

static const char *TAG = "TelegramBot";

// External certificate references
extern const char telegram_certificate_pem_start[] asm(
    "_binary_telegram_certificate_pem_start");
extern const char
    telegram_certificate_pem_end[] asm("_binary_telegram_certificate_pem_end");

// Constructor - Modified to load from TelegramManager
TelegramBot::TelegramBot(const std::string &bot_token,
                         const std::string &default_chat_id)
    : buffer_full_(false),           // Đặt đầu tiên (dòng 105 trong header)
      polling_paused_(false),        // Tiếp theo
      polling_task_handle_(nullptr), // Tiếp theo
      polling_active_(false),        // Dòng 110 trong header
      last_update_id_(0) {

  // Load configuration from TelegramManager if parameters are empty
  auto &telegram_manager = TelegramManager::GetInstance();
  auto config = telegram_manager.GetConfig();

  if (bot_token.empty() && !config.bot_token.empty()) {
    bot_token_ = config.bot_token;
    ESP_LOGI(TAG, "Loaded bot token from TelegramManager");
  } else {
    bot_token_ = bot_token;
  }

  if (default_chat_id.empty() && !config.chat_id.empty()) {
    default_chat_id_ = config.chat_id;
    ESP_LOGI(TAG, "Loaded chat ID from TelegramManager");
  } else {
    default_chat_id_ = default_chat_id;
  }

  // Check if we have valid configuration
  if (bot_token_.empty() || default_chat_id_.empty()) {
    ESP_LOGW(TAG, "TelegramBot created with incomplete configuration");
    ESP_LOGW(TAG, "Bot token: %s, Chat ID: %s",
             bot_token_.empty() ? "(empty)" : "***",
             default_chat_id_.empty() ? "(empty)" : default_chat_id_.c_str());
  } else {
    ESP_LOGI(TAG, "TelegramBot created with token: %s, chat: %s",
             bot_token_.substr(0, 10).c_str(), default_chat_id_.c_str());
  }

  // Set API URL
  if (!bot_token_.empty()) {
    api_url_ = "https://api.telegram.org/bot" + bot_token_;
  }

  ESP_LOGI(TAG, "Message buffer size: %d", MESSAGE_BUFFER_SIZE);
}

// Constructor overload - Create with TelegramManager config only
TelegramBot::TelegramBot() : TelegramBot("", "") {
  ESP_LOGI(TAG, "TelegramBot created using TelegramManager configuration");
}

// Destructor
TelegramBot::~TelegramBot() {
  stopPolling();
  clearMessageBuffer();
}

// Check if bot is properly configured
bool TelegramBot::isConfigured() const {
  return !bot_token_.empty() && !default_chat_id_.empty();
}

// Update configuration from TelegramManager
esp_err_t TelegramBot::updateConfiguration() {
  auto &telegram_manager = TelegramManager::GetInstance();
  auto config = telegram_manager.GetConfig();

  if (config.bot_token.empty() || config.chat_id.empty()) {
    ESP_LOGE(TAG, "TelegramManager has incomplete configuration");
    return ESP_ERR_INVALID_STATE;
  }

  // Stop polling if running
  bool was_polling = polling_active_;
  if (was_polling) {
    stopPolling();
  }

  // Update configuration
  bot_token_ = config.bot_token;
  default_chat_id_ = config.chat_id;
  api_url_ = "https://api.telegram.org/bot" + bot_token_;

  ESP_LOGI(TAG, "Configuration updated from TelegramManager");
  ESP_LOGI(TAG, "New bot token: %s, chat: %s", bot_token_.substr(0, 10).c_str(),
           default_chat_id_.c_str());

  // Restart polling if it was running
  if (was_polling) {
    return startPolling();
  }

  return ESP_OK;
}

// Get current configuration
std::pair<std::string, std::string> TelegramBot::getConfiguration() const {
  return std::make_pair(bot_token_, default_chat_id_);
}

// Initialize bot - Modified to check configuration
esp_err_t TelegramBot::init() {
  ESP_LOGI(TAG, "Initializing Telegram bot...");

  // Check if bot is configured
  if (!isConfigured()) {
    ESP_LOGE(TAG, "Bot is not properly configured. Please set bot_token and "
                  "chat_id via TelegramManager");
    return ESP_ERR_INVALID_STATE;
  }

  // Wait a bit before starting
  //   vTaskDelay(2000 / portTICK_PERIOD_MS);

  // Check bot info
  esp_err_t err = getBotInfo();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get bot info");
    return err;
  }

  // Delete webhook to ensure polling works
  err = deleteWebhook();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to delete webhook");
    return err;
  }

  // Send startup message
  // sendMessage("🤖 Bot restarted and ready! Buffer size: " +
  //             std::to_string(MESSAGE_BUFFER_SIZE));

  ESP_LOGI(TAG, "Telegram bot initialized successfully");
  return ESP_OK;
}

// Check if buffer has messages
bool TelegramBot::hasBufferedMessages() const {
  std::lock_guard<std::mutex> lock(buffer_mutex_);
  return !message_buffer_.empty();
}

// Get next message from buffer in JSON format
std::string TelegramBot::getNextMessageAsJson() {
  std::lock_guard<std::mutex> lock(buffer_mutex_);

  if (message_buffer_.empty()) {
    ESP_LOGD(TAG, "No messages in buffer to retrieve");
    return "{}"; // Return empty JSON
  }

  TelegramMessage message = message_buffer_.front();
  message_buffer_.pop();

  ESP_LOGI(TAG, "External code retrieved message from buffer. Remaining: %d/%d",
           (int)message_buffer_.size(), MESSAGE_BUFFER_SIZE);

  // Check if buffer was full and now has space - resume polling
  if (buffer_full_ && message_buffer_.size() < MESSAGE_BUFFER_SIZE) {
    buffer_full_ = false;
    polling_paused_ = false;
    ESP_LOGI(TAG,
             "Buffer has space after external retrieval, resuming polling. "
             "Current size: %d/%d",
             (int)message_buffer_.size(), MESSAGE_BUFFER_SIZE);
  }

  // Convert message to JSON string
  return messageToJson(message);
}

// Get next message from buffer - keep original method for compatibility
TelegramMessage TelegramBot::getNextMessage() {
  std::lock_guard<std::mutex> lock(buffer_mutex_);

  if (message_buffer_.empty()) {
    ESP_LOGD(TAG, "No messages in buffer to retrieve");
    return TelegramMessage{}; // Return empty message
  }

  TelegramMessage message = message_buffer_.front();
  message_buffer_.pop();

  ESP_LOGI(TAG, "External code retrieved message from buffer. Remaining: %d/%d",
           (int)message_buffer_.size(), MESSAGE_BUFFER_SIZE);

  // Check if buffer was full and now has space - resume polling
  if (buffer_full_ && message_buffer_.size() < MESSAGE_BUFFER_SIZE) {
    buffer_full_ = false;
    polling_paused_ = false;
    ESP_LOGI(TAG,
             "Buffer has space after external retrieval, resuming polling. "
             "Current size: %d/%d",
             (int)message_buffer_.size(), MESSAGE_BUFFER_SIZE);
  }

  return message;
}

// Get all buffered messages as JSON and clear buffer
std::string TelegramBot::getAllMessagesAsJson() {
  std::lock_guard<std::mutex> lock(buffer_mutex_);

  if (message_buffer_.empty()) {
    ESP_LOGD(TAG, "No messages in buffer to retrieve");
    return "{}"; // Return empty JSON object when no messages
  }

  cJSON *json_array = cJSON_CreateArray();
  int message_count = 0;

  // Process all messages and clear buffer
  while (!message_buffer_.empty()) {
    const TelegramMessage &message = message_buffer_.front();

    // Create JSON object for each message
    cJSON *msg_json = cJSON_CreateObject();
    cJSON_AddStringToObject(msg_json, "text", message.text.c_str());
    cJSON_AddStringToObject(msg_json, "chat_id", message.chat_id.c_str());
    cJSON_AddStringToObject(msg_json, "username", message.username.c_str());
    cJSON_AddStringToObject(msg_json, "timestamp", message.timestamp.c_str());
    cJSON_AddNumberToObject(msg_json, "message_id", (double)message.message_id);
    cJSON_AddNumberToObject(msg_json, "date", (double)message.date);

    // Add message object to array
    cJSON_AddItemToArray(json_array, msg_json);

    // Remove message from buffer
    message_buffer_.pop();
    message_count++;
  }

  // Reset buffer state flags after clearing
  buffer_full_ = false;
  polling_paused_ = false;

  char *json_string = cJSON_Print(json_array);
  std::string result;
  if (json_string) {
    result = json_string;
    free(json_string);
  } else {
    result = "{}";
  }

  cJSON_Delete(json_array);

  ESP_LOGI(TAG,
           "Retrieved and cleared %d messages from buffer, polling resumed",
           message_count);
  return result;
}

// Convert TelegramMessage to JSON string
std::string TelegramBot::messageToJson(const TelegramMessage &message) {
  cJSON *json = cJSON_CreateObject();

  cJSON_AddStringToObject(json, "text", message.text.c_str());
  cJSON_AddStringToObject(json, "chat_id", message.chat_id.c_str());
  cJSON_AddStringToObject(json, "username", message.username.c_str());
  cJSON_AddStringToObject(json, "timestamp", message.timestamp.c_str());
  cJSON_AddNumberToObject(json, "message_id", (double)message.message_id);
  cJSON_AddNumberToObject(json, "date", (double)message.date);

  char *json_string = cJSON_Print(json);
  std::string result;
  if (json_string) {
    result = json_string;
    free(json_string);
  } else {
    result = "{}";
  }

  cJSON_Delete(json);
  return result;
}

// Get buffered message count
int TelegramBot::getBufferedMessageCount() const {
  std::lock_guard<std::mutex> lock(buffer_mutex_);
  return message_buffer_.size();
}

// Clear message buffer
void TelegramBot::clearMessageBuffer() {
  std::lock_guard<std::mutex> lock(buffer_mutex_);
  while (!message_buffer_.empty()) {
    message_buffer_.pop();
  }
  buffer_full_ = false;
  polling_paused_ = false;
  ESP_LOGI(TAG, "Message buffer cleared by external code, polling resumed");
}

// Check if buffer is full
bool TelegramBot::isBufferFull() const {
  std::lock_guard<std::mutex> lock(buffer_mutex_);
  return message_buffer_.size() >= MESSAGE_BUFFER_SIZE;
}

// Add message to buffer and ALWAYS call callback
void TelegramBot::addMessageToBuffer(const TelegramMessage &message) {
  std::lock_guard<std::mutex> lock(buffer_mutex_);

  // This should only be called when we know there's space
  if (message_buffer_.size() >= MESSAGE_BUFFER_SIZE) {
    ESP_LOGE(TAG, "CRITICAL ERROR: Attempted to add message to full buffer! "
                  "This should never happen!");
    return;
  }

  // Add to buffer first
  message_buffer_.push(message);
  ESP_LOGI(TAG, "Added message to buffer from %s: %s. Buffer size: %d/%d",
           message.username.c_str(), message.text.c_str(),
           (int)message_buffer_.size(), MESSAGE_BUFFER_SIZE);

  // Check if buffer is now full after adding
  if (message_buffer_.size() >= MESSAGE_BUFFER_SIZE) {
    buffer_full_ = true;
    polling_paused_ = true;
    ESP_LOGW(TAG,
             "Buffer is now full (%d/%d)! Pausing polling until external code "
             "retrieves messages.",
             MESSAGE_BUFFER_SIZE, MESSAGE_BUFFER_SIZE);
  }
}

// Process new message: Add to buffer AND call callback
void TelegramBot::processNewMessage(const TelegramMessage &message) {
  // Add to buffer first (if there's space)
  if (!isBufferFull()) {
    addMessageToBuffer(message);
  } else {
    ESP_LOGW(TAG, "Buffer full, cannot store new message from %s",
             message.username.c_str());
    return; // Don't call callback if we can't store the message
  }

  // ALWAYS call callback to notify user about new message
  if (message_callback_) {
    try {
      ESP_LOGI(TAG, "Notifying external code about new message from %s",
               message.username.c_str());
      message_callback_(message);
    } catch (const std::exception &e) {
      ESP_LOGE(TAG, "Exception in message callback: %s", e.what());
    } catch (...) {
      ESP_LOGE(TAG, "Unknown exception in message callback");
    }
  } else {
    ESP_LOGW(TAG, "No callback set, message only stored in buffer");
  }
}

// HTTP event handler
esp_err_t TelegramBot::httpEventHandler(esp_http_client_event_t *evt) {
  static char *output_buffer = nullptr;
  static int output_len = 0;

  switch (evt->event_id) {
  case HTTP_EVENT_ERROR:
    ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
    break;

  case HTTP_EVENT_ON_CONNECTED:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
    break;

  case HTTP_EVENT_HEADER_SENT:
    ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
    break;

  case HTTP_EVENT_ON_HEADER:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key,
             evt->header_value);
    break;

  case HTTP_EVENT_ON_DATA: {
    ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

    if (!esp_http_client_is_chunked_response(evt->client)) {
      if (evt->user_data) {
        char *user_buffer = static_cast<char *>(evt->user_data);
        memcpy(user_buffer + output_len, evt->data, evt->data_len);
      } else {
        if (output_buffer == nullptr) {
          int content_length = esp_http_client_get_content_length(evt->client);
          if (content_length > 0) {
            output_buffer = static_cast<char *>(malloc(content_length));
          } else {
            output_buffer = static_cast<char *>(malloc(MAX_HTTP_OUTPUT_BUFFER));
          }
          output_len = 0;
          if (output_buffer == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
            return ESP_FAIL;
          }
        }
        memcpy(output_buffer + output_len, evt->data, evt->data_len);
      }
      output_len += evt->data_len;
    }
    break;
  }

  case HTTP_EVENT_ON_FINISH:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
    if (output_buffer != nullptr) {
      free(output_buffer);
      output_buffer = nullptr;
    }
    output_len = 0;
    break;

  case HTTP_EVENT_DISCONNECTED: {
    ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
    int mbedtls_err = 0;
    int esp_tls_code = 0;

    // Clean up output buffer if needed
    if (output_buffer != nullptr) {
      free(output_buffer);
      output_buffer = nullptr;
    }
    output_len = 0;

    if (mbedtls_err != 0 || esp_tls_code != 0) {
      ESP_LOGI(TAG, "Last esp error code: 0x%x", esp_tls_code);
      ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
    }
    break;
  }

  case HTTP_EVENT_REDIRECT:
    ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
    break;

  default:
    ESP_LOGD(TAG, "Unhandled HTTP event: %d", evt->event_id);
    break;
  }
  return ESP_OK;
}

// HTTP event handler for SMS
esp_err_t TelegramBot::httpEventHandlerSms(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
  case HTTP_EVENT_ON_DATA:
    if (!esp_http_client_is_chunked_response(evt->client)) {
      char *user_buffer = static_cast<char *>(evt->user_data);
      char *event_data = static_cast<char *>(evt->data);
      strncat(user_buffer, event_data, evt->data_len);
    }
    break;
  default:
    break;
  }
  return ESP_OK;
}

// // Make HTTP request - Modified to check configuration
// esp_err_t TelegramBot::makeHttpRequest(const std::string &endpoint,
//                                        const std::string &method,
//                                        const std::string &post_data,
//                                        std::string *response) {
//   if (!isConfigured()) {
//     ESP_LOGE(TAG, "Bot is not configured, cannot make HTTP request");
//     return ESP_ERR_INVALID_STATE;
//   }

//   char buffer[MAX_HTTP_OUTPUT_BUFFER];
//   memset(buffer, 0, sizeof(buffer));
//   std::string url = api_url_ + endpoint;

//   // Initialize config structure properly
//   esp_http_client_config_t config;
//   memset(&config, 0, sizeof(config));

//   config.url = url.c_str();
//   config.transport_type = HTTP_TRANSPORT_OVER_SSL;
//   config.event_handler = httpEventHandler;
//   config.user_data = buffer;
//   config.cert_pem = telegram_certificate_pem_start;
//   config.timeout_ms = 30000; // 30 second timeout

//   esp_http_client_handle_t client = esp_http_client_init(&config);
//   if (!client) {
//     ESP_LOGE(TAG, "Failed to initialize HTTP client");
//     return ESP_FAIL;
//   }

//   esp_err_t err;

//   if (method == "POST") {
//     err = esp_http_client_set_method(client, HTTP_METHOD_POST);
//     if (err == ESP_OK) {
//       err = esp_http_client_set_header(client, "Content-Type",
//                                        "application/json");
//     }
//     if (err == ESP_OK) {
//       err = esp_http_client_set_post_field(client, post_data.c_str(),
//                                            post_data.length());
//     }
//   } else {
//     err = esp_http_client_set_method(client, HTTP_METHOD_GET);
//   }

//   if (err != ESP_OK) {
//     ESP_LOGE(TAG, "Failed to configure HTTP client: %s", esp_err_to_name(err));
//     esp_http_client_cleanup(client);
//     return err;
//   }

//   err = esp_http_client_perform(client);

//   if (err == ESP_OK) {
//     int status_code = esp_http_client_get_status_code(client);
//     int64_t content_length = esp_http_client_get_content_length(client);

//     ESP_LOGD(TAG, "HTTP %s Status = %d, content_length = %lld", method.c_str(),
//              status_code, content_length);

//     if (response) {
//       *response = std::string(buffer);
//     }

//     if (status_code != 200) {
//       ESP_LOGW(TAG, "HTTP request failed with status %d, response: %s",
//                status_code, buffer);
//       err = ESP_FAIL;
//     }
//   } else {
//     ESP_LOGE(TAG, "HTTP %s request failed: %s", method.c_str(),
//              esp_err_to_name(err));
//   }

//   esp_http_client_close(client);
//   esp_http_client_cleanup(client);

//   return err;
// }


esp_err_t TelegramBot::makeHttpRequest(const std::string &endpoint,
                                       const std::string &method,
                                       const std::string &post_data,
                                       std::string *response) {
    if (!isConfigured()) {
        ESP_LOGE(TAG, "Bot not configured");
        return ESP_ERR_INVALID_STATE;
    }

    std::string url = api_url_ + endpoint;
    ESP_LOGI(TAG, "HTTP %s %s", method.c_str(), url.c_str());

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(30); // timeout 30s

    if (!http->Open(method, url)) {
        ESP_LOGE(TAG, "Failed to open connection to %s", url.c_str());
        return ESP_FAIL;
    }

    if (method == "POST" && !post_data.empty()) {
        http->SetHeader("Content-Type", "application/json");
        if (!http->Write(post_data.c_str(), post_data.size())) {
            ESP_LOGE(TAG, "Failed to send POST body");
            http->Close();
            return ESP_FAIL;
        }
    }

    int status = http->GetStatusCode();
    std::string body = http->ReadAll();
    http->Close();

    if (status / 100 != 2) {
        ESP_LOGW(TAG, "Telegram HTTP %s failed: %d\n%s",
                 method.c_str(), status, body.c_str());
        return ESP_FAIL;
    }

    if (response) *response = body;
    return ESP_OK;
}


// Send message to specific chat
esp_err_t TelegramBot::sendMessage(const std::string &chat_id,
                                   const std::string &message) {
  if (!isConfigured()) {
    ESP_LOGE(TAG, "Bot is not configured, cannot send message");
    return ESP_ERR_INVALID_STATE;
  }

  if (message.empty()) {
    ESP_LOGW(TAG, "Cannot send empty message");
    return ESP_ERR_INVALID_ARG;
  }

  std::string escaped_message = escapeJsonString(message);
  std::string post_data =
      "{\"chat_id\":\"" + chat_id + "\",\"text\":\"" + escaped_message + "\"}";

  ESP_LOGI(TAG, "Sending message to chat %s: %s", chat_id.c_str(),
           message.c_str());

  return makeHttpRequest("/sendMessage", "POST", post_data);
}

// Send message to default chat
esp_err_t TelegramBot::sendMessage(const std::string &message) {
  return sendMessage(default_chat_id_, message);
}

// Add command
void TelegramBot::addCommand(const std::string &command,
                             const std::string &description) {
  if (commands_.size() >= MAX_COMMANDS) {
    ESP_LOGW(TAG, "Command limit (%d) reached!", MAX_COMMANDS);
    return;
  }

  if (command.empty() || description.empty()) {
    ESP_LOGW(TAG, "Command and description cannot be empty");
    return;
  }

  commands_.emplace_back(command, description);
  ESP_LOGI(TAG, "Added command: /%s -> %s", command.c_str(),
           description.c_str());
}

// Upload commands
esp_err_t TelegramBot::uploadCommands() {
  if (!isConfigured()) {
    ESP_LOGE(TAG, "Bot is not configured, cannot upload commands");
    return ESP_ERR_INVALID_STATE;
  }

  if (commands_.empty()) {
    ESP_LOGW(TAG, "No commands to upload");
    return ESP_OK;
  }

  std::string post_data = "{\"commands\":[";

  for (size_t i = 0; i < commands_.size(); ++i) {
    if (i > 0) {
      post_data += ",";
    }
    post_data += "{\"command\":\"" + commands_[i].command +
                 "\",\"description\":\"" +
                 escapeJsonString(commands_[i].description) + "\"}";
  }

  post_data += "]}";

  ESP_LOGI(TAG, "Uploading %zu commands", commands_.size());

  return makeHttpRequest("/setMyCommands", "POST", post_data);
}

// Set message callback
void TelegramBot::setMessageCallback(MessageCallback callback) {
  message_callback_ = callback;
  ESP_LOGI(TAG, "Message callback set");
}

// Get bot info
esp_err_t TelegramBot::getBotInfo() {
  std::string response;
  esp_err_t err = makeHttpRequest("/getMe", "GET", "", &response);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Bot info: %s", response.c_str());
  }

  return err;
}

// Delete webhook
esp_err_t TelegramBot::deleteWebhook() {
  ESP_LOGI(TAG, "Deleting webhook...");
  return makeHttpRequest("/deleteWebhook", "GET");
}

// Start polling - Modified to check configuration
esp_err_t TelegramBot::startPolling() {
  if (!isConfigured()) {
    ESP_LOGE(TAG, "Bot is not configured, cannot start polling");
    return ESP_ERR_INVALID_STATE;
  }

  if (polling_active_) {
    ESP_LOGW(TAG, "Polling already active");
    return ESP_OK;
  }

  polling_active_ = true;

  BaseType_t result = xTaskCreate(pollingTask, "telegram_polling", 8192, this,
                                  5, &polling_task_handle_);

  if (result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create polling task");
    polling_active_ = false;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Message polling started");
  return ESP_OK;
}

// Stop polling
void TelegramBot::stopPolling() {
  if (!polling_active_) {
    return;
  }

  polling_active_ = false;

  if (polling_task_handle_) {
    vTaskDelete(polling_task_handle_);
    polling_task_handle_ = nullptr;
  }

  ESP_LOGI(TAG, "Message polling stopped");
}

// Polling task
void TelegramBot::pollingTask(void *pvParameter) {
  TelegramBot *bot = static_cast<TelegramBot *>(pvParameter);
  if (bot) {
    bot->runPolling();
  }
}

// FIXED: Run polling loop - Store messages AND call callback
void TelegramBot::runPolling() {
  ESP_LOGI(TAG, "Starting message polling loop - STORE + NOTIFY mode");

  while (polling_active_) {
    // CRITICAL: Only poll for new messages if buffer has space
    if (!polling_paused_ && !isBufferFull()) {
      std::vector<TelegramMessage> messages;

      if (getUpdates(messages) == ESP_OK && !messages.empty()) {
        ESP_LOGI(TAG, "Received %d new messages for processing",
                 (int)messages.size());

        for (const auto &message : messages) {
          ESP_LOGI(TAG, "Processing new message from %s: %s",
                   message.username.c_str(), message.text.c_str());

          // Check space BEFORE processing each message
          if (!isBufferFull()) {
            // This will store AND notify
            processNewMessage(message);
          } else {
            // Buffer became full during processing - stop here
            ESP_LOGW(TAG,
                     "Buffer became full while processing batch, stopped at "
                     "message %d",
                     (int)(&message - &messages[0]) + 1);
            break;
          }
        }
      }
    } else if (polling_paused_ || isBufferFull()) {
      ESP_LOGD(TAG,
               "Polling paused - buffer full (%d/%d). Waiting for external "
               "code to retrieve messages.",
               getBufferedMessageCount(), MESSAGE_BUFFER_SIZE);
    }

    // Adjust delay: check buffer more frequently when full
    int delay = (polling_paused_ || isBufferFull()) ? (GET_NEW_MSG_TIME / 4)
                                                    : GET_NEW_MSG_TIME;
    vTaskDelay(delay / portTICK_PERIOD_MS);
  }

  ESP_LOGI(TAG, "Polling loop ended");
  vTaskDelete(nullptr);
}

// Get updates
esp_err_t TelegramBot::getUpdates(std::vector<TelegramMessage> &messages) {
  std::string endpoint = "/getUpdates?timeout=10";
  if (last_update_id_ > 0) {
    endpoint += "&offset=" + std::to_string(last_update_id_ + 1);
  }

  std::string response;
  esp_err_t err = makeHttpRequest(endpoint, "GET", "", &response);

  if (err != ESP_OK) {
    ESP_LOGD(TAG, "Failed to get updates: %s", esp_err_to_name(err));
    return err;
  }

  // Parse JSON response
  cJSON *json = cJSON_Parse(response.c_str());
  if (!json) {
    ESP_LOGE(TAG, "Failed to parse JSON response: %s", response.c_str());
    return ESP_FAIL;
  }

  cJSON *ok = cJSON_GetObjectItem(json, "ok");
  if (!cJSON_IsBool(ok) || !cJSON_IsTrue(ok)) {
    cJSON *description = cJSON_GetObjectItem(json, "description");
    if (cJSON_IsString(description)) {
      ESP_LOGE(TAG, "API returned error: %s", description->valuestring);
    } else {
      ESP_LOGE(TAG, "API returned error (no description)");
    }
    cJSON_Delete(json);
    return ESP_FAIL;
  }

  cJSON *result = cJSON_GetObjectItem(json, "result");
  if (!cJSON_IsArray(result)) {
    cJSON_Delete(json);
    return ESP_OK; // No updates
  }

  int array_size = cJSON_GetArraySize(result);
  messages.reserve(array_size);

  for (int i = 0; i < array_size; i++) {
    cJSON *update = cJSON_GetArrayItem(result, i);
    if (!cJSON_IsObject(update)) {
      continue;
    }

    // Get update_id
    cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
    if (cJSON_IsNumber(update_id)) {
      long long current_update_id =
          static_cast<long long>(update_id->valuedouble);
      if (current_update_id > last_update_id_) {
        last_update_id_ = current_update_id;
      }
    }

    // Get message
    cJSON *message = cJSON_GetObjectItem(update, "message");
    if (!cJSON_IsObject(message)) {
      continue;
    }

    TelegramMessage tg_message;

    // Parse message text
    cJSON *text = cJSON_GetObjectItem(message, "text");
    if (cJSON_IsString(text) && text->valuestring) {
      tg_message.text = text->valuestring;
    }

    // Parse message_id
    cJSON *message_id = cJSON_GetObjectItem(message, "message_id");
    if (cJSON_IsNumber(message_id)) {
      tg_message.message_id = static_cast<long long>(message_id->valuedouble);
    }

    // Parse date
    cJSON *date = cJSON_GetObjectItem(message, "date");
    if (cJSON_IsNumber(date)) {
      tg_message.date = static_cast<long long>(date->valuedouble);
      time_t t = static_cast<time_t>(date->valuedouble);
      struct tm ts;
      localtime_r(&t, &ts);
      char time_str[64];
      strftime(time_str, sizeof(time_str), "%H:%M:%S %d-%m-%Y", &ts);
      tg_message.timestamp = time_str;
    }

    // Parse chat info
    cJSON *chat = cJSON_GetObjectItem(message, "chat");
    if (cJSON_IsObject(chat)) {
      cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
      if (cJSON_IsNumber(chat_id)) {
        tg_message.chat_id =
            std::to_string(static_cast<long long>(chat_id->valuedouble));
      }

      cJSON *username = cJSON_GetObjectItem(chat, "username");
      if (cJSON_IsString(username) && username->valuestring) {
        tg_message.username = username->valuestring;
      }

      // If no username, try first_name
      if (tg_message.username.empty()) {
        cJSON *first_name = cJSON_GetObjectItem(chat, "first_name");
        if (cJSON_IsString(first_name) && first_name->valuestring) {
          tg_message.username = first_name->valuestring;
        }
      }
    }

    // Only add messages with text content
    if (!tg_message.text.empty()) {
      messages.push_back(tg_message);
    }
  }

  cJSON_Delete(json);
  return ESP_OK;
}

// Escape JSON string
std::string TelegramBot::escapeJsonString(const std::string &str) {
  std::string escaped;
  escaped.reserve(str.length() + str.length() / 4); // Reserve some extra space

  for (char c : str) {
    switch (c) {
    case '"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\b':
      escaped += "\\b";
      break;
    case '\f':
      escaped += "\\f";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 32) {
        // Control characters
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
        escaped += buf;
      } else {
        escaped += c;
      }
      break;
    }
  }

  return escaped;
}