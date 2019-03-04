#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <DFRobotDFPlayerMini.h>
#include <SoftwareSerial.h>
#include <time.h>
#include <Ticker.h>
#include <math.h>
#include "tracknames.h"
#include "config.h"

//-------------------------
// 定数
// ------------------------

// ピン番号
const int PLAYER_RX_PIN = 4;
const int PLAYER_TX_PIN = 5;
const int PLAYING_LED_PIN = 12;
const int ONLINE_LED_PIN = 13;
const int PLAY_BTN_PIN = 14;
const int VOLUME_PIN = A0;

// 日本標準時(+0900)
const int JST = 3600*9;

//-------------------------
// グローバル変数
// ------------------------
SoftwareSerial mySoftwareSerial(PLAYER_RX_PIN, PLAYER_TX_PIN); // DFPlayerとの通信に使用
DFRobotDFPlayerMini player;
bool is_playing = false; // 再生中かどうか(曲と曲との間の待機時間も含む)
bool is_waiting = false; // 待機中かどうか
bool is_sync_time = false; // 時刻をインターネット時刻と同期したかどうか
unsigned long play_start_time; // 最初の曲を再生し始めてからの経過時間
int last_music_finished = 0; // 最後に曲を再生し終わってからの経過時間
int total_track; // 総トラック数
int track_num; // 現在のトラック
int loop_count = 0;
int pre_switch_value = HIGH;
char order[0xff];
int max_volume = 10;
String ifttt_base_url = "http://maker.ifttt.com/trigger/" + IFFTT_EVENT + "/with/key/" + IFFTT_KEY + "?value1=";

Ticker ticker;

void refreshLED() { // 10ms毎に呼び出される
  static int count = 0;
  static bool sync_time_flag = false;
  if (!sync_time_flag) {
    float val = (sin((float)(count % 100) / 100 * PI * 2) + 1.0) / 2;
    if (is_sync_time && count % 100 == 0) {
      sync_time_flag = true;
    }
    analogWrite(ONLINE_LED_PIN, (int)(val * 0xff));
  } else {
    analogWrite(ONLINE_LED_PIN, 0xff);
  }
  digitalWrite(PLAYING_LED_PIN, is_playing);
  count++;
}

// 初期設定
void setup () {

  // シリアル設定
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println(F("Stating \"Yamaken Music Player App\"..."));

  // LEDを更新する処理を10ms毎に呼び出す
  ticker.attach_ms(10, refreshLED);

  // 乱数を初期化
  randomSeed(analogRead(A0));

  pinMode(PLAYING_LED_PIN, OUTPUT);
  pinMode(ONLINE_LED_PIN, OUTPUT);
  pinMode(PLAY_BTN_PIN, INPUT_PULLUP);

  setup_player();
  setup_wifi();

  // 電源投入時に再生開始
//  play();
}

void setup_wifi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void setup_player() {
  // DEFPlayerと通信するためにSoftwareSerialを使用
  mySoftwareSerial.begin(9600);
  if (!player.begin(mySoftwareSerial)) {
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    while(true) delay(1);
  }
  total_track = player.readFileCountsInFolder(MUSIC_FOLDER); //　トラックの数
  for (int i = 0; i < 0xff; i++) {
      order[i] = i + 1;
  }
  Serial.println(F("DFPlayer Mini initialized"));
}

