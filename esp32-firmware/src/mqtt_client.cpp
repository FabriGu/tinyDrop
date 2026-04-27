#include "mqtt_client.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "secrets.h"
#include "ca_cert.h"

static WiFiClientSecure wifiClient;
static PubSubClient mqttClient(wifiClient);
static MqttCallback _user_cb = nullptr;
static char _dev_id[32] = "";
static unsigned long _last_reconnect = 0;

static void raw_callback(char* topic, byte* payload, unsigned int length) {
    if (!_user_cb) return;
    static char buf[512];
    unsigned int n = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = '\0';
    _user_cb(topic, buf);
}

static void subscribe_device_topics() {
    char t[64];
    snprintf(t, sizeof(t), "apigame/device/%s/task", _dev_id);
    mqttClient.subscribe(t);
    Serial.printf("  Sub: %s\n", t);

    snprintf(t, sizeof(t), "apigame/device/%s/result", _dev_id);
    mqttClient.subscribe(t);
    Serial.printf("  Sub: %s\n", t);
}

void mqtt_init(const char* device_id) {
    strncpy(_dev_id, device_id, sizeof(_dev_id) - 1);
    wifiClient.setCACert(ISRG_ROOT_X1);
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setBufferSize(512);
    mqttClient.setCallback(raw_callback);
}

void mqtt_connect() {
    // WiFi (blocking)
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("WiFi: connecting to %s", WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("WiFi FAILED — check credentials");
            return;
        }
    }

    // MQTT (blocking)
    Serial.printf("MQTT: connecting to %s:%d\n", MQTT_HOST, MQTT_PORT);
    for (int i = 0; i < 10 && !mqttClient.connected(); i++) {
        char cid[48];
        snprintf(cid, sizeof(cid), "apigame-%s-%lu", _dev_id, millis());
        if (mqttClient.connect(cid, MQTT_USER, MQTT_PASSWORD)) {
            Serial.println("MQTT connected!");
            subscribe_device_topics();
            return;
        }
        Serial.printf("  failed (rc=%d), retry...\n", mqttClient.state());
        delay(2000);
    }
    Serial.println("MQTT FAILED — check host/cert/credentials");
}

void mqtt_loop() {
    // Non-blocking reconnect
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        return;
    }
    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - _last_reconnect > 5000) {
            _last_reconnect = now;
            char cid[48];
            snprintf(cid, sizeof(cid), "apigame-%s-%lu", _dev_id, now);
            if (mqttClient.connect(cid, MQTT_USER, MQTT_PASSWORD)) {
                Serial.println("MQTT reconnected");
                subscribe_device_topics();
            }
        }
        return;
    }
    mqttClient.loop();
}

bool mqtt_publish(const char* topic, const char* payload) {
    return mqttClient.publish(topic, payload);
}

void mqtt_set_callback(MqttCallback cb) {
    _user_cb = cb;
}

bool mqtt_connected() {
    return mqttClient.connected();
}

const char* mqtt_device_id() {
    return _dev_id;
}
