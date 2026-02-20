#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cassert>
#include <string>
#include <typeinfo>

#include "GfxRenderer.h"
#include "MappedInputManager.h"

struct Intent {
  std::string path;

  // FullScreenMessage
  std::string message;
  EpdFontFamily::Style messageStyle;
};

class Activity;  // forward declaration

class ActivityManager {
 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  Activity* currentActivity = nullptr;

  void exitActivity();
  void enterNewActivity(Activity* newActivity);

  // Pending activity to be launched on next loop iteration
  Activity* pendingActivity = nullptr;

  // Task to render and display the activity
  TaskHandle_t renderTaskHandle = nullptr;
  static void renderTaskTrampoline(void* param);
  [[noreturn]] virtual void renderTaskLoop();

  // Mutex to protect rendering operations from race conditions
  SemaphoreHandle_t renderingMutex = nullptr;

 public:
  explicit ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : renderer(renderer), mappedInput(mappedInput), renderingMutex(xSemaphoreCreateMutex()) {
    assert(renderingMutex != nullptr && "Failed to create rendering mutex");
  }
  ~ActivityManager() { assert(false); /* should never be called */ };

  void begin();
  void loop();

  void goToFileTransfer();
  void goToSettings();
  void goToMyLibrary(Intent&& intent);
  void goToRecentBooks();
  void goToBrowser();
  void goToReader(Intent&& intent);
  void goToSleep();
  void goToBoot();
  void goToFullScreenMessage(Intent&& intent);
  void goHome();

  bool preventAutoSleep() const;
  bool isReaderActivity() const;
  bool skipLoopDelay() const;

  void requestUpdate();

  // RAII helper to lock rendering mutex for the duration of a scope.
  class RenderLock {
    friend class ActivityManager;

   public:
    explicit RenderLock();
    explicit RenderLock(Activity&);  // unused for now, but keep for compatibility
    RenderLock(const RenderLock&) = delete;
    RenderLock& operator=(const RenderLock&) = delete;
    ~RenderLock();
  };
};

extern ActivityManager activityManager;  // singleton, to be defined in main.cpp
