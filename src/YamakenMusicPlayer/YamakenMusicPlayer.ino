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
#include "src/pubsubclient-2.7/src/PubSubClient.h"
#include "setting_loader.h"
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

// デフォルトで再生される音楽フォルダ(ボタンが押されたとき)
const int DEFAULT_MUSIC_FOLDER = 1;

//-------------------------------------
// グローバル変数
//-------------------------------------
PlayerService playerService(PLAYER_RX_PIN, PLAYER_TX_PIN);
MusicScheduler musicScheduler;

// ボタンとLED
Switch playButton(PLAY_SW_PIN);
JLed playingLed(PLAYING_LED_PIN);
JLed onlineLed(ONLINE_LED_PIN);

int volume = 30 / 2;
LPF volumeFilter(0.2, false); // ボリュームのローパスフィルタ

SerialCommand sCmd; // シリアル通信でコマンドを処理するため

WiFiSettings wifiSettings; // SSIDとパスワードを格納
MQTTSettings mqttSettings;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

//-------------------------------------
// タイマー割り込み用
//-------------------------------------
Ticker tickerUpdateSensorAndLed;
Ticker tickerCheckWiFi;
Ticker tickerUpdatePlayer;
Ticker tickerNotify;

bool wifiConnectedNotifyFlag = false;
bool playButtonPushed = false;
bool playerUpdateRequested = false;

//-------------------------------------
// setup & loop
//-------------------------------------
void setup() {
  Serial.begin(115200);
  printBannerMessage();
  Serial.println("[INFO] Setup start");

  // ボリューム用のローパスフィルタを初期化
  volumeFilter.Reset(volume);

  // LEDの点滅(スロー)を開始
  onlineLed.Breathe(1000).Forever();
  
  // タイマーの登録
  tickerUpdateSensorAndLed.attach_ms(20, updateSensorAndLed);

  // コマンドの登録
  sCmd.addCommand("HELP", printCommandHelp);
  sCmd.addCommand("SET_WIFI", setWiFiCommandCallback);
  sCmd.addCommand("PLAY", playCommandCallback);
  sCmd.addCommand("STOP", stopCommandCallback);
  sCmd.addCommand("SLEEP", sleepCommandCallback);
  sCmd.setDefaultHandler(defaultCommandCallback);

  // 
  SPIFFS.begin();

  // Wi-Fiの設定を読み込んで接続
  loadWiFiSettings(&wifiSettings);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSettings.ssid, wifiSettings.password);
  configTime(JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
  tickerCheckWiFi.attach_ms(100, checkWiFi);
  
  loadMQTTSettings(&mqttSettings);

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

int loop_count = 0;

void loop() {
  sCmd.readSerial();
  if (playButtonPushed) {
    if (!playerService.isPlaying()) {
      playerService.setRepeat(true);
      playerService.setShuffle(true);
      playerService.playFolder(DEFAULT_MUSIC_FOLDER);
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
    connectBeebotte();
    notifyEvent("startApp");
  }
  if (client.connected()) {
    client.loop();
  }
  if (loop_count % 500 == 0) {
    Serial.print("WiFi.status: ");
    Serial.println(WiFi.status());
  }
  loop_count++;
  delay(20);
}

//-------------------------------------
// シリアルコマンドのコールバック
//-------------------------------------

// SET_WIFIコマンド
void setWiFiCommandCallback() {
  wifiSettings.ssid = sCmd.next();
  wifiSettings.password = sCmd.next();
  Serial.println("[LOG] Changed WiFi");
  Serial.print("  SSID: ");
  Serial.println(wifiSettings.ssid);
  Serial.print("  Password: ");
  Serial.println(wifiSettings.password);
  saveWiFiSettings(&wifiSettings);
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

void defaultCommandCallback(const char *cmd) {
  Serial.print(F("[ERROR] Unknown command: "));
  Serial.println(cmd);
  printCommandNavi();
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
  Serial.println("[INFO] WiFi connected");
  Serial.print("  IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("  Time: ");
  Serial.println(getTimeStr());
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
// その他
//-------------------------------------
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

void printCommandHelp() {
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

void printCommandNavi() {
  Serial.println(F("  Please enter HELP to see how to use the command."));
  Serial.println(F("  コマンドの使用方法を調べるにはHELPと入力してください。"));
}

const char* clientID = "ESP8266";
const char* username = "token:token_wMgtfkI7dcGCHU4w";
const char* receive_topic = "yamaken_music_player/to_player";
const char* sent_topic = "yamaken_music_player/to_webhook";

const char* mqtt_host = "mqtt.beebotte.com";
const int port = 1833;

void connectBeebotte() {
  Serial.println(mqttSettings.hostname);
  Serial.println(mqttSettings.port);
  Serial.println(mqttSettings.clientID);
  Serial.println(mqttSettings.username);
  Serial.println(mqttSettings.receive_topic);
  Serial.println(mqttSettings.sent_topic);
  client.setServer(mqttSettings.hostname.c_str(), mqttSettings.port);
  client.connect(mqttSettings.clientID.c_str(), mqttSettings.username.c_str(), NULL);
  
  if (client.connected()) {
    Serial.println("[INFO] MQTT connected");
    client.setCallback(callback);
    client.subscribe(mqttSettings.receive_topic.c_str());
  } else {
    Serial.print("[ERROR] MQTT connection failed: ");
    Serial.println(client.state());
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

  String command = json["command"];
  
  if (command == "play") {
    int folderNumber = json["folderNumber"];
    int fileNumber = json["fileNumber"];
    playerService.playFile(folderNumber, fileNumber);
  } else if (command == "stop") {
    playerService.stop();
  }
}

void notifyEvent(const char *eventName) {
  StaticJsonDocument<MQTT_MAX_PACKET_SIZE> doc;
  auto json = doc.to<JsonObject>();
  json["event"] = eventName;
  notify(json);
}

void notifyPlay(uint8_t folderNumber, uint8_t fileNumber) {
  StaticJsonDocument<MQTT_MAX_PACKET_SIZE> doc;
  auto json = doc.to<JsonObject>();
  json["event"] = "play";
  json["params"]["folderNumber"] = folderNumber;
  json["params"]["fileNumber"] = fileNumber;
  notify(json);
}

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
  bool a = client.publish(mqttSettings.sent_topic.c_str(), payload);
  Serial.println(a);
}

