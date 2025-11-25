#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h> // Thư viện lưu flash
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "ButtonHandler.h"
#include "HotspotManager.h"
#include "CryptoESP.h"
#include "MqttManager.h"

// ==========================================
// 1. CẤU HÌNH & BIẾN TOÀN CỤC
// ==========================================

// Objects
ButtonHandler btn(14, 3000, true); // GPIO 14, Long Press 3s
HotspotManager hotspot("ESP32_SECURE_DEVICE", "12345678");
CryptoESP crypto;
MqttManager *mqtt = NULL; // Dùng con trỏ để khởi tạo động sau khi load config
Preferences preferences;  // Lưu cấu hình vào Flash

// Trạng thái hệ thống
enum SystemState {
    STATE_NORMAL,
    STATE_CONFIG
};
volatile SystemState currentState = STATE_NORMAL;

// Cờ điều khiển
volatile bool triggerKeyExchange = false; // Cờ báo cần tạo lại khóa (Short Press)
volatile bool wifiConnected = false;

// Task Handles
TaskHandle_t taskNetHandle = NULL;
TaskHandle_t taskInputHandle = NULL;

// Biến lưu cấu hình (Load từ Flash)
struct {
    String wifi_ssid = "";
    String wifi_pass = "";
    String mqtt_server = "";
    int    mqtt_port = 1883;
    String mqtt_user = "";
    String mqtt_pass = "";
    String key_url = "";
} sysConfig;

// ==========================================
// 2. HÀM HỖ TRỢ (PREFERENCES & HTTP)
// ==========================================

void loadConfig() {
    preferences.begin("my-app", true); // Read-only mode
    sysConfig.wifi_ssid = preferences.getString("ssid", "");
    sysConfig.wifi_pass = preferences.getString("pass", "");
    sysConfig.mqtt_server = preferences.getString("mq_srv", "");
    sysConfig.mqtt_port = preferences.getInt("mq_port", 1883);
    sysConfig.mqtt_user = preferences.getString("mq_usr", "");
    sysConfig.mqtt_pass = preferences.getString("mq_pwd", "");
    sysConfig.key_url = preferences.getString("key_url", "");
    preferences.end();

    Serial.println(">>> CONFIG LOADED <<<");
    Serial.println("SSID: " + sysConfig.wifi_ssid);
    Serial.println("Key URL: " + sysConfig.key_url);
}

void saveConfig(ConfigData data) {
    preferences.begin("my-app", false); // Read-write mode
    preferences.putString("ssid", data.wifi_ssid);
    preferences.putString("pass", data.wifi_pass);
    preferences.putString("mq_srv", data.mqtt_server);
    preferences.putInt("mq_port", data.mqtt_port);
    preferences.putString("mq_usr", data.mqtt_user);
    preferences.putString("mq_pwd", data.mqtt_pass);
    preferences.putString("key_url", data.key_exchange_url);
    preferences.end();
    Serial.println(">>> CONFIG SAVED TO FLASH <<<");
}

// Hàm trao đổi khóa HTTP
bool performKeyExchange() {
    if (WiFi.status() != WL_CONNECTED || sysConfig.key_url == "") return false;

    HTTPClient http;
    http.begin(sysConfig.key_url);
    http.addHeader("Content-Type", "application/json");

    // Lấy Public Key hiện tại của ESP32
    DynamicJsonDocument doc(256);
    doc["device"] = "esp32";
    doc["publicKey"] = crypto.getPublicKeyHex();

    String requestBody;
    serializeJson(doc, requestBody);

    Serial.println("[HTTP] Sending Public Key...");
    int httpCode = http.POST(requestBody);
    
    bool success = false;
    if (httpCode == 200) {
        String payload = http.getString();
        DynamicJsonDocument res(512);
        deserializeJson(res, payload);
        const char *laptopHex = res["publicKey"];
        
        if (laptopHex && crypto.setPeerPublicKeyHex(laptopHex)) {
            Serial.println("[Crypto] Key Exchange SUCCESS!");
            success = true;
        }
    } else {
        Serial.printf("[HTTP] Error: %d\n", httpCode);
    }
    http.end();
    return success;
}

