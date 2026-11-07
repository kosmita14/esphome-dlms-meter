#pragma once

#include <cstdint>

namespace esphome {
namespace dlms_meter {

enum class IndicatorColor : uint8_t { UNSET, YELLOW, GREEN, RED };

class StatusIndicator {
 public:
  static constexpr uint32_t TIMEOUT_MS = 5000;

  void start(uint32_t now) { this->start_time_ = now; }
  void success(uint32_t now) {
    this->last_success_ = now;
    this->has_success_ = true;
  }
  void disconnected() { this->has_success_ = false; }

  IndicatorColor color(uint32_t now, bool connected) const {
    if (connected && this->has_success_ && now - this->last_success_ < TIMEOUT_MS)
      return IndicatorColor::GREEN;
    if (!this->has_success_ && now - this->start_time_ < TIMEOUT_MS)
      return IndicatorColor::YELLOW;
    return IndicatorColor::RED;
  }

 protected:
  uint32_t start_time_{0};
  uint32_t last_success_{0};
  bool has_success_{false};
};

}  // namespace dlms_meter
}  // namespace esphome
