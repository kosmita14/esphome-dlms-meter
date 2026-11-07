#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace esphome {
namespace dlms_meter {

bool parse_obis_json(const uint8_t *text, size_t length, std::string &output);

}  // namespace dlms_meter
}  // namespace esphome
