#pragma once
#include <string>
#include <utility>
#include <vector>
namespace esphome::mqtt {
class MQTTClientComponent {
 public:
  bool connected = true;
  bool fail = false;
  std::vector<std::pair<std::string, std::string>> messages;
  bool is_connected() { return connected; }
  bool publish(const std::string &topic, const std::string &payload, int qos, bool retain) {
    if (fail || qos != 1 || !retain) return false;
    messages.emplace_back(topic, payload);
    return true;
  }
};
extern MQTTClientComponent *global_mqtt_client;
}
