#include <math.h>
#include <time.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Ticker.h>
#include <avdweb_Switch.h>
#include <jled.h>
#include <Lpf.h>
#include <ArduinoJson.h>
#include "src/Arduino-SerialCommand/SerialCommand.h"
#include "src/pubsubclient/src/PubSubClient.h"
#include "PlayerService.h"
#include "MusicScheduler.h"

//-------------------------------------
// 定数
//-------------------------------------
// ピン番号
const int8_t PLAYER_RX_PIN = 4;
const int8_t PLAYER_TX_PIN = 5;
const int PLAYING_LED_PIN = 12;
const int ONLINE_LED_PIN = 13;
const int PLAY_SW_PIN = 14;
const int VOLUME_PIN = A0;

// ボリュームセンサーのアナログ最大値
const int VOLUME_SENSOR_MAX = 1024;
const int VOLUME_SENSOR_MIN = 2;

// 日本標準時(+0900)
const int JST = 3600*9;

//-------------------------------------
// グローバル変数
//-------------------------------------

// DFPlayerとの通信を担当するクラス
PlayerService playerService(PLAYER_RX_PIN, PLAYER_TX_PIN);

// 定期的な再生スケジューリングを管理するクラス
MusicScheduler musicScheduler;

// ボタンとLED
Switch playButton(PLAY_SW_PIN);
JLed playingLed(PLAYING_LED_PIN);
JLed onlineLed(ONLINE_LED_PIN);

// 音量関係
int volume = -1;
LPF volumeFilter(0.2, false); // ボリュームのローパスフィルタ

// シリアル
SerialCommand sCmd; // シリアル通信でコマンドを処理するため

// 通信関係
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
bool isWifiConnecting = false;
bool isFirstConnect = true;

// 設定ストア(JSON形式)
StaticJsonDocument<1000> config;
bool isLoaded = false;

// タイマー割り込み用
//Ticker tickerUpdateSensorAndLed;
//Ticker tickerUpdatePlayer;
Ticker tickerCheckWiFi;

bool playButtonPushedFlag = false;
bool playerUpdateFlag = false;
bool wifiJustConnectedFlag = false;
bool isOnline = false;

// その他
int32_t loop_count = 0;

