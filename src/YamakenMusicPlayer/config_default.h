
//-------------------------
// 設定項目
// ------------------------

// 曲が格納されているフォルダ
const int MUSIC_FOLDER = 1;

// 自動再生時刻
const int MUSIC_START_TIME_H = 12;
const int MUSIC_START_TIME_M = 31;

// 連続再生時間の上限
const unsigned long MAX_PLAY_DURATION = (40 - 3) * 60 * 1000; // 昼休みの40分間

// 曲と曲との間の待機時間
const unsigned long WAIT_TIME = 2000;

// Wi-Fiの設定
const char* WIFI_SSID = "********";
const char* WIFI_PASSWORD = "********";

// IFTTTの設定
const bool USE_IFFTT = true;
const String IFFTT_EVENT = "********";
const String IFFTT_KEY = "********";
