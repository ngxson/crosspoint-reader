#include "ActivityWithSubactivity.h"

#include <HalPowerManager.h>

void ActivityWithSubactivity::renderImpl(RenderLock&& lock) {
  if (subActivity) {
    subActivity->renderImpl(std::move(lock));
  } else {
    render(std::move(lock));
  }
}

void ActivityWithSubactivity::exitActivity() {
  // No need to lock, since onExit() already acquires its own lock
  if (subActivity) {
    LOG_DBG("ACT", "Exiting subactivity...");
    subActivity->onExit();
    subActivity.reset();
  }
}

void ActivityWithSubactivity::enterNewActivity(Activity* activity) {
  // Acquire lock to avoid 2 activities rendering at the same time during transition
  // RenderLock lock(*this); // TODO: what to do?
  subActivity.reset(activity);
  subActivity->onEnter();
  LOG_DBG("ACT", "Entering subactivity...");
}

void ActivityWithSubactivity::loop() {
  // TODO: this logic should already be handled by activity.loop() function, it should be removed
  if (subActivity) {
    subActivity->loop();
  }
}

void ActivityWithSubactivity::requestUpdate() {
  if (!subActivity) {
    Activity::requestUpdate();
  }
  // Sub-activity should call their own requestUpdate() from their loop() function
}

void ActivityWithSubactivity::onExit() {
  // No need to lock, onExit() already acquires its own lock
  exitActivity();
  Activity::onExit();
}
