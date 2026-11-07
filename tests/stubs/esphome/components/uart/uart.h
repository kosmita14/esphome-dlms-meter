#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
namespace esphome::uart {
class UARTComponent {
 public:
  bool connected = true;
  std::deque<uint8_t> bytes;
  virtual bool is_connected() { return connected; }
  size_t available() { return bytes.size(); }
  bool read_array(uint8_t *data, size_t count) {
    if (count > bytes.size()) return false;
    for (size_t i = 0; i < count; ++i) {
      data[i] = bytes.front();
      bytes.pop_front();
    }
    return true;
  }
};
class UARTDevice {
 public:
  void set_uart_parent(UARTComponent *parent) { parent_ = parent; }
  size_t available() { return parent_->available(); }
  bool read_array(uint8_t *data, size_t count) { return parent_->read_array(data, count); }
 protected:
  UARTComponent *parent_{nullptr};
};
}
