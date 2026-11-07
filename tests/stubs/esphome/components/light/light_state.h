#pragma once
namespace esphome::light {
struct LightCall {
  void set_brightness(float) {}
  void set_rgb(float, float, float) {}
  void set_transition_length(int) {}
  void set_publish(bool) {}
  void set_save(bool) {}
  void perform() {}
};
struct LightState { LightCall turn_on() { return {}; } };
}
