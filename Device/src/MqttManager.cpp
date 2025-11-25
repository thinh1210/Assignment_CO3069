#include "MqttManager.h"

MqttManager::MqttManager(const char *broker, int port, const char *user, const char *pass)
    : _broker(broker), _port(port), _user(user), _pass(pass), _client(_espClient)
{
    _clientIdPrefix = "ESP32Client-";
}

void MqttManager::begin()
{
    _client.setServer(_broker, _port);
    // Callback sẽ được set sau khi user gọi hàm setCallback
}

void MqttManager::setCallback(void (*callback)(char *, uint8_t *, unsigned int))
{
    _callbackFunc = callback;
    _client.setCallback(_callbackFunc);
}

void MqttManager::loop()
{
    if (!_client.connected())
    {
        connect();
    }
    _client.loop();
}

bool MqttManager::connect()
{
    if (_client.connected())
        return true;

    Serial.print("Connecting to MQTT...");
    String clientId = String(_clientIdPrefix) + String(random(0xffff), HEX);

    if (_client.connect(clientId.c_str(), _user, _pass))
    {
        Serial.println("connected");
        // Resubscribe topics here if needed, or handle in main
        return true;
    }
    else
    {
        Serial.print("failed, rc=");
        Serial.print(_client.state());
        Serial.println(" try again in 3s");
        delay(3000); // Blocking delay, có thể tối ưu bằng millis()
        return false;
    }
}

bool MqttManager::connected()
{
    return _client.connected();
}

bool MqttManager::publish(const char *topic, const char *payload)
{
    if (_client.connected())
    {
        return _client.publish(topic, payload);
    }
    return false;
}

bool MqttManager::subscribe(const char *topic)
{
    if (_client.connected())
    {
        return _client.subscribe(topic);
    }
    return false;
}