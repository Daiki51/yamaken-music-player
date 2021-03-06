#include <math.h>
#include "PlayerService.h"

static String getDetail(uint8_t type, int value);

static void defaultPlayCallback(uint8_t folderNumber, uint8_t fileNumber) {
  // do nothing
}

static void defaultStopCallback() {
  // do nothing
}

static void defaultErrorCallback(String message) {
  // do nothing
}

PlayerService::PlayerService(int8_t rx_pin, int8_t tx_pin) {
  _rx_pin = rx_pin;
  _tx_pin = tx_pin;
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
  _errorCallback = defaultErrorCallback;
}

void PlayerService::begin() {
  // DEF_playerと通信するためにSoftwareSerialを使用
  _software_serial.begin(9600, _rx_pin, _tx_pin);
  _player.begin(_software_serial);
  _player.stop();
  Serial.println(F("[INFO] DFPlayer Mini initialized"));
}

void PlayerService::playFile(uint8_t folderNumber, uint8_t fileNumber) {
  // キューに追加
  _queueIndex = 0;
  for (int i = 0; i < MAX_MUSIC_QUEUE; i++) {
    if (i > 0 && !_repeat) {
      _playQueue[i][0] = -1;
      _playQueue[i][1] = -1;
    } else {
      _playQueue[i][0] = folderNumber;
      _playQueue[i][1] = fileNumber;
    }
  }
  // キューを再生
  _playQueueOne();
}

void PlayerService::playFolder(uint8_t folderNumber) {
  // 曲順を決定
  int totalTrack;
  // トラックの数を取得 (エラーを考慮して数回試行する)
  for (int i = 0; i < 100; i++) {
    totalTrack = _player.readFileCountsInFolder(folderNumber);
    if (totalTrack != -1) {
      break;
    }
    delay(10);
  }
  if (totalTrack == -1) {
    Serial.println("[ERROR] readFileCountsInFolder = -1");
    while (true) {
      // プログラム終了
      delay(1000); 
    }
  }
  Serial.print("[INFO] Total Track: ");
  Serial.println(totalTrack);
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
    _stopCallback();
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
  while (_player.available()) { // 受信している情報がある場合
    uint8_t type = _player.readType();
    int value = _player.read();
    if (type == DFPlayerPlayFinished && !_isWaiting) {
      // 曲の最後まで到達したとき
      _isWaiting = true;
      _tickerNextPlay.once_ms(_waitDuration, [this]() {
        _nextPlayRequested = true;
      });
    } else if (type == DFPlayerError && value == FileMismatch && !_isWaiting) {
      // 存在しないファイルを再生しようとしたとき
      _isWaiting = true;
      _errorCallback(getDetail(type, value));
      _tickerNextPlay.once_ms(_waitDuration, [this]() {
        _nextPlayRequested = true;
      });
    } else {
      // エラーのとき
      _errorCallback(getDetail(type, value));
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

void PlayerService::onError(error_callback_t callback) {
  _errorCallback = callback;
}

String getDetail(uint8_t type, int value) {
  switch (type) {
    case TimeOut:
      return F("Time Out!");
    case WrongStack:
      return F("Stack Wrong!");
    case DFPlayerCardInserted:
      return F("Card Inserted!");
    case DFPlayerCardRemoved:
      return F("Card Removed!");
    case DFPlayerCardOnline:
      return F("Card Online!");
    case DFPlayerPlayFinished:
      return String(F("Number:")) + value + F(" Play Finished!");
    case DFPlayerError:
      switch (value) {
        case Busy:
          return F("DFPlayerError: Card not found");
        case Sleeping:
          return F("DFPlayerError: Sleeping");
        case SerialWrongStack:
          return F("DFPlayerError: Get Wrong Stack");
        case CheckSumNotMatch:
          return F("DFPlayerError: Check Sum Not Match");
        case FileIndexOut:
          return F("DFPlayerError: File Index Out of Bound");
        case FileMismatch:
          return F("DFPlayerError: Cannot Find File");
        case Advertise:
          return F("DFPlayerError: In Advertise");
      }
      break;
  }
}
