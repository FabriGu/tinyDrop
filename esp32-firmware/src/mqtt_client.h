#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>

typedef void (*MqttCallback)(const char* topic, const char* payload);

void mqtt_init(const char* device_id);
void mqtt_connect();     // blocking — use in setup()
void mqtt_loop();        // non-blocking reconnect + loop — use in loop()
bool mqtt_publish(const char* topic, const char* payload);
void mqtt_set_callback(MqttCallback cb);
bool mqtt_connected();
const char* mqtt_device_id();

#endif
