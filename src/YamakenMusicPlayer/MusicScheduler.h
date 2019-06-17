#pragma once
#include "PlayerService.h"

const int MAX_TASK = 20;

typedef std::function<void(void)> start_callback_t;
typedef std::function<void(void)> end_callback_t;

struct TimeHM {
  int8_t hour;
  int8_t minites;
};

class ScheduledTask {
public:
  bool enable;
  char *name[32];
  TimeHM startTime;
  TimeHM endTime;
  int16_t folderNumber;
  int16_t fileNumber;
  bool repeat;
  bool shuffle;
};

class MusicScheduler {
public:
  MusicScheduler();
  void bind(PlayerService *playerService);
  void loadTask();
  void begin();
  void update();
  void onStart(start_callback_t callback);
  void onEnd(end_callback_t callback);
private:
  bool _isBegin;
  ScheduledTask _tasks[MAX_TASK];
  PlayerService *_playerService;
  ScheduledTask *_processingTask;
  start_callback_t _startCallback;
  start_callback_t _endCallback;
};
