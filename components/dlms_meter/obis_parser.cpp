#include "obis_parser.h"

#define ARDUINOJSON_ENABLE_STD_STRING 1
#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace esphome {
namespace dlms_meter {

static bool consume_digits(const std::string &line, size_t &pos) {
  size_t start = pos;
  while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9')
    pos++;
  return pos > start;
}

static bool parse_obis_line(const std::string &line, std::string &obis, std::string &value, std::string &unit) {
  size_t pos = 0;
  if (!consume_digits(line, pos) || pos >= line.size() || line[pos++] != '-' || !consume_digits(line, pos) ||
      pos >= line.size() || line[pos++] != ':' || !consume_digits(line, pos) || pos >= line.size() ||
      line[pos++] != '.' || !consume_digits(line, pos) || pos >= line.size() || line[pos++] != '.' ||
      !consume_digits(line, pos) || pos >= line.size() || line[pos] != '(')
    return false;
  obis = line.substr(0, pos++);
  size_t end = line.find(')', pos);
  if (end == std::string::npos)
    return false;
  std::string raw = line.substr(pos, end - pos);
  size_t star = raw.find('*');
  value = raw.substr(0, star);
  unit = star == std::string::npos ? "" : raw.substr(star + 1, raw.find('*', star + 1) - star - 1);
  return true;
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static void copy_field(JsonObjectConst source, JsonObject target, const char *input, const char *output) {
  if (!source[input].is<JsonVariantConst>())
    return;
  JsonVariantConst value = source[input];
  if (value.is<const char *>()) {
    const char *text = value.as<const char *>();
    const char *p = text;
    while (*p >= '0' && *p <= '9')
      p++;
    if (p != text && *p == '.' && p[1] >= '0' && p[1] <= '9') {
      double number = std::strtod(text, nullptr);
      if (std::isfinite(number))
        target[output] = number;
      else
        target[output] = nullptr;
      return;
    }
  }
  target[output] = value;
}

static void copy_number(JsonObjectConst source, JsonObject target, const char *input, const char *output) {
  const char *value = source[input].as<const char *>();
  if (value == nullptr) {
    target[output] = nullptr;
    return;
  }
  char *end = nullptr;
  double number = std::strtod(value, &end);
  if (end == value || !std::isfinite(number))
    target[output] = nullptr;
  else
    target[output] = number;
}

static bool transform_obis(JsonObjectConst source, std::string &output) {
  JsonDocument document;
  JsonObject root = document.to<JsonObject>();
  static const struct {
    const char *input;
    const char *output;
    bool numeric;
  } fields[] = {
      {"timestamp", "timestamp", false},
      {"meter_id", "meter_id", false},
      {"energy_total_kwh", "energy_total_kwh", false},
      {"1-0_1_8_1", "energy_tariff_1_kwh", true},
      {"1-0_1_8_2", "energy_tariff_2_kwh", true},
      {"1-0_1_8_3", "energy_tariff_3_kwh", true},
      {"1-0_1_8_4", "energy_tariff_4_kwh", true},
      {"1-0_2_8_0", "energy_return_total_kwh", true},
      {"1-0_2_8_1", "energy_return_t1_kwh", true},
      {"1-0_2_8_2", "energy_return_t2_kwh", true},
      {"1-0_2_8_3", "energy_return_t3_kwh", true},
      {"1-0_2_8_4", "energy_return_t4_kwh", true},
      {"power_kw", "power_kw", false},
      {"1-0_3_7_0", "reactive_power_l1_kvar", true},
      {"1-0_4_7_0", "reactive_power_l2_kvar", true},
      {"voltage_l1_v", "voltage_l1_v", false},
      {"1-0_52_7_0", "voltage_l2_v", true},
      {"1-0_72_7_0", "voltage_l3_v", true},
      {"0-0_17_0_0", "meter_status", false},
      {"0-0_96_1_1", "meter_serial_hex", false},
      {"0-0_94_98_31", "meter_error_31", false},
      {"0-0_94_98_32", "meter_error_32", false},
      {"0-0_94_98_33", "meter_error_33", false},
      {"0-0_96_13_0", "meter_unknown_96_13_0", false},
  };
  for (const auto &field : fields) {
    if (field.numeric)
      copy_number(source, root, field.input, field.output);
    else
      copy_field(source, root, field.input, field.output);
  }
  if (document.overflowed())
    return false;
  output.clear();
  return serializeJson(document, output) > 0;
}

bool parse_obis_json(const uint8_t *text, size_t length, std::string &output) {
  if (text == nullptr || length == 0)
    return false;

  JsonDocument document;
  JsonObject root = document.to<JsonObject>();
  const std::string source(reinterpret_cast<const char *>(text), length);
  size_t offset = 0;
  size_t fields = 0;
  while (offset < source.size()) {
    size_t end = source.find('\n', offset);
    std::string line = source.substr(offset, end == std::string::npos ? end : end - offset);
    offset = end == std::string::npos ? source.size() : end + 1;
    std::string obis, value, unit;
    if (!parse_obis_line(line, obis, value, unit))
      continue;
    fields++;
    if (obis == "0-0:1.0.0") {
      if (value.size() < 12 ||
          !std::all_of(value.begin(), value.begin() + 12, [](char c) { return c >= '0' && c <= '9'; })) {
        return false;
      }
      root["timestamp"] = "20" + value.substr(0, 2) + "-" + value.substr(2, 2) + "-" + value.substr(4, 2) + "T" +
                          value.substr(6, 2) + ":" + value.substr(8, 2) + ":" + value.substr(10, 2);
    } else if (obis == "0-0:96.1.0") {
      std::string decoded;
      for (size_t i = 0; i + 1 < value.size(); i += 2) {
        int hi = hex_digit(value[i]);
        int lo = hex_digit(value[i + 1]);
        if (hi < 0 || lo < 0)
          break;
        decoded.push_back(static_cast<char>((hi << 4) | lo));
      }
      root["meter_id"] = decoded;
    } else if (obis == "1-0:1.8.0" || obis == "1-0:1.7.0" || obis == "1-0:32.7.0") {
      const char *name = obis == "1-0:1.8.0" ? "energy_total_kwh" : obis == "1-0:1.7.0" ? "power_kw" : "voltage_l1_v";
      char *number_end = nullptr;
      double number = std::strtod(value.c_str(), &number_end);
      if (number_end == value.c_str() || !std::isfinite(number)) {
        root[name] = nullptr;
      } else {
        root[name] = number;
      }
    } else {
      std::replace(obis.begin(), obis.end(), ':', '_');
      std::replace(obis.begin(), obis.end(), '.', '_');
      root[obis] = unit.empty() ? value : value + " " + unit;
    }
  }
  if (fields == 0 || document.overflowed())
    return false;
  return transform_obis(root, output);
}

}  // namespace dlms_meter
}  // namespace esphome
