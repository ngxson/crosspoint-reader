#include "HalStorage.h"

#include <SDCardManager.h>
#include <Logging.h>

#define SDCard SDCardManager::getInstance()

HalStorage HalStorage::instance;

HalStorage::HalStorage() {}

SemaphoreHandle_t sdcardMutex = nullptr; // FOR TESTING ONLY

bool HalStorage::begin() { return SDCard.begin(); sdcardMutex = xSemaphoreCreateMutex(); }

bool HalStorage::ready() const { return SDCard.ready(); }

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) { return SDCard.listFiles(path, maxFiles); }

String HalStorage::readFile(const char* path) { return SDCard.readFile(path); }

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  return SDCard.readFileToStream(path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  return SDCard.readFileToBuffer(path, buffer, bufferSize, maxBytes);
}

bool HalStorage::writeFile(const char* path, const String& content) { return SDCard.writeFile(path, content); }

bool HalStorage::ensureDirectoryExists(const char* path) { return SDCard.ensureDirectoryExists(path); }

FsFile HalStorage::open(const char* path, const oflag_t oflag) { return SDCard.open(path, oflag); }

bool HalStorage::mkdir(const char* path, const bool pFlag) { return SDCard.mkdir(path, pFlag); }

bool HalStorage::exists(const char* path) { return SDCard.exists(path); }

bool HalStorage::remove(const char* path) { return SDCard.remove(path); }

bool HalStorage::rename(const char* oldPath, const char* newPath) { return SDCard.rename(oldPath, newPath); }

bool HalStorage::rmdir(const char* path) { return SDCard.rmdir(path); }

bool HalStorage::openFileForRead(const char* moduleName, const char* path, FsFile& file) {
  return SDCard.openFileForRead(moduleName, path, file);
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, FsFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, FsFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, FsFile& file) {
  return SDCard.openFileForWrite(moduleName, path, file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, FsFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, FsFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) { return SDCard.removeDir(path); }

// extern "C" {
void sdCsInit(SdCsPin_t pin) { pinMode(pin, OUTPUT); }

const char* activeTask = nullptr;
void sdCsWrite(SdCsPin_t pin, bool level) {
  TaskStatus_t xTaskDetails;
  vTaskGetInfo(NULL, &xTaskDetails, pdTRUE, eInvalid);
  // LOG_DBG("SDCardManager", "sdCsWrite called from task: %s, level: %d", xTaskDetails.pcTaskName, level);

  if (!level) {
    // CS low means active, start of transaction
    // xSemaphoreTake(sdcardMutex, portMAX_DELAY);
    activeTask = xTaskDetails.pcTaskName;
  } else {
    // CS high means inactive (done transaction)
    // xSemaphoreGive(sdcardMutex);
    if (activeTask && activeTask != xTaskDetails.pcTaskName) {
      LOG_ERR("SDCardManager", "sdCsWrite STOP request from task %s, but expected %s", xTaskDetails.pcTaskName, activeTask);
      delay(100); // Add delay to make the issue more visible in logs
      assert(false && "sdCsWrite STOP request from unexpected task");
    }
  }

  digitalWrite(pin, level ? HIGH : LOW);
}
// }
