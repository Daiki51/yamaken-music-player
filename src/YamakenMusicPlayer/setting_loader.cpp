#include <FS.h>
#include "setting_loader.h"

void loadWiFiSettings(WiFiSettings *settings) {
  File f = SPIFFS.open("/wifi_setting.txt", "r");
  settings->ssid = f.readStringUntil('\n');
  settings->ssid.trim();
  settings->password = f.readStringUntil('\n');
  settings->password.trim();
  f.close();
}

void saveWiFiSettings(WiFiSettings *settings) {
  File f = SPIFFS.open("/wifi_setting.txt", "w");
  f.println(settings->ssid);
  f.println(settings->password);
  f.close();
}

void loadMQTTSettings(MQTTSettings *settings) {
  File f = SPIFFS.open("/mqtt_setting.txt", "r");
  settings->hostname = f.readStringUntil('\n');
  settings->hostname.trim();
  String port = f.readStringUntil('\n');
  port.trim();
  settings->port = port.toInt();
  settings->clientID = f.readStringUntil('\n');
  settings->clientID.trim();
  settings->username = f.readStringUntil('\n');
  settings->username.trim();
  settings->receive_topic = f.readStringUntil('\n');
  settings->receive_topic.trim();
  settings->sent_topic = f.readStringUntil('\n');
  settings->sent_topic.trim();
  f.close();
}

void saveMQTTSettings(MQTTSettings *settings) {
  File f = SPIFFS.open("/mqtt_setting.txt", "w");
  f.println(settings->hostname);
  f.println(settings->port);
  f.println(settings->clientID);
  f.println(settings->username);
  f.println(settings->receive_topic);
  f.println(settings->sent_topic);
  f.close();
}
