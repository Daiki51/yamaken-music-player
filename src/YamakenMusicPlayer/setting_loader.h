#pragma once

#include <Arduino.h>

class WiFiSettings {
public:
  String ssid;
  String password;
};

void saveWiFiSettings(WiFiSettings *settings);
void loadWiFiSettings(WiFiSettings *settings);

class MQTTSettings {
public:
  String hostname;
  int port;
  String clientID;
  String username;
  String receive_topic;
  String sent_topic;
};

void saveMQTTSettings(MQTTSettings *settings);
void loadMQTTSettings(MQTTSettings *settings);
