#include <Arduino.h>
#include <Wire.h>

// ==================== I2C CONFIG ====================
#define I2C_SLAVE_ADDR 0x55
#define SDA_PIN 42
#define SCL_PIN 41
#define I2C_FREQ 100000
#define I2C_BUFFER_SIZE 200

// ==================== GLOBAL VARIABLES ====================
String serialBuffer = "";
unsigned long lastStatusRequest = 0;
unsigned long lastLedToggle = 0;
bool ledState = false;

#define LED_BUILTIN 2

// ==================== I2C FUNCTIONS ====================

/**
 * Send JSON command to slave via I2C
 */
bool sendI2CCommand(const String &json) {
  Wire.beginTransmission(I2C_SLAVE_ADDR);
  Wire.print(json);
  uint8_t error = Wire.endTransmission();

  if (error == 0) {
    Serial.printf("📤 TX (%d bytes): %s\n", json.length(), json.c_str());
    return true;
  } else {
    Serial.printf("❌ I2C Send Error: %d\n", error);
    return false;
  }
}

/**
 * Read response from slave via I2C
 */
String readI2CResponse(uint16_t delayMs = 500) {
  String response = "";
  
  // ✅ CHỜ LÂU HƠN để slave xử lý xong
  delay(delayMs);
  
  // ✅ THỬ ĐỌC NHIỀU LẦN NẾU THẤT BẠI
  for (int retry = 0; retry < 3; retry++) {
    // Request data
    int bytesAvailable = Wire.requestFrom((uint8_t)I2C_SLAVE_ADDR, 
                                          (uint8_t)I2C_BUFFER_SIZE, 
                                          (uint8_t)true);
    
    if (bytesAvailable <= 0) {
      Serial.printf("⚠️ Retry %d/3: No response\n", retry + 1);
      delay(100);
      continue;
    }
    
    Serial.printf("📡 Available: %d bytes\n", bytesAvailable);
    
    // Read all available bytes
    response = "";
    int readCount = 0;
    unsigned long startTime = millis();
    
    while (Wire.available() && readCount < I2C_BUFFER_SIZE && (millis() - startTime < 1000)) {
      char c = (char)Wire.read();
      response += c;
      readCount++;
      
      // Stop at end of JSON
      if (c == '}' && response.startsWith("{")) {
        break;
      }
    }
    
    // ✅ KIỂM TRA NẾU LÀ JSON HỢP LỆ
    if (response.length() > 0 && response.startsWith("{") && response.endsWith("}")) {
      Serial.printf("✅ RX (%d bytes): %s\n", response.length(), response.c_str());
      return response;
    }
    
    Serial.printf("⚠️ Invalid response: %s\n", response.c_str());
    delay(100);
  }
  
  Serial.println("❌ Failed after 3 retries");
  return "";
}

/**
 * Send command and wait for response
 */
String sendAndReceive(const String &json, uint16_t delayMs = 500) {
  // ✅ CLEAR BUFFER TRƯỚC KHI GỬI
  Wire.requestFrom((uint8_t)I2C_SLAVE_ADDR, (uint8_t)2, (uint8_t)500);
  while (Wire.available()) Wire.read();
  
  if (!sendI2CCommand(json)) {
    return "";
  }
  
  return readI2CResponse(delayMs);
}

/**
 * Check if slave is online
 */
bool checkSlaveConnection() {
  Wire.beginTransmission(I2C_SLAVE_ADDR);
  uint8_t error = Wire.endTransmission();
  return (error == 0);
}

// ==================== SERIAL INPUT ====================

/**
 * Process commands from Serial Monitor
 */
void processSerialInput() {
  while (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        Serial.printf("🎯 Command: %s\n", serialBuffer.c_str());
        
        // ✅ TĂNG DELAY LÊN 500ms
        String response = sendAndReceive(serialBuffer, 20);
        
        if (response.length() > 0) {
          Serial.println("✅ SUCCESS!");
        } else {
          Serial.println("❌ NO VALID RESPONSE");
        }
        
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }
}

// ==================== AUTO STATUS ====================

/**
 * Periodically request status from slave
 */
void periodicStatusRequest() {
  if (millis() - lastStatusRequest >= 5000) {
    lastStatusRequest = millis();

    Serial.println("\n🔄 ────── Auto Status Request ──────");
    String response = sendAndReceive("{\"t\":\"t\"}");
    Serial.println("────────────────────────────────────\n");
  }
}

// ==================== SETUP ====================

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Print banner
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   ESP32-S3 I2C Master Controller      ║");
  Serial.println("╚════════════════════════════════════════╝\n");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Initialize I2C
  Wire.setBufferSize(I2C_BUFFER_SIZE);
  Wire.begin(SDA_PIN, SCL_PIN, I2C_FREQ);
  Wire.setTimeout(1000);

  Serial.println("📡 I2C Configuration:");
  Serial.printf("   • SDA Pin: %d\n", SDA_PIN);
  Serial.printf("   • SCL Pin: %d\n", SCL_PIN);
  Serial.printf("   • Frequency: %d Hz\n", I2C_FREQ);
  Serial.printf("   • Slave Address: 0x%02X\n", I2C_SLAVE_ADDR);
  Serial.printf("   • Buffer Size: %d bytes\n", I2C_BUFFER_SIZE);

  // Wait for slave to boot
  Serial.println("\n⏳ Waiting for slave...");
  delay(2000);

  // Check connection
  if (checkSlaveConnection()) {
    Serial.println("✅ Slave detected!\n");
    digitalWrite(LED_BUILTIN, HIGH);

    // Initial status request
    Serial.println("🧪 Initial test:");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    sendAndReceive("{\"t\":\"t\"}");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
  } else {
    Serial.println("❌ Slave not found!");
    Serial.println("   Check wiring and addresses\n");
  }

  // Print command examples
  Serial.println("📝 Command Examples:");
  Serial.println("   Vehicle:");
  Serial.println("     {\"t\":\"v\",\"d\":1,\"s\":60,\"ms\":2000}  ← Forward");
  Serial.println("     {\"t\":\"v\",\"d\":2,\"s\":60,\"ms\":2000}  ← Backward");
  Serial.println("     {\"t\":\"v\",\"d\":0}                    ← Stop");
  Serial.println("   Storage:");
  Serial.println(
      "     {\"t\":\"s\",\"i\":0,\"a\":1}              ← Open slot 0");
  Serial.println(
      "     {\"t\":\"s\",\"i\":0,\"a\":0}              ← Close slot 0");
  Serial.println("   Status:");
  Serial.println("     {\"t\":\"t\"}                           ← Get status");
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  Ready! Type commands above ↑          ║");
  Serial.println("╚════════════════════════════════════════╝\n");
}

// ==================== LOOP ====================

void loop() {
  // Process serial commands
  processSerialInput();

  // Auto status request every 5 seconds
    periodicStatusRequest();

  //   // LED blink to show activity
  //   if (millis() - lastLedToggle > 500) {
  //     ledState = !ledState;
  //     digitalWrite(LED_BUILTIN, ledState);
  //     lastLedToggle = millis();
  //   }

  delay(10);
}
