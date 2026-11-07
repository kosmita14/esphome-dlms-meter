#pragma once

#include "esphome/core/component.h"
#include "meter_sensors.h"
#include "status_indicator.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/uart/uart.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace dlms_meter {

class DlmsMeter : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void set_encryption_key(const std::array<uint8_t, 16> &key) { this->encryption_key_ = key; }
  void set_authentication_key(const std::array<uint8_t, 16> &key) { this->authentication_key_ = key; }
  void set_status_light(light::LightState *light) { this->status_light_ = light; }
  void set_mqtt_topic_prefix(const std::string &prefix) { this->sensors_.set_topic_prefix(prefix); }

 protected:
  static constexpr size_t MAX_FRAME_SIZE = 32768;
  static constexpr size_t INPUT_BUFFER_SIZE = 2048;
  void reset_pipeline_();
  bool discard_source_input_();
  void read_source_();
  void process_input_(size_t input_size);
  void publish_sensor_();
  bool decrypt_frame_(size_t length, uint8_t *&plaintext, size_t &plaintext_length);
  void reset_frame_();
  void update_status_();

  bool source_connected_{false};
  bool mqtt_connected_{false};
  bool discard_on_reconnect_{true};
  uint8_t buffer_[MAX_FRAME_SIZE];
  size_t frame_size_{0};
  size_t payload_marker_{MAX_FRAME_SIZE};
  size_t expected_size_{0};
  bool collecting_{false};
  bool start_slash_{false};
  uint8_t input_[INPUT_BUFFER_SIZE];
  size_t input_size_{0};
  size_t input_offset_{0};
  MeterSensors sensors_;
  light::LightState *status_light_{nullptr};
  StatusIndicator indicator_;
  IndicatorColor indicator_color_{IndicatorColor::UNSET};
  std::array<uint8_t, 16> encryption_key_{};
  std::array<uint8_t, 16> authentication_key_{};
};

}  // namespace dlms_meter
}  // namespace esphome
