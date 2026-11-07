#include "meter_sensors.h"

namespace esphome {
namespace dlms_meter {

struct Sensor {
  const char *id;
  const char *name;
  const char *field;
  const char *unit;
  const char *device_class;
  const char *state_class;
};

static const Sensor SENSORS[] = {
    {"energy_total", "Energy Total", "energy_total_kwh", "kWh", "energy", "total_increasing"},
    {"energy_tariff_1", "Energy Tariff 1", "energy_tariff_1_kwh", "kWh", "energy", "total_increasing"},
    {"energy_tariff_2", "Energy Tariff 2", "energy_tariff_2_kwh", "kWh", "energy", "total_increasing"},
    {"energy_tariff_3", "Energy Tariff 3", "energy_tariff_3_kwh", "kWh", "energy", "total_increasing"},
    {"energy_tariff_4", "Energy Tariff 4", "energy_tariff_4_kwh", "kWh", "energy", "total_increasing"},
    {"energy_return_total", "Energy Returned Total", "energy_return_total_kwh", "kWh", "energy", "total_increasing"},
    {"energy_return_t1", "Energy Returned T1", "energy_return_t1_kwh", "kWh", "energy", "total_increasing"},
    {"energy_return_t2", "Energy Returned T2", "energy_return_t2_kwh", "kWh", "energy", "total_increasing"},
    {"energy_return_t3", "Energy Returned T3", "energy_return_t3_kwh", "kWh", "energy", "total_increasing"},
    {"energy_return_t4", "Energy Returned T4", "energy_return_t4_kwh", "kWh", "energy", "total_increasing"},
    {"power", "Power", "power_kw", "kW", "power", "measurement"},
    {"reactive_power_l1", "Reactive Power L1", "reactive_power_l1_kvar", "kvar", "reactive_power", "measurement"},
    {"reactive_power_l2", "Reactive Power L2", "reactive_power_l2_kvar", "kvar", "reactive_power", "measurement"},
    {"voltage_l1", "Voltage L1", "voltage_l1_v", "V", "voltage", "measurement"},
    {"voltage_l2", "Voltage L2", "voltage_l2_v", "V", "voltage", "measurement"},
    {"voltage_l3", "Voltage L3", "voltage_l3_v", "V", "voltage", "measurement"},
    {"meter_status", "Meter Status", "meter_status", "", "", ""},
    {"meter_error_31", "Meter Error 31", "meter_error_31", "", "", ""},
    {"meter_error_32", "Meter Error 32", "meter_error_32", "", "", ""},
    {"meter_error_33", "Meter Error 33", "meter_error_33", "", "", ""},
    {"meter_serial_hex", "Meter Serial HEX", "meter_serial_hex", "", "", ""},
    {"timestamp", "Timestamp", "timestamp", "", "", ""},
};
static constexpr size_t SENSOR_COUNT = sizeof(SENSORS) / sizeof(SENSORS[0]);

bool MeterSensors::start(const std::string &json) {
  this->clear();
  if (deserializeJson(this->data_, json) || !this->data_.is<JsonObject>() || this->data_.overflowed())
    return false;
  JsonVariantConst id = this->data_["meter_id"];
  if (id.isNull() || (id.is<const char *>() && id.as<std::string>().empty())) {
    this->meter_id_ = "han_meter";
  } else if (id.is<const char *>()) {
    this->meter_id_ = id.as<std::string>();
  } else if (id.is<double>()) {
    serializeJson(id, this->meter_id_);
  } else {
    return false;
  }
  if (this->meter_id_.find_first_of("/+#") != std::string::npos ||
      this->meter_id_.find('\0') != std::string::npos)
    return false;
  this->active_ = true;
  return true;
}

bool MeterSensors::pending() const { return this->active_ && this->position_ < SENSOR_COUNT * 2; }

bool MeterSensors::next(std::string &topic, std::string &payload) const {
  if (!this->pending())
    return false;
  const Sensor &sensor = SENSORS[this->position_ / 2];
  std::string base = this->topic_prefix_ + "/" + this->meter_id_ + "/" + sensor.id;
  payload.clear();
  if (this->position_ % 2 == 0) {
    topic = base + "/config";
    JsonDocument config;
    config["name"] = sensor.name;
    config["unique_id"] = this->meter_id_ + "_" + sensor.id;
    config["state_topic"] = base + "/state";
    if (*sensor.unit) {
      config["unit_of_measurement"] = sensor.unit;
      config["device_class"] = sensor.device_class;
      config["state_class"] = sensor.state_class;
      config["value_template"] = "{{ none if value in ['null', 'undefined'] else value }}";
    }
    JsonObject device = config["device"].to<JsonObject>();
    device["identifiers"].to<JsonArray>().add(this->meter_id_);
    device["name"] = "HAN Meter";
    device["manufacturer"] = "Kaifa";
    return !config.overflowed() && serializeJson(config, payload) > 0;
  }
  topic = base + "/state";
  JsonVariantConst value = this->data_[sensor.field];
  if (!value.is<JsonVariantConst>()) {
    payload = "undefined";
  } else if (value.isNull()) {
    payload = "null";
  } else if (value.is<const char *>()) {
    payload = value.as<std::string>();
  } else {
    serializeJson(value, payload);
  }
  return true;
}

void MeterSensors::advance() {
  if (this->pending())
    this->position_++;
  if (!this->pending())
    this->clear();
}

void MeterSensors::clear() {
  this->data_.clear();
  this->meter_id_.clear();
  this->position_ = 0;
  this->active_ = false;
}

}  // namespace dlms_meter
}  // namespace esphome
