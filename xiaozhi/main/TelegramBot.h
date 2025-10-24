#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <functional>
#include <utility>

#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Constants
#define MAX_HTTP_OUTPUT_BUFFER 4096
#define MESSAGE_BUFFER_SIZE 10
#define MAX_COMMANDS 20
#define GET_NEW_MSG_TIME 1000

// Telegram message structure
struct TelegramMessage {
    std::string text;
    std::string chat_id;
    std::string username;
    std::string timestamp;
    long long message_id = 0;
    long long date = 0;
    
    // Default constructor for empty message
    TelegramMessage() = default;
    
    // Check if message is empty
    bool isEmpty() const {
        return text.empty() && chat_id.empty();
    }
};

// Command structure
struct TelegramCommand {
    std::string command;
    std::string description;
    
    TelegramCommand(const std::string& cmd, const std::string& desc)
        : command(cmd), description(desc) {}
};

// Forward declaration for callback
class TelegramBot;
using MessageCallback = std::function<void(const TelegramMessage&)>;

class TelegramBot {
public:
    // Constructors
    explicit TelegramBot(const std::string& bot_token = "", 
                        const std::string& default_chat_id = "");
    TelegramBot(); // Uses TelegramManager configuration only
    
    // Destructor
    ~TelegramBot();

    // Configuration methods
    bool isConfigured() const;
    esp_err_t updateConfiguration();
    std::pair<std::string, std::string> getConfiguration() const;

    // Core methods
    esp_err_t init();
    esp_err_t startPolling();
    void stopPolling();

    // Message sending
    esp_err_t sendMessage(const std::string& message);
    esp_err_t sendMessage(const std::string& chat_id, const std::string& message);

    // Message receiving and buffer management
    bool hasBufferedMessages() const;
    TelegramMessage getNextMessage();
    std::string getNextMessageAsJson();
    std::string getAllMessagesAsJson();
    int getBufferedMessageCount() const;
    void clearMessageBuffer();
    bool isBufferFull() const;

    // Command management
    void addCommand(const std::string& command, const std::string& description);
    esp_err_t uploadCommands();

    // Callback management
    void setMessageCallback(MessageCallback callback);

    // Bot information
    esp_err_t getBotInfo();

private:
    // Configuration
    std::string bot_token_;
    std::string default_chat_id_;
    std::string api_url_;

    // Message buffer
    mutable std::mutex buffer_mutex_;
    std::queue<TelegramMessage> message_buffer_;
    bool buffer_full_;
    bool polling_paused_;

    // Polling
    TaskHandle_t polling_task_handle_;
    bool polling_active_;
    long long last_update_id_;

    // Commands
    std::vector<TelegramCommand> commands_;

    // Callback
    MessageCallback message_callback_;

    // Private methods
    static void pollingTask(void* pvParameter);
    void runPolling();
    esp_err_t getUpdates(std::vector<TelegramMessage>& messages);
    esp_err_t deleteWebhook();
    
    // Message processing
    void processNewMessage(const TelegramMessage& message);
    void addMessageToBuffer(const TelegramMessage& message);
    std::string messageToJson(const TelegramMessage& message);

    // HTTP handling
    esp_err_t makeHttpRequest(const std::string& endpoint,
                             const std::string& method,
                             const std::string& post_data = "",
                             std::string* response = nullptr);
    
    static esp_err_t httpEventHandler(esp_http_client_event_t* evt);
    static esp_err_t httpEventHandlerSms(esp_http_client_event_t* evt);

    // Utility
    std::string escapeJsonString(const std::string& str);

    // Disable copy constructor and assignment operator
    TelegramBot(const TelegramBot&) = delete;
    TelegramBot& operator=(const TelegramBot&) = delete;
};

#endif // TELEGRAM_BOT_H