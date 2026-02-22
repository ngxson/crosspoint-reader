#include "Activity.h"

#include "ActivityManager.h"

void Activity::onEnter() { LOG_DBG("ACT", "Entering activity: %s", name.c_str()); }

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate() { activityManager.requestUpdate(); }

void Activity::requestUpdateAndWait() {
  // FIXME @ngxson : properly implement this using freeRTOS notification
  delay(100);
}

void Activity::onGoHome() { activityManager.goHome(); }

void Activity::onSelectBook(const std::string& path) {
  activityManager.goToReader(path);
}

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(const ActivityResult& result) { this->result = result; }

void Activity::finish() { activityManager.popActivity(); }
