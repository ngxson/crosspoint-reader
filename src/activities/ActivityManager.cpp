#include "ActivityManager.h"

#include <HalPowerManager.h>

#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "home/HomeActivity.h"
#include "home/MyLibraryActivity.h"
#include "home/RecentBooksActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/SettingsActivity.h"
#include "util/FullScreenMessageActivity.h"

void ActivityManager::begin() {
  xTaskCreate(&renderTaskTrampoline, "ActivityManagerRender",
              8192,              // Stack size
              this,              // Parameters
              1,                 // Priority
              &renderTaskHandle  // Task handle
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

void ActivityManager::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (currentActivity) {
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      RenderLock lock;
      currentActivity->renderImpl(std::move(lock));
    }
  }
}

void ActivityManager::loop() {
  if (currentActivity) {
    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    currentActivity->loop();
  }

  if (pendingActivity) {
    // Current activity has requested a new activity to be launched
    RenderLock lock;

    exitActivity();
    currentActivity = pendingActivity;
    pendingActivity = nullptr;
    currentActivity->onEnter();
  }
}

void ActivityManager::exitActivity() {
  // Note: lock must be held by the caller
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }
}

void ActivityManager::enterNewActivity(Activity* newActivity) {
  RenderLock lock;

  if (currentActivity) {
    // Defer launch if we're currently in an activity, to avoid deleting the current activity leading to the "delete
    // this" problem
    pendingActivity = newActivity;
  } else {
    // No current activity, safe to launch immediately
    currentActivity = newActivity;
    currentActivity->onEnter();
  }
}

void ActivityManager::goToFileTransfer() { enterNewActivity(new CrossPointWebServerActivity(renderer, mappedInput)); }

void ActivityManager::goToSettings() { enterNewActivity(new SettingsActivity(renderer, mappedInput)); }

void ActivityManager::goToMyLibrary(Intent&& intent) {
  enterNewActivity(new MyLibraryActivity(renderer, mappedInput, intent.path));
}

void ActivityManager::goToRecentBooks() { enterNewActivity(new RecentBooksActivity(renderer, mappedInput)); }

void ActivityManager::goToBrowser() { enterNewActivity(new OpdsBookBrowserActivity(renderer, mappedInput)); }

void ActivityManager::goToReader(Intent&& intent) {
  enterNewActivity(new ReaderActivity(renderer, mappedInput, intent.path));
}

void ActivityManager::goToSleep() {
  enterNewActivity(new SleepActivity(renderer, mappedInput));
  loop();  // Important: sleep screen must be rendered immediately, the caller will go to sleep right after this returns
}

void ActivityManager::goToBoot() { enterNewActivity(new BootActivity(renderer, mappedInput)); }

void ActivityManager::goToFullScreenMessage(Intent&& intent) {
  enterNewActivity(new FullScreenMessageActivity(renderer, mappedInput, intent.message, intent.messageStyle));
}

void ActivityManager::goHome() { enterNewActivity(new HomeActivity(renderer, mappedInput)); }

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

bool ActivityManager::isReaderActivity() const { return currentActivity && currentActivity->isReaderActivity(); }

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

void ActivityManager::requestUpdate() {
  // Using direct notification to signal the render task to update
  // Increment counter so multiple rapid calls won't be lost
  if (renderTaskHandle) {
    xTaskNotify(renderTaskHandle, 1, eIncrement);
  }
}

// RenderLock

ActivityManager::RenderLock::RenderLock() { xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY); }

ActivityManager::RenderLock::RenderLock(Activity& /* unused */) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
}

ActivityManager::RenderLock::~RenderLock() { xSemaphoreGive(activityManager.renderingMutex); }
