#pragma once

#include <cstdint>
#include <string>

class HalSystem {
 public:
  struct StackFrame {
    uint32_t sp;
    uint32_t spp[8];
  };

  HalSystem();

  // Dump panic info to SD card if necessary
  void checkPanic();
  void clearPanic();

  std::string getPanicInfo(bool full = false);
  bool isRebootFromPanic();
};

extern HalSystem halSystem;
