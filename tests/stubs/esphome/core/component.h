#pragma once
namespace esphome {
namespace setup_priority {
constexpr float AFTER_WIFI = 200.0f;
}
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 600.0f; }
};
}