void loop () {
  unsigned long current_time = millis();
  int switch_value = digitalRead(PLAY_BTN_PIN);

  // Wi-FIに接続できていたら時刻を同期する
  if (!is_sync_time && WiFi.status() == WL_CONNECTED) {

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    configTime(JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
    delay(1000);
    print_time();
    notify("【お知らせ】自動昼休み音楽プレイヤーが起動しました。");

    is_sync_time = true;
  }

  // 音量を更新
  int val = 30 * (float)analogRead(VOLUME_PIN) / 1023;
  if (val != max_volume) {
    max_volume = val;
    player.volume(max_volume);
  }

  if (is_playing) {
    // スイッチが押されたら
    if (switch_value == LOW && pre_switch_value != HIGH) {
      stop();
      Serial.println(F("stop"));
      notify("【お知らせ】再生が停止されました(手動)。");
    }
    // 連続再生時間が上限を超過したとき
    if (current_time - play_start_time > MAX_PLAY_DURATION) {

      // 自動的に停止
      stop();
      Serial.println(F("automatic stop"));
      notify("【お知らせ】指定された時刻になったので再生を停止します。");
    }

    // 曲と曲との間の待機時間が終了したとき
    if (is_waiting && current_time - last_music_finished > WAIT_TIME) {
      is_waiting = false;

      // 最後の曲まで再生したとき
      if (track_num == total_track) {
        stop();
        Serial.print(F("finish last track"));

      } else {
        next();
        Serial.print(F("play next: "));
        Serial.println(track_num);
      }
    }

    // 現在のプレイヤーの情報を取得し、状況に応じて制御
    bool is_available = player.available();
    uint8_t type = player.readType();
    int read_value = player.read();

    // 現在、再生しているとき
    if (is_available) {
      // 曲の最後まで到達したとき
      if (type == DFPlayerPlayFinished && !is_waiting) {
        is_waiting = true;
        last_music_finished = current_time;
        Serial.print(F("finished track: "));
        Serial.println(read_value);
        Serial.println(F("waiting..."));
      }
      // カードが抜き取られたとき
      else if (type == DFPlayerCardRemoved) {
        is_playing = false;
        is_waiting = false;
        Serial.println(F("[warning] card removed"));
      }
    }
    // エラーのとき
    if (type == DFPlayerError) {
      // 詳細を表示
      printDetail(type, read_value);
    }
  // 現在、再生していないとき
  } else {
    // 自動再生時刻になったら
    if (is_auto_start_time()) {
      // 自動的に再生
      play();
      Serial.print(F("automatic play"));
      notify("【お知らせ】指定された時刻になったので再生を開始します。");
    // スイッチが押されたら
    } else if (switch_value == LOW && pre_switch_value == HIGH) {
      play();
      Serial.println(F("play"));
    }
  }

  pre_switch_value = switch_value;

  delay(100);
}

void play() {
    shuffle_track();
    player.volume(0);
    track_num = 1;
    player.playFolder(MUSIC_FOLDER, order[track_num - 1]);
    is_playing = true;
    play_start_time = millis();
    // フェードイン
    for (int volume = 0; volume <= max_volume; volume++) {
      player.volume(volume);
      delay(50);
    }
    notify(String("【お知らせ】『") + track_names[order[track_num - 1] - 1] + "』を再生します。");
}

void stop() {
    // フェードアウト
    for (int volume = max_volume; volume >= 0; volume--) {
      player.volume(volume);
      delay(30);
    }
    delay(500);
    player.pause();
    is_playing = false;
}

void next() {
    track_num++;
    player.playFolder(MUSIC_FOLDER, order[track_num - 1]);
    notify(String("【お知らせ】『") + track_names[order[track_num - 1] - 1] + "』を再生します。");
}

void shuffle_track() {
    for (int i = 0; i < total_track; i++) {
        int j = random(total_track);
        char tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }
//    for (int i = 0; i < total_track; i++) {
//      Serial.println((int)order[i]);
//    }
}

void notify(String text) {
  if (!USE_IFFTT) {
    return;
  }
  String url = ifttt_base_url + urlencode(text);
  HTTPClient http;
  http.begin(url);
  http.GET();
  http.end();
}

void printDetail(uint8_t type, int value){
  switch (type) {
    case TimeOut:
      Serial.println(F("Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          break;
        case FileIndexOut:
          Serial.println(F("File Index Out of Bound"));
          break;
        case FileMismatch:
          Serial.println(F("Cannot Find File"));
          break;
        case Advertise:
          Serial.println(F("In Advertise"));
          break;
        default:
          Serial.println();
          break;
      }
      break;
    default:
      break;
  }
}

void print_time(){
  time_t t = time(NULL);
  struct tm *tm;
  tm = localtime(&t);
  char s[20];
  sprintf(s, "%04d-%02d-%02d %02d:%02d:%02d",
    tm->tm_year + 1900, tm->tm_mon+1, tm->tm_mday,
    tm->tm_hour, tm->tm_min, tm->tm_sec);
  Serial.print("Time: ");
  Serial.println(s);
}

bool is_auto_start_time() {
  time_t t = time(NULL);
  struct tm *tm;
  tm = localtime(&t);
  return is_sync_time && tm->tm_hour == MUSIC_START_TIME_H && tm->tm_min == MUSIC_START_TIME_M;
}