// ==========================================
// 3. TASK XỬ LÝ NÚT NHẤN (INPUT TASK)
// ==========================================
// Logic: Phân biệt Short/Long press bằng cách chờ Release
void inputTask(void *parameter) {
    btn.begin();
    static bool isHolding = false;
    static unsigned long pressStart = 0;

    for (;;) {
        btn.loop(); // Cập nhật trạng thái nút

        // 1. Phát hiện bắt đầu nhấn
        if (btn.isPressedRaw() && !isHolding) {
            isHolding = true;
            pressStart = millis();
        }

        // 2. Trong khi đang giữ nút
        if (isHolding) {
            unsigned long holdTime = millis() - pressStart;

            // CHECK LONG PRESS (> 3000ms)
            if (holdTime > 3000) {
                Serial.println("\n[Button] LONG PRESS DETECTED -> HOTSPOT MODE");
                currentState = STATE_CONFIG;
                
                // Reset trạng thái để không trigger lại liên tục
                isHolding = false; 
                // Chờ người dùng nhả nút ra hẳn mới xử lý tiếp
                while(btn.isPressedRaw()) { btn.loop(); vTaskDelay(10); } 
            }
            
            // CHECK RELEASE (SHORT PRESS)
            // Nếu nút đã nhả ra VÀ thời gian giữ < 3000ms
            else if (!btn.isPressedRaw()) {
                // Kiểm tra chống nhiễu (phải giữ > 50ms mới tính là nhấn)
                if (holdTime > 50) { 
                    Serial.printf("\n[Button] SHORT PRESS (%lums) -> RE-GEN KEY\n", holdTime);
                    triggerKeyExchange = true; // Bật cờ để NetworkTask xử lý
                }
                isHolding = false; // Reset
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ==========================================
// 4. TASK QUẢN LÝ MẠNG (NETWORK TASK)
// ==========================================
void networkTask(void *parameter) {
    // 1. Khởi tạo Crypto
    crypto.begin();
    
    // 2. Load Config
    loadConfig();

    // 3. Khởi tạo MQTT Manager (nếu có config)
    if (sysConfig.mqtt_server != "") {
        mqtt = new MqttManager(sysConfig.mqtt_server.c_str(), sysConfig.mqtt_port, 
                               sysConfig.mqtt_user.c_str(), sysConfig.mqtt_pass.c_str());
        mqtt->begin();
    }

    bool keyExchanged = false;
    uint32_t lastMsgTime = 0;
    const uint32_t MSG_INTERVAL = 30000; // 30 giây

    for (;;) {
        // ----------------------------------------
        // CASE 1: CHẾ ĐỘ NORMAL
        // ----------------------------------------
        if (currentState == STATE_NORMAL) {
            
            // A. Kiểm tra WiFi
            if (WiFi.status() != WL_CONNECTED) {
                if (sysConfig.wifi_ssid != "") {
                    Serial.printf("[WiFi] Connecting to %s...\n", sysConfig.wifi_ssid.c_str());
                    WiFi.begin(sysConfig.wifi_ssid.c_str(), sysConfig.wifi_pass.c_str());
                    
                    // Chờ kết nối (có timeout)
                    int retry = 0;
                    while (WiFi.status() != WL_CONNECTED && retry < 20) {
                        vTaskDelay(500 / portTICK_PERIOD_MS);
                        Serial.print(".");
                        retry++;
                        // Nếu trong lúc chờ mà bấm nút chuyển mode thì break ngay
                        if (currentState != STATE_NORMAL) break; 
                    }
                    Serial.println(WiFi.status() == WL_CONNECTED ? " CONNECTED" : " FAILED");
                } else {
                    Serial.println("[WiFi] No Config found! Please Long Press to Setup.");
                    vTaskDelay(2000 / portTICK_PERIOD_MS);
                }
            }

            // B. Xử lý Short Press (Tạo lại Key)
            if (triggerKeyExchange) {
                Serial.println("[System] Regenerating Keys...");
                crypto.generateNewKeys(); // Tạo cặp khóa mới
                keyExchanged = false;     // Reset trạng thái
                triggerKeyExchange = false; // Xóa cờ
            }

            // C. Trao đổi khóa (Nếu chưa có hoặc vừa bị reset)
            if (WiFi.status() == WL_CONNECTED && !keyExchanged) {
                if (performKeyExchange()) {
                    keyExchanged = true;
                    // Gửi ngay 1 gói tin sau khi exchange thành công
                    lastMsgTime = millis() - MSG_INTERVAL; 
                } else {
                    Serial.println("[Crypto] Exchange failed. Retrying in 5s...");
                    vTaskDelay(5000 / portTICK_PERIOD_MS);
                }
            }

            // D. MQTT Loop & Gửi tin định kỳ
            if (WiFi.status() == WL_CONNECTED && keyExchanged && mqtt) {
                mqtt->loop(); // Duy trì kết nối

                if (millis() - lastMsgTime > MSG_INTERVAL) {
                    lastMsgTime = millis();
                    
                    if (crypto.isReadyToSend()) {
                        String msg = "Data: " + String(millis());
                        // Mã hóa
                        String encrypted = crypto.createEncryptedPacket(msg.c_str());
                        // Gửi
                        mqtt->publish("esp32/data", encrypted.c_str());
                        Serial.println("[MQTT] Sent encrypted: " + msg);
                    }
                }
            }

            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        // ----------------------------------------
        // CASE 2: CHẾ ĐỘ CONFIG (HOTSPOT)
        // ----------------------------------------
        else if (currentState == STATE_CONFIG) {
            Serial.println("[System] Entering Hotspot Config Mode...");
            
            hotspot.begin(); // Bật AP & WebServer

            while (currentState == STATE_CONFIG) {
                // Kiểm tra xem Web có gửi dữ liệu về không
                if (hotspot.isDataReceived()) {
                    Serial.println("[System] New Config Received!");
                    
                    // Lấy dữ liệu và lưu vào Flash
                    ConfigData newData = hotspot.getConfigData();
                    saveConfig(newData);
					
                    Serial.println("[System] Restarting...");
                    delay(1000);
                    ESP.restart(); // Khởi động lại để áp dụng Wifi mới
                }
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
            
            hotspot.stop();
        }
    }
}

// ==========================================
// 5. SETUP & LOOP
// ==========================================
void setup() {
    Serial.begin(115200);

    // Tạo Task Input (Priority thấp)
    xTaskCreatePinnedToCore(inputTask, "InputTask", 4096, NULL, 1, &taskInputHandle, 1);
    
    // Tạo Task Network (Priority cao hơn, Stack lớn cho SSL/JSON)
    xTaskCreatePinnedToCore(networkTask, "NetTask", 8192, NULL, 2, &taskNetHandle, 0);
}

void loop() {
    vTaskDelete(NULL); // Xóa loop mặc định
}