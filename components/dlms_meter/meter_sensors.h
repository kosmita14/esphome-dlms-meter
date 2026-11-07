#pragma once

#define ARDUINOJSON_ENABLE_STD_STRING 1
#include <ArduinoJson.h>

#include <cstddef>
#include <string>

namespace esphome {
namespace dlms_meter {

class MeterSensors {
 public:
  void set_topic_prefix(const std::string &prefix) { this->topic_prefix_ = prefix; }
  bool start(const std::string &json);
  bool pending() const;
  bool next(std::string &topic, std::string &payload) const;
  void advance();
  void clear();

 protected:
  JsonDocument data_;
  std::string topic_prefix_{"homeassistant/sensor"};
  std::string meter_id_;
  size_t position_{0};
  bool active_{false};
};

}  // namespace dlms_meter
}  // namespace esphome
