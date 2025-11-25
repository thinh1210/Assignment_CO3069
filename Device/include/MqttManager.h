#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <WiFi.h>
#include <PubSubClient.h>

class MqttManager
{
private:
    const char *_broker;
    int _port;
    const char *_user;
    const char *_pass;
    const char *_clientIdPrefix;

    WiFiClient _espClient;
    PubSubClient _client;

    // Pointer đến hàm callback
    void (*_callbackFunc)(char *, uint8_t *, unsigned int);

public:
    MqttManager(const char *broker, int port, const char *user, const char *pass);

    void begin();
    void setCallback(void (*callback)(char *, uint8_t *, unsigned int));
    void loop();
    bool connect();
    bool connected();

    bool publish(const char *topic, const char *payload);
    bool subscribe(const char *topic);
};

#endif