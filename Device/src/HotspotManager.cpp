#include "HotspotManager.h"

// --- CẬP NHẬT HTML FORM MỚI ---
const char HotspotManager::index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8"> <meta name="viewport" content="width=device-width, initial-scale=1.0"> <title>Cấu hình Hệ thống</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%); min-height: 100vh; display: flex; justify-content: center; align-items: center; padding: 20px; color: #333; }
        .container { background: rgba(255, 255, 255, 0.95); padding: 30px; border-radius: 15px; box-shadow: 0 10px 30px rgba(0,0,0,0.3); width: 100%; max-width: 450px; }
        h2 { text-align: center; color: #1e3c72; margin-bottom: 20px; border-bottom: 2px solid #eee; padding-bottom: 10px; }
        .group-title { font-size: 14px; color: #666; margin-top: 15px; margin-bottom: 5px; text-transform: uppercase; font-weight: bold; border-left: 3px solid #1e3c72; padding-left: 8px; }
        .form-group { margin-bottom: 12px; }
        label { display: block; font-weight: 600; margin-bottom: 5px; font-size: 13px; }
        input { width: 100%; padding: 10px; border: 1px solid #ccc; border-radius: 6px; font-size: 14px; transition: 0.3s; }
        input:focus { border-color: #1e3c72; outline: none; box-shadow: 0 0 5px rgba(30,60,114,0.3); }
        button { width: 100%; padding: 12px; background: #1e3c72; color: white; border: none; border-radius: 6px; font-size: 16px; font-weight: bold; cursor: pointer; margin-top: 20px; transition: 0.3s; }
        button:hover { background: #2a5298; }
        .message { margin-top: 15px; padding: 10px; border-radius: 6px; text-align: center; display: none; font-size: 14px; }
        .success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
    </style>
</head>
<body>
    <div class="container">
        <h2>📡 Thiết lập Thiết bị</h2>
        <form id="configForm">
            
            <div class="group-title">Kết nối WiFi</div>
            <div class="form-group"><input type="text" id="wifi_ssid" placeholder="Tên WiFi (SSID)" required></div>
            <div class="form-group"><input type="password" id="wifi_pass" placeholder="Mật khẩu WiFi"></div>

            <div class="group-title">Cấu hình MQTT Broker</div>
            <div class="form-group"><input type="text" id="mqtt_server" placeholder="Địa chỉ IP Broker (VD: 192.168.1.10)" required></div>
            <div class="form-group"><input type="number" id="mqtt_port" placeholder="Port (Mặc định: 1883)" value="1883" required></div>
            <div class="form-group"><input type="text" id="mqtt_user" placeholder="MQTT Username"></div>
            <div class="form-group"><input type="password" id="mqtt_pass" placeholder="MQTT Password"></div>

            <div class="group-title">Bảo mật (Key Exchange)</div>
            <div class="form-group"><input type="text" id="key_url" placeholder="URL lấy Key (VD: http://192.168.1.10:8000/exchange)" required></div>

            <button type="submit">Lưu Cấu Hình</button>
        </form>
        <div id="message" class="message"></div>
    </div>
    <script>
        document.getElementById('configForm').addEventListener('submit', function(e) {
            e.preventDefault();
            const btn = document.querySelector('button');
            btn.textContent = "Đang lưu..."; btn.disabled = true;

            const data = {
                wifi_ssid: document.getElementById('wifi_ssid').value,
                wifi_pass: document.getElementById('wifi_pass').value,
                mqtt_server: document.getElementById('mqtt_server').value,
                mqtt_port: parseInt(document.getElementById('mqtt_port').value),
                mqtt_user: document.getElementById('mqtt_user').value,
                mqtt_pass: document.getElementById('mqtt_pass').value,
                key_url: document.getElementById('key_url').value
            };

            fetch('/config', {
                method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(data)
            })
            .then(r => r.json())
            .then(d => {
                const msg = document.getElementById('message');
                msg.textContent = '✅ Đã lưu thành công! Thiết bị sẽ khởi động lại.';
                msg.className = 'message success'; msg.style.display = 'block';
            })
            .catch(err => {
                btn.textContent = "Lưu Cấu Hình"; btn.disabled = false;
                alert("Lỗi khi gửi dữ liệu!");
            });
        });
    </script>
</body>
</html>
)rawliteral";

HotspotManager::HotspotManager(const char* apSSID, const char* apPass) 
    : _server(80), _apSSID(apSSID), _apPass(apPass) {
    _dataReceived = false;
}

void HotspotManager::begin() {
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_apSSID, _apPass);
    Serial.println("\n[Hotspot] AP IP: " + WiFi.softAPIP().toString());

    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request){
        request->send(200, "text/html", index_html);
    });

    _server.on("/config", HTTP_POST, 
        [](AsyncWebServerRequest *request){ request->send(200, "application/json", "{\"status\":\"ok\"}"); },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            this->handleConfigData(request, data, len, index, total);
        }
    );
    
    _server.onNotFound([](AsyncWebServerRequest *request){ request->send(404); });
    _server.begin();
}

void HotspotManager::handleConfigData(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    DynamicJsonDocument doc(1024); // Tăng size buffer vì JSON giờ to hơn
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) { Serial.println("[Hotspot] JSON Error"); return; }

    // --- ĐỌC DỮ LIỆU TỪ JSON VÀO STRUCT ---
    _receivedData.wifi_ssid    = doc["wifi_ssid"].as<String>();
    _receivedData.wifi_pass    = doc["wifi_pass"].as<String>();
    _receivedData.mqtt_server  = doc["mqtt_server"].as<String>();
    _receivedData.mqtt_port    = doc["mqtt_port"] | 1883; // Mặc định 1883 nếu lỗi
    _receivedData.mqtt_user    = doc["mqtt_user"].as<String>();
    _receivedData.mqtt_pass    = doc["mqtt_pass"].as<String>();
    _receivedData.key_exchange_url = doc["key_url"].as<String>();

    Serial.println("[Hotspot] New Config Received:");
    Serial.println(" - WiFi: " + _receivedData.wifi_ssid);
    Serial.println(" - MQTT: " + _receivedData.mqtt_server);
    Serial.println(" - Key URL: " + _receivedData.key_exchange_url);

    _dataReceived = true;
}

void HotspotManager::stop() {
    _server.end();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
}

bool HotspotManager::isDataReceived() { return _dataReceived; }

ConfigData HotspotManager::getConfigData() {
    _dataReceived = false;
    return _receivedData;
}