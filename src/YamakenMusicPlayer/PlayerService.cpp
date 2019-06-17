#include <math.h>
#include "PlayerService.h"
#include "setting_loader.h"

static void printDetail(uint8_t type, int value);

static void defaultPlayCallback(uint8_t folderNumber, uint8_t fileNumber) {
  // do nothing
}

static void defaultStopCallback() {
  // do nothing
}

PlayerService::PlayerService(int rx_pin, int tx_pin) 
  : _software_serial(rx_pin, tx_pin) {
  _isPlaying = false;
  _isFading = false;
  _isWaiting = false;
  _volume = 30 / 2;
  _waitDuration = 2000;
  _queueIndex = 0;
  _repeat = false;
  _shuffle = false;
  _nextPlayRequested = false;
  for (int i = 0;  i < MAX_MUSIC_QUEUE; i++) {
    _playQueue[i][0] = -1;
    _playQueue[i][1] = -1;
  }
  _playCallback = defaultPlayCallback;
  _stopCallback = defaultStopCallback;
}

void PlayerService::begin() {
  // DEF_playerと通信するためにSoftwareSerialを使用
  _software_serial.begin(9600);
  if (!_player.begin(_software_serial)) {
    Serial.println(F("[ERROR] DFPlayer Mini is unable to begin:"));
    Serial.println(F("  1.Please recheck the connection!"));
    Serial.println(F("  2.Please insert the SD card!"));
    while(true) delay(1);
  }
  Serial.println(F("[INFO] DFPlayer Mini initialized"));
}

void PlayerService::playFile(uint8_t folderNumber, uint8_t fileNumber) {
  // キューに追加
  _queueIndex = 0;
  for (int i = 0; i < MAX_MUSIC_QUEUE; i++) {
    if (i > 0 && !_repeat) {
      _playQueue[i][0] = -1;
      _playQueue[i][1] = -1;
    }
    _playQueue[i][0] = folderNumber;
    _playQueue[i][1] = fileNumber;
  }
  // キューを再生
  _playQueueOne();
}

void PlayerService::playFolder(uint8_t folderNumber) {
  // 曲順を決定
  int totalTrack = _player.readFileCountsInFolder(folderNumber); // トラックの数
  auto order = new uint8_t[totalTrack];
  for (int i = 0; i < totalTrack; i++) {
    order[i] = i + 1;
  }
  // シャッフル
  if (_shuffle) {
    for (int i = 0; i < totalTrack; i++) {
      int j = random(totalTrack);
      auto tmp = order[i];
      order[i] = order[j];
      order[j] = tmp;
    }
  }
  // キューに追加
  _queueIndex = 0;
  for (int i = 0; i < MAX_MUSIC_QUEUE; i++) {
    if (i >= totalTrack && !_repeat) {
      _playQueue[i][0] = -1;
      _playQueue[i][1] = -1;
    }
    _playQueue[i][0] = folderNumber;
    _playQueue[i][1] = order[i % totalTrack];
  }
  // キューを再生
  _playQueueOne();
}

void PlayerService::_playQueueOne() {
  auto folderNumber = _playQueue[_queueIndex][0];
  auto fileNumber = _playQueue[_queueIndex][1];
  if (folderNumber == -1) {
    // キューが空なので再生停止
    _isPlaying = false;
    return;
  }
  _player.volume(0);
  _player.playFolder(folderNumber, fileNumber);
  _isPlaying = true;
  // フェードイン
  _isFading = true;
  for (int i = 0; i <= 30; i++) {
    _player.volume((int)round((float)_volume * i / 30));
    delay(20);
  }
  _isFading = false;
  _playCallback(folderNumber, fileNumber);
}

void PlayerService::_nextPlay() {
  _queueIndex = (_queueIndex + 1) % 0xff;
  _isWaiting = false;
  _playQueueOne();
}

void PlayerService::stop() {
  // フェードアウト
  _isFading = true;
  for (int i = 30; i >= 0; i--) {
    _player.volume((int)round((float)_volume * i / 30));
    delay(20);
  }
  _isFading = false;
  _player.pause();
  _isPlaying = false;
  _stopCallback();
}

void PlayerService::update() {
  if (_player.available()) { // 受信している情報がある場合
    uint8_t type = _player.readType();
    int value = _player.read();
    // 曲の最後まで到達したとき
    if (type == DFPlayerPlayFinished && !_isWaiting) {
      _isWaiting = true;
      _tickerNextPlay.once_ms(_waitDuration, [this]() {
        _nextPlayRequested = true;
      });
    }
    // カードが抜き取られたとき
    if (type == DFPlayerCardRemoved) {
      _isPlaying = false;
      _isWaiting = false;
      Serial.println(F("[WARNING] SD card removed."));
      _stopCallback();
    }
    // エラーのとき
    if (type == DFPlayerError) {
      // 詳細を表示
      printDetail(type, value);
    }
  }
  if (_nextPlayRequested) {
    _nextPlayRequested = false;
    _nextPlay();
  }
}

void PlayerService::setVolume(int value) {
  if (value != _volume) {
    if (!_isFading) {
      _player.volume(value);
    }
    _volume = value;
  }
}

void PlayerService::setWaitDuration(int value) {
  _waitDuration = value;
}

void PlayerService::setRepeat(bool value) {
  _repeat = value;
}

void PlayerService::setShuffle(bool value) {
  _shuffle = value;
}

bool PlayerService::isPlaying() {
  return _isPlaying;
}

void PlayerService::onPlay(play_callback_t callback) {
  _playCallback = callback;
}

void PlayerService::onStop(stop_callback_t callback) {
  _stopCallback = callback;
}

void printDetail(uint8_t type, int value) {
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
      Serial.print(F("[ERROR] DFPlayerError:"));
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
      }
      break;
  }
}