//-------------------------------------
// setup & loop
//-------------------------------------
void setup() {
  Serial.begin(115200);
  printBannerMessage();
  Serial.println("[INFO] Setup start");

  // ボリューム用のローパスフィルタを初期化
  volume = (int)round(30.0 * getRawVolume());
  volumeFilter.Reset(volume);

  // コマンドの登録
  sCmd.addCommand("HELP", helpCommandCallback);
  sCmd.addCommand("SET_WIFI", setWiFiCommandCallback);
  sCmd.addCommand("PLAY", playCommandCallback);
  sCmd.addCommand("STOP", stopCommandCallback);
  sCmd.addCommand("SLEEP", sleepCommandCallback);
  sCmd.setDefaultHandler(defaultCommandCallback);

  // 設定を読み込む
  SPIFFS.begin();
  loadConfig();

  // Wi-Fiに接続
  connectWiFi();
  configTime(JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");

  // プレイヤーを初期化
  playerService.begin();
  playerService.onPlay(onPlayCallback);
  playerService.onStop(onStopCallback);
  playerService.onError(onErrorCallback);

  // タイマーの登録
//  tickerUpdateSensorAndLed.attach_ms(20, updateSensorAndLed);
//  tickerUpdatePlayer.attach_ms(20, updatePlayer);

  // スケジューラーを初期化
  musicScheduler.onStart(onStartCallback);
  musicScheduler.onEnd(onEndCallback);
  musicScheduler.bind(&playerService);
  musicScheduler.loadTask();

  Serial.println("[INFO] Ready");
}

// Wi-Fiに接続
void connectWiFi() {
  // LEDの点滅(スロー)を開始
  onlineLed.Breathe(1000).Forever();
  WiFi.mode(WIFI_STA);
  if (String((const char*)config["wifi"]["configure_ip"]) == "static") {
    // 固定IPアドレスの設定
    IPAddress ip_address;
    IPAddress dns;
    IPAddress gateway;
    IPAddress netmask;
    ip_address.fromString((const char*)config["wifi"]["ip_address"]);
    dns.fromString((const char*)config["wifi"]["dns"]);
    gateway.fromString((const char*)config["wifi"]["gateway"]);
    netmask.fromString((const char*)config["wifi"]["netmask"]);
    WiFi.config(ip_address, dns, gateway, netmask);
  }
  WiFi.begin((const char*)config["wifi"]["ssid"], (const char*)config["wifi"]["password"]);
  tickerCheckWiFi.attach_ms(100, checkWiFi);
  isWifiConnecting = true;
}

void loop() {
  sCmd.readSerial();
  if (playButtonPushedFlag) {
    // 再生ボタンが押された時
    if (!playerService.isPlaying()) {
      playerService.setRepeat(true);
      playerService.setShuffle(true);
      playerService.playFolder(config["music"]["default_folder"]);
    } else {
      playerService.stop();
    }
    playButtonPushedFlag = false;
  }
  if (playerUpdateFlag) {
    // プレイヤーの状態を更新する時
    playerService.update();
    playerService.setVolume(volume);
    musicScheduler.update();
    playerUpdateFlag = false;
  }
  if (wifiJustConnectedFlag) {
    // Wi-Fiの接続に成功した時
    wifiJustConnectedFlag = false;
    isWifiConnecting = false;
    isOnline = true;
    if (isFirstConnect) {
      Serial.println("[INFO] Wi-Fi connected");
    } else {
      Serial.println("[INFO] Wi-Fi reconnected");
    }
    Serial.print("  IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Time: ");
    Serial.println(getTimeStr());
  }
  if (!isWifiConnecting && WiFi.status() != WL_CONNECTED) {
    // Wi-Fiが何らかの理由で切断された時
    Serial.println("[WARN] Wi-Fi disconnected");
    isOnline = false;
    // Wi-Fiに再接続
    connectWiFi();
  }
  if (isOnline && !mqttClient.connected()) {
    // Wi-Fiに接続したがMQTTにまだ接続していない時
    // もしくはMQTTが何らかの理由で切断された時
    if (connectMQTT()) {
      Serial.println("[INFO] MQTT connected");
      if (isFirstConnect) {
        notifyEvent("startApp");
      } else {
        notifyEvent("reconnect");
      }
      isFirstConnect = false;
    } else {
      Serial.print("[ERROR] MQTT connection failed: ");
      Serial.println(mqttClient.state());
    }
  }
  if (loop_count % 5 == 0) {
    updateSensorAndLed();
    updatePlayer();
  }
  if (loop_count % 75000 == 0) { // 5分毎に生存報告
    notifyEvent("heartBeats");
  }
  mqttClient.loop();
  loop_count++;
  delay(4);
}

//-------------------------------------
// タイマー割り込みのコールバック
//-------------------------------------
void updateSensorAndLed() {
  playButton.poll();
  playingLed.Update();
  onlineLed.Update();
  float rawVolume = getRawVolume();
  volume = (int)round(30.0 * volumeFilter.NextValue(rawVolume));
  if (playButton.pushed()) {
    playButtonPushedFlag = true;
  }
}

void updatePlayer() {
  playerUpdateFlag = true;
}

void checkWiFi() {
  // Wi-Fiに接続したかどうか確認
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  // 時刻が同期したかどうか確認
  time_t t = time(NULL);
  struct tm *tm;
  tm = localtime(&t);
  if (tm->tm_year + 1900 < 2019) {
    return;
  }
  tickerCheckWiFi.detach();
  onlineLed.On();
  musicScheduler.begin();
  wifiJustConnectedFlag = true;
}

//-------------------------------------
// プレイヤーのイベントコールバック
//-------------------------------------
void onPlayCallback(uint8_t folderNumber, uint8_t fileNumber) {
  Serial.print("[INFO] Play: ");
  Serial.print(folderNumber);
  Serial.print("-");
  Serial.println(fileNumber);
  playingLed.On();
  notifyPlay(folderNumber, fileNumber);
}

void onStopCallback() {
  Serial.println("[INFO] Stop");
  playingLed.Off();
  notifyEvent("stop");
}

void onErrorCallback(String message) {
  Serial.print("[ERROR] ");
  Serial.println(message);
  notifyError(message);
}

//-------------------------------------
// スケジューラーのイベントコールバック
//-------------------------------------
void onStartCallback() {
  Serial.println("[INFO] Start Task");
  notifyEvent("startTask");
}

void onEndCallback() {
  Serial.println("[INFO] End Task ");
  notifyEvent("endTask");
}

//-------------------------------------
// シリアルコマンド関連
//-------------------------------------
// 起動時のバナー表示
void printBannerMessage() {
  Serial.println(F(
    "\n"
    "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
    "■■   ■■                                          \n"
    " ■   ■                                             \n"
    "  ■ ■   Yamaken Music Player                                \n"
    "   ■     © 2019 Daiichi Institute of Technology Yamada's lab \n"
    "   ■                                         \n"
    "   ■                                         \n"
    "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    ));
  Serial.println(F("Commands are available for this player."));
  Serial.println(F("Please enter HELP to see how to use the command."));
  Serial.println(F("このプレイヤーはコマンドが利用可能です。"));
  Serial.println(F("コマンドの使用方法を調べるにはHELPと入力してください。"));
  Serial.println(F("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"));
}

// SET_WIFIコマンド
void setWiFiCommandCallback() {
  config["wifi"]["ssid"] = sCmd.next();
  config["wifi"]["password"] = sCmd.next();
  Serial.println("[LOG] Changed WiFi");
  Serial.print("  ssid: ");
  Serial.println((const char*)config["wifi"]["ssid"]);
  Serial.print("  Password: ");
  Serial.println((const char*)config["wifi"]["password"]);
  saveConfig();
}

// PLAYコマンド
void playCommandCallback() {
  char *folderNumberStr = sCmd.next();
  if (folderNumberStr == nullptr) {
    Serial.println(F("[ERROR] No arguments"));
    printCommandNavi();
    return;
  }
  char *fileNumberStr = sCmd.next();
  if (folderNumberStr == nullptr) {
    Serial.println(F("[ERROR] No arguments"));
    printCommandNavi();
    return;
  }
  int folderNumber = atoi(folderNumberStr);
  int fileNumber = atoi(fileNumberStr);
  playerService.playFile(folderNumber, fileNumber);
}

// STOPコマンド
void stopCommandCallback() {
  playerService.stop();
}

// SLEEPコマンド
void sleepCommandCallback() {
  char *secStr = sCmd.next();
  if (secStr == nullptr) {
    Serial.println(F("[ERROR] No arguments"));
    printCommandNavi();
    return;
  }
  int sec = atoi(secStr);
  ESP.deepSleep(sec * 1000 * 1000, WAKE_RF_DEFAULT);
  delay(1000);
}

// HELPコマンド
void helpCommandCallback() {
  Serial.println(F(
    "[INFO] Command List\n"
    "  HELP                          show this help message\n"
    "                                このヘルプを表示します\n"
    "  PLAY folderNum fileNum        play pecified music\n"
    "                                指定した音楽を再生します\n"
    "  STOP                          stop music\n"
    "                                再生を停止します\n"
    "  SLEEP sec                     sleep for a specified number of seconds.\n"
    "                                指定した秒数の間スリープします\n"
    "  SET_WIFI ssid pass            set Wi-Fi SSID & password\n"
    "                                接続するWi-FiのSSIDとパスワードを設定します\n"
    ));
}

// 不明のコマンド
void defaultCommandCallback(const char *cmd) {
  Serial.print(F("[ERROR] Unknown command: "));
  Serial.println(cmd);
  printCommandNavi();
}

void printCommandNavi() {
  Serial.println(F("  Please enter HELP to see how to use the command."));
  Serial.println(F("  コマンドの使用方法を調べるにはHELPと入力してください。"));
}

//-------------------------------------
// MQTT
//-------------------------------------

bool connectMQTT() {
  mqttClient.setServer((const char*)config["mqtt"]["hostname"], (int)config["mqtt"]["port"]);
  mqttClient.connect(config["mqtt"]["client_id"], config["mqtt"]["user_name"], NULL);
  if (!mqttClient.connected()) {
    return false;
  }
  mqttClient.setCallback(callback);
  mqttClient.subscribe(config["mqtt"]["receive_topic"]);
  return true;
}

// MQTTのPublishイベントを受け取るコールバック
void callback(char* topic, byte* payload, unsigned int length) {
  // 受け取ったJSON形式のペイロードをデコードする
  StaticJsonDocument<MQTT_MAX_PACKET_SIZE> json;
  deserializeJson(json, payload);
  char data_char[MQTT_MAX_PACKET_SIZE];
  strcpy(data_char, (const char*)json["data"]);
  deserializeJson(json, data_char);
  // コマンドを処理
  String command = json["command"];
  if (command == "play") {
    int folderNumber = json["folderNumber"];
    int fileNumber = json["fileNumber"];
    playerService.setRepeat(false);
    playerService.setShuffle(true);
    playerService.playFile(folderNumber, fileNumber);
  } else if (command == "playFolder") {
    int folderNumber = json["folderNumber"];
    playerService.setRepeat(false);
    playerService.setShuffle(true);
    playerService.playFolder(folderNumber);
  } else if (command == "stop") {
    playerService.stop();
  }
}

// イベントが発生したことを外部に通知する
void notifyEvent(const char *eventName) {
  StaticJsonDocument<MQTT_MAX_PACKET_SIZE> doc;
  auto json = doc.to<JsonObject>();
  json["event"] = eventName;
  notify(json);
}

// 「再生」イベントが発生したことを外部に通知する
void notifyPlay(uint8_t folderNumber, uint8_t fileNumber) {
  StaticJsonDocument<MQTT_MAX_PACKET_SIZE> doc;
  auto json = doc.to<JsonObject>();
  json["event"] = "play";
  json["params"]["folderNumber"] = folderNumber;
  json["params"]["fileNumber"] = fileNumber;
  notify(json);
}

// エラーが発生したことを外部に通知する
void notifyError(String errorMessage) {
  StaticJsonDocument<MQTT_MAX_PACKET_SIZE> doc;
  auto json = doc.to<JsonObject>();
  json["event"] = "error";
  json["params"]["message"] = errorMessage;
  notify(json);
}

// 外部に任意のデータを通知する
void notify(JsonObject &dataobj) {
  // Wi-Fiに接続したかどうか確認
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  char data[MQTT_MAX_PACKET_SIZE];
  serializeJson(dataobj, data);
  StaticJsonDocument<MQTT_MAX_PACKET_SIZE> doc;
  auto json = doc.to<JsonObject>();
  json["data"] = data;
  char payload[MQTT_MAX_PACKET_SIZE];
  serializeJson(json, payload);
  bool success = mqttClient.publish(config["mqtt"]["sent_topic"], payload);
  if (!success) {
    Serial.println("[ERROR] MQTT Publish Error");
  }
}

//-------------------------------------
// その他
//-------------------------------------
// 設定を読み込む
void loadConfig() {
  File f = SPIFFS.open("/config.json", "r");
  deserializeJson(config, f);
  f.close();
}

// 設定を保存
void saveConfig() {
  File f = SPIFFS.open("/config.json", "w");
  serializeJson(config, f);
  f.close();
}

String getTimeStr(){
  time_t t = time(NULL);
  struct tm *tm;
  tm = localtime(&t);
  char s[22];
  sprintf(s, "%04d-%02d-%02d %02d:%02d:%02d",
    tm->tm_year + 1900, tm->tm_mon+1, tm->tm_mday,
    tm->tm_hour, tm->tm_min, tm->tm_sec);
  return String(s);
}

float getRawVolume() {
  return fmaxf(fminf(((float)analogRead(VOLUME_PIN) - VOLUME_SENSOR_MIN) / (VOLUME_SENSOR_MAX - VOLUME_SENSOR_MIN), 1.0), 0.0);
}
