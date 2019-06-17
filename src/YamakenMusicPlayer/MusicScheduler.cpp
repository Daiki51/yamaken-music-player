#include <inttypes.h>
#include <FS.h>
#include "MusicScheduler.h"

static void defaultStartCallback() {
  // do nothing
}

static void defaultEndCallback() {
  // do nothing
}

MusicScheduler::MusicScheduler() {
  _isBegin = false;
  _processingTask = nullptr;
  for (int i = 0; i < MAX_TASK; i++) {
    _tasks[i].enable = false;
  }
  _startCallback = defaultStartCallback;
  _endCallback = defaultEndCallback;
}

void MusicScheduler::bind(PlayerService *playerService) {
  this->_playerService = playerService;
}

void MusicScheduler::loadTask() {
  for (int i = 0; i < MAX_TASK; i++) {
    _tasks[i].enable = false;
  }
  File f = SPIFFS.open("/task.txt", "r");
  String line;
  char lineBuffer[0x100];
  int j = 0;
  int startTimeHour, startTimeMinites, endTimeHour, endTimeMinites, folderNumber, fileNumber;
  char option1[10];
  char option2[10];
  while (f.available()) {
    line = f.readStringUntil('\n');
    line.trim();
    // Serial.println(line);
    if (line.charAt(0) == '#') {
      continue;
    }
    auto &task = _tasks[j++];
    line.toCharArray(lineBuffer, sizeof(lineBuffer));
    sscanf(lineBuffer, "%2d:%2d %2d:%2d %d %d% %s %s*[^\n]%*c",
      &startTimeHour,
      &startTimeMinites,
      &endTimeHour,
      &endTimeMinites,
      &folderNumber,
      &fileNumber,
      &option1,
      &option2);
    task.enable = true;
    task.startTime.hour = (int8_t)startTimeHour;
    task.startTime.minites = (int8_t)startTimeMinites;
    task.endTime.hour = (int8_t)endTimeHour;
    task.endTime.minites = (int8_t)endTimeMinites;
    task.folderNumber = (int16_t)folderNumber;
    task.fileNumber = (int16_t)fileNumber;
    task.shuffle = false;
    task.repeat = false;
    if (strcmp(option1, "shuffle") || strcmp(option2, "shuffle")) {
      task.shuffle = true;
    }
    if (strcmp(option1, "repeat") || strcmp(option2, "repeat")) {
      task.repeat = true;
    }
  }
  f.close();
}

void MusicScheduler::begin() {
  _isBegin = true;
}

void MusicScheduler::update() {
  if (!_isBegin) return;
  time_t t = time(NULL);
  struct tm *tm;
  tm = localtime(&t);
  if (!_processingTask) {
    for (int i = 0; i < MAX_TASK; i++) {
      if (_tasks[i].enable && _tasks[i].startTime.hour == tm->tm_hour && _tasks[i].startTime.minites == tm->tm_min) {
        _processingTask = &_tasks[i];
        _playerService->setRepeat(_processingTask->repeat);
        _playerService->setShuffle(_processingTask->shuffle);
        if (_processingTask->fileNumber == -1) {
          _startCallback();
          _playerService->playFolder(_processingTask->folderNumber);
        } else {
          _startCallback();
          _playerService->playFile(_processingTask->folderNumber, _processingTask->fileNumber);
        }
        break;
      }
    }
  } else {
    if (_processingTask->endTime.hour == tm->tm_hour && _processingTask->endTime.minites == tm->tm_min) {
      _processingTask = nullptr;
      _playerService->stop();
      _endCallback();
    }
  }
}

void MusicScheduler::onStart(start_callback_t callback) {
  _startCallback = callback;
}

void MusicScheduler::onEnd(end_callback_t callback) {
  _endCallback = callback;
}

