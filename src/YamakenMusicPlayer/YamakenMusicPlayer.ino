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
const int PLAYER_RX_PIN = 4;
const int PLAYER_TX_PIN = 5;
const int PLAYING_LED_PIN = 12;
const int ONLINE_LED_PIN = 13;
const int PLAY_SW_PIN = 14;
const int VOLUME_PIN = A0;

// ボリュームセンサーのアナログ最大値
const int VOLUME_SENSOR_MAX = 1001;

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

int volume = 30 / 2;
LPF volumeFilter(0.2, false); // ボリュームのローパスフィルタ

SerialCommand sCmd; // シリアル通信でコマンドを処理するため

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// 設定ストア(JSON形式)
StaticJsonDocument<1000> config;
bool isLoaded = false;

//-------------------------------------
// タイマー割り込み用
//-------------------------------------
Ticker tickerUpdateSensorAndLed;
Ticker tickerCheckWiFi;
Ticker tickerUpdatePlayer;

bool wifiConnectedNotifyFlag = false;
bool playButtonPushed = false;
bool playerUpdateRequested = false;
bool wifiConnecting = false;
int wifiConnectCount = 0;
int loop_count = 0;

//-------------------------------------
// setup & loop
//-------------------------------------
void setup() {
  Serial.begin(115200);
  printBannerMessage();
  Serial.println("[INFO] Setup start");

  // ボリューム用のローパスフィルタを初期化
  volumeFilter.Reset(volume);
  
  // タイマーの登録
  tickerUpdateSensorAndLed.attach_ms(20, updateSensorAndLed);

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

  // プレイヤーを初期化
  playerService.begin();
  playerService.onPlay(onPlayCallback);
  playerService.onStop(onStopCallback);
  
  // タイマーの登録
  tickerUpdatePlayer.attach_ms(50, updatePlayer);

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
  WiFi.begin((const char*)config["wifi"]["ssid"], (const char*)config["wifi"]["password"]);
  configTime(JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
  tickerCheckWiFi.attach_ms(100, checkWiFi);
  wifiConnecting = true;
}

void loop() {
  sCmd.readSerial();
  if (playButtonPushed) {
    if (!playerService.isPlaying()) {
      playerService.setRepeat(true);
      playerService.setShuffle(true);
      playerService.playFolder(config["music"]["default_folder"]);
    } else {
      playerService.stop();
    }
    playButtonPushed = false;
  }
  if (playerUpdateRequested) {
    playerService.update();
    playerService.setVolume(volume);
    musicScheduler.update();
    playerUpdateRequested = false;
  }
  if (wifiConnectedNotifyFlag) {
    wifiConnectedNotifyFlag = false;
    if (wifiConnectCount == 0) {
      Serial.println("[INFO] Wi-Fi connected");
    } else {
      Serial.println("[INFO] Wi-Fi reconnected");
    }
    Serial.print("  IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Time: ");
    Serial.println(getTimeStr());
    connectMQTT();
    if (wifiConnectCount == 0) {
      notifyEvent("startApp");
    } else {
      notifyEvent("reconnect");
    }
    wifiConnecting = false;
    wifiConnectCount++;
  }
  if (!wifiConnecting && WiFi.status() != WL_CONNECTED) {
    Serial.println("[WARN] Wi-Fi disconnected");
    // Wi-Fiに再接続
    connectWiFi();
  }
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  loop_count++;
  delay(20);
}

//-------------------------------------
// タイマー割り込みのコールバック
//-------------------------------------
void updateSensorAndLed() {
  playButton.poll();
  playingLed.Update();
  onlineLed.Update();
  volume = (int)round(volumeFilter.NextValue(round(30.0 * analogRead(VOLUME_PIN) / VOLUME_SENSOR_MAX)));
  if (playButton.pushed()) {
    playButtonPushed = true;
  }
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
  wifiConnectedNotifyFlag = true;
}

void updatePlayer() {
  playerUpdateRequested = true;
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
    "  SET_WIFI ssid pass            set Wi-Fi ssid & password\n"
    "                                接続するWi-Fiのssidとパスワードを設定します\n"
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

void connectMQTT() {
  mqttClient.setServer((const char*)config["mqtt"]["hostname"], (int)config["mqtt"]["port"]);
  mqttClient.connect(config["mqtt"]["client_id"], config["mqtt"]["user_name"], NULL);
  
  if (mqttClient.connected()) {
    Serial.println("[INFO] MQTT connected");
    mqttClient.setCallback(callback);
    mqttClient.subscribe(config["mqtt"]["receive_topic"]);
  } else {
    Serial.print("[ERROR] MQTT connection failed: ");
    Serial.println(mqttClient.state());
  }
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
    playerService.playFile(folderNumber, fileNumber);
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

