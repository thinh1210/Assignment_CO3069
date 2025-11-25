#ifndef HOTSPOT_MANAGER_H
#define HOTSPOT_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>

// Cập nhật cấu trúc dữ liệu config
struct ConfigData {
    String wifi_ssid;
    String wifi_pass;
    String mqtt_server;
    int    mqtt_port;
    String mqtt_user;
    String mqtt_pass;
    String key_exchange_url;
};

class HotspotManager {
private:
    AsyncWebServer _server;
    bool _dataReceived;
    ConfigData _receivedData;
    const char* _apSSID;
    const char* _apPass;

    static const char index_html[];

    void handleConfigData(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

public:
    HotspotManager(const char* apSSID = "ESP32_SETUP", const char* apPass = "12345678");

    void begin();
    void stop();
    bool isDataReceived();
    ConfigData getConfigData();
};

#endif