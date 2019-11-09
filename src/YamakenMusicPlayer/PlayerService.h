#pragma once

#include <DFRobotDFPlayerMini.h>
#include <SoftwareSerial.h>
#include <Ticker.h>

const int MAX_MUSIC_QUEUE = 0x100;
const int MAX_FILE = 0x100;

typedef std::function<void(uint8_t, uint8_t)> play_callback_t;
typedef std::function<void(void)> stop_callback_t;

class PlayerService {
public:
  PlayerService(int8_t rx_pin, int8_t tx_pin);
  void begin();
  void playFile(uint8_t folderNumber, uint8_t fileNumber);
  void playFolder(uint8_t folderNumber);
  void stop();
  void setVolume(int value);
  void setWaitDuration(int value);
  void setRepeat(bool value);
  void setShuffle(bool value);
  bool isPlaying();
  void onPlay(play_callback_t callback);
  void onStop(stop_callback_t callback);
  void update();
private:
  SoftwareSerial _software_serial;
  int8_t _rx_pin;
  int8_t _tx_pin;
  DFRobotDFPlayerMini _player;
  int _volume;
  bool _isPlaying;
  bool _isFading;
  bool _isWaiting;
  int _waitDuration;
  int _queueIndex;
  int16_t _playQueue[MAX_MUSIC_QUEUE][2];
  bool _repeat;
  bool _shuffle;
  void _playQueueOne();
  Ticker _tickerNextPlay;
  bool _nextPlayRequested;
  void _nextPlay();
  play_callback_t _playCallback;
  stop_callback_t _stopCallback;
};
