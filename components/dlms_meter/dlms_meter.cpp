#include "dlms_meter.h"
#include "obis_parser.h"

#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <mbedtls/gcm.h>

namespace esphome {
namespace dlms_meter {

static const char *const TAG = "dlms_meter";
void DlmsMeter::setup() {
  uint32_t now = millis();
  this->indicator_.start(now);
  this->update_status_();
}

void DlmsMeter::dump_config() {
  ESP_LOGCONFIG(TAG, "UART source -> MQTT Home Assistant discovery");
  ESP_LOGCONFIG(TAG, "  Frame start: /K; payload length framing; maximum frame size: %u bytes",
                static_cast<unsigned>(MAX_FRAME_SIZE));
}

void DlmsMeter::reset_pipeline_() {
  this->indicator_.disconnected();
  this->input_size_ = 0;
  this->input_offset_ = 0;
  this->sensors_.clear();
  this->reset_frame_();
}

void DlmsMeter::reset_frame_() {
  this->frame_size_ = 0;
  this->payload_marker_ = MAX_FRAME_SIZE;
  this->expected_size_ = 0;
  this->collecting_ = false;
  this->start_slash_ = false;
}

bool DlmsMeter::decrypt_frame_(size_t length, uint8_t *&plaintext, size_t &plaintext_length) {
  size_t offset = 0;
  while (offset + 1 < length && (this->buffer_[offset] != 0 || this->buffer_[offset + 1] != 0x82))
    offset++;
  if (offset + 1 >= length) {
    ESP_LOGW(TAG, "Cannot decrypt frame: 00 82 marker not found");
    return false;
  }
  if (length - offset < 4) {
    ESP_LOGW(TAG, "Cannot decrypt frame: truncated length");
    return false;
  }
  const size_t payload_length = (static_cast<size_t>(this->buffer_[offset + 2]) << 8) | this->buffer_[offset + 3];
  if (payload_length < 18 || payload_length > length - offset - 4) {
    ESP_LOGW(TAG, "Cannot decrypt frame: invalid or truncated payload");
    return false;
  }
  uint8_t *payload = this->buffer_ + offset + 4;
  plaintext = payload + 5;
  plaintext_length = payload_length - 17;
  uint8_t aad[17];
  aad[0] = payload[0];
  std::copy(this->authentication_key_.begin(), this->authentication_key_.end(), aad + 1);
  const uint8_t iv[12]{};
  mbedtls_gcm_context context;
  mbedtls_gcm_init(&context);
  int result = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, this->encryption_key_.data(), 128);
  if (result == 0) {
    result = mbedtls_gcm_auth_decrypt(&context, plaintext_length, iv, sizeof(iv), aad, sizeof(aad),
                                      plaintext + plaintext_length, 12, plaintext, plaintext);
  }
  mbedtls_gcm_free(&context);
  if (result != 0) {
    ESP_LOGW(TAG, "AES-GCM authentication or decryption failed (error %d)", result);
    return false;
  }
  return true;
}

void DlmsMeter::process_input_(size_t input_size) {
  while (this->input_offset_ < input_size) {
    uint8_t byte = this->input_[this->input_offset_++];
    if (!this->collecting_) {
      if (this->start_slash_ && byte == 'K') {
        this->buffer_[0] = '/';
        this->buffer_[1] = 'K';
        this->frame_size_ = 2;
        this->collecting_ = true;
        this->start_slash_ = false;
      } else {
        this->start_slash_ = byte == '/';
      }
      continue;
    }

    if (this->payload_marker_ == MAX_FRAME_SIZE && this->frame_size_ > 0 &&
        this->buffer_[this->frame_size_ - 1] == '/' && byte == 'K') {
      ESP_LOGW(TAG, "Incomplete frame header; resynchronizing");
      this->reset_frame_();
      this->buffer_[0] = '/';
      this->buffer_[1] = 'K';
      this->frame_size_ = 2;
      this->collecting_ = true;
      continue;
    }

    if (this->frame_size_ == MAX_FRAME_SIZE) {
      ESP_LOGW(TAG, "Frame exceeds %u bytes; discarding until next /K",
               static_cast<unsigned>(MAX_FRAME_SIZE));
      this->reset_frame_();
      this->start_slash_ = byte == '/';
      continue;
    }
    this->buffer_[this->frame_size_++] = byte;

    if (this->payload_marker_ == MAX_FRAME_SIZE && this->frame_size_ >= 2 &&
        this->buffer_[this->frame_size_ - 2] == 0 && byte == 0x82) {
      this->payload_marker_ = this->frame_size_ - 2;
    }
    if (this->payload_marker_ != MAX_FRAME_SIZE && this->expected_size_ == 0 &&
        this->frame_size_ >= this->payload_marker_ + 4) {
      size_t payload_length = (static_cast<size_t>(this->buffer_[this->payload_marker_ + 2]) << 8) |
                              this->buffer_[this->payload_marker_ + 3];
      if (payload_length < 18 || payload_length > MAX_FRAME_SIZE - this->payload_marker_ - 4) {
        ESP_LOGW(TAG, "Invalid source frame length; discarding frame");
        this->reset_frame_();
        this->start_slash_ = byte == '/';
        continue;
      }
      this->expected_size_ = this->payload_marker_ + 4 + payload_length;
    }
    if (this->expected_size_ != 0 && this->frame_size_ == this->expected_size_) {
      uint8_t *plaintext = nullptr;
      size_t plaintext_length = 0;
      if (!this->decrypt_frame_(this->frame_size_, plaintext, plaintext_length)) {
        this->reset_frame_();
        continue;
      }
      std::string json;
      if (!parse_obis_json(plaintext, plaintext_length, json)) {
        ESP_LOGW(TAG, "Cannot parse decrypted OBIS frame into JSON");
        this->reset_frame_();
        continue;
      }
      bool valid = this->sensors_.start(json);
      this->reset_frame_();
      if (!valid) {
        ESP_LOGW(TAG, "Invalid meter identifier or sensor data; discarding frame");
        continue;
      }
      return;
    }
  }
  this->input_size_ = 0;
  this->input_offset_ = 0;
}

void DlmsMeter::publish_sensor_() {
  std::string topic;
  std::string payload;
  if (!this->sensors_.next(topic, payload) ||
      !mqtt::global_mqtt_client->publish(topic, payload, 1, true)) {
    ESP_LOGW(TAG, "Sensor MQTT publication failed; discarding current data");
    this->reset_pipeline_();
    this->discard_on_reconnect_ = true;
    return;
  }
  this->sensors_.advance();
  if (!this->sensors_.pending())
    this->indicator_.success(millis());
}

void DlmsMeter::update_status_() {
  bool connected = mqtt::global_mqtt_client != nullptr && mqtt::global_mqtt_client->is_connected() &&
                   this->parent_->is_connected();
  IndicatorColor color = this->indicator_.color(millis(), connected);
  if (color == this->indicator_color_)
    return;
  auto call = this->status_light_->turn_on();
  call.set_brightness(0.3f);
  call.set_rgb(color == IndicatorColor::GREEN ? 0.0f : 1.0f,
               color == IndicatorColor::RED ? 0.0f : 1.0f, 0.0f);
  call.set_transition_length(0);
  call.set_publish(false);
  call.set_save(false);
  call.perform();
  this->indicator_color_ = color;
  ESP_LOGD(TAG, "Status LED: %s", color == IndicatorColor::GREEN ? "green" :
                                   color == IndicatorColor::YELLOW ? "yellow" : "red");
}

void DlmsMeter::read_source_() {
  if (this->input_offset_ < this->input_size_) {
    this->process_input_(this->input_size_);
    return;
  }
  size_t received = std::min(this->available(), sizeof(this->input_));
  if (received == 0)
    return;
  if (!this->read_array(this->input_, received)) {
    ESP_LOGW(TAG, "Cannot read source data; discarding current frame");
    this->reset_pipeline_();
    this->discard_on_reconnect_ = true;
    return;
  }
  this->input_size_ = received;
  this->input_offset_ = 0;
  this->process_input_(this->input_size_);
}

bool DlmsMeter::discard_source_input_() {
  size_t size = std::min(this->available(), sizeof(this->input_));
  if (size == 0)
    return false;
  if (!this->read_array(this->input_, size))
    ESP_LOGW(TAG, "Cannot discard buffered source data");
  return true;
}

void DlmsMeter::loop() {
  bool source_connected = this->parent_->is_connected();
  bool mqtt_connected = mqtt::global_mqtt_client != nullptr && mqtt::global_mqtt_client->is_connected();
  if (source_connected != this->source_connected_) {
    this->source_connected_ = source_connected;
    if (!source_connected) {
      ESP_LOGW(TAG, "Source disconnected; discarding incomplete data");
      this->reset_pipeline_();
    }
  }
  if (mqtt_connected != this->mqtt_connected_) {
    this->mqtt_connected_ = mqtt_connected;
    if (!mqtt_connected) {
      ESP_LOGW(TAG, "MQTT disconnected; discarding source data");
      this->reset_pipeline_();
      this->discard_on_reconnect_ = true;
    }
  }
  this->update_status_();
  if (!mqtt_connected || !source_connected) {
    this->discard_source_input_();
    return;
  }
  if (this->discard_on_reconnect_) {
    if (this->discard_source_input_())
      return;
    this->discard_on_reconnect_ = false;
  }
  if (this->sensors_.pending()) {
    this->publish_sensor_();
    return;
  }
  this->read_source_();
}

}  // namespace dlms_meter
}  // namespace esphome
