#include "dlms_meter.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <string>

namespace esphome::mqtt { MQTTClientComponent *global_mqtt_client = nullptr; }

using Key = std::array<uint8_t, 16>;

std::string encrypt(const std::string &text, const Key &key, const Key &auth) {
  std::array<uint8_t, 17> aad{};
  aad[0] = 0x30;
  std::copy(auth.begin(), auth.end(), aad.begin() + 1);
  uint8_t iv[12]{}, tag[12];
  std::string cipher(text.size(), '\0');
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  int n = 0;
  assert(EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, key.data(), iv));
  assert(EVP_EncryptUpdate(ctx, nullptr, &n, aad.data(), aad.size()));
  assert(EVP_EncryptUpdate(ctx, reinterpret_cast<uint8_t *>(cipher.data()), &n,
                           reinterpret_cast<const uint8_t *>(text.data()), text.size()));
  assert(EVP_EncryptFinal_ex(ctx, reinterpret_cast<uint8_t *>(cipher.data()) + n, &n));
  assert(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag));
  EVP_CIPHER_CTX_free(ctx);
  size_t len = 17 + cipher.size();
  std::string out("/KFM\r\n\r\n\x00\x82", 10);
  out.push_back(static_cast<char>(len >> 8));
  out.push_back(static_cast<char>(len));
  out.push_back(static_cast<char>(aad[0]));
  out.append("\x00\x00\x00\x00", 4);
  out += cipher;
  out.append(reinterpret_cast<const char *>(tag), sizeof(tag));
  return out;
}

struct Fixture {
  esphome::uart::UARTComponent source;
  esphome::light::LightState light;
  esphome::mqtt::MQTTClientComponent mqtt;
  esphome::dlms_meter::DlmsMeter processor;
  Fixture(const Key &key, const Key &auth) {
    esphome::mqtt::global_mqtt_client = &mqtt;
    processor.set_uart_parent(&source);
    processor.set_status_light(&light);
    processor.set_encryption_key(key);
    processor.set_authentication_key(auth);
    processor.setup();
    loop();
  }
  void loop(int n = 1) { for (int i = 0; i < n; ++i) processor.loop(); }
  void feed(const std::string &data) { source.bytes.insert(source.bytes.end(), data.begin(), data.end()); }
  void verify(size_t count) {
    loop(160);
    assert(mqtt.messages.size() == count * 44);
    if (count) {
      assert(mqtt.messages[0].first == "homeassistant/sensor/ABC123/energy_total/config");
      assert(mqtt.messages[1].first == "homeassistant/sensor/ABC123/energy_total/state");
      assert(mqtt.messages[1].second == "123.456");
    }
  }
};

int main() {
  Key key{}, auth{};
  key.fill(1);
  auth.fill(2);
  std::string text = "0-0:96.1.0(414243313233)\n1-0:1.8.0(00123.456*kWh)\n#00\n";
  auto probe = encrypt(text, key, auth);
  const size_t cipher_offset = 17;
  const size_t pos = text.size() - 3;
  text[pos] ^= probe[cipher_offset + pos] ^ '/';
  text[pos + 1] ^= probe[cipher_offset + pos + 1] ^ 'K';
  auto frame = encrypt(text, key, auth);
  assert(frame.substr(cipher_offset + pos, 2) == "/K");

  for (size_t split = 0; split <= frame.size(); ++split) {
    Fixture fixture(key, auth);
    fixture.feed(frame.substr(0, split));
    fixture.loop(80);
    fixture.feed(frame.substr(split));
    fixture.verify(1);
  }
  {
    Fixture fixture(key, auth);
    fixture.feed("junk" + frame + "!1234\r\n" + frame);
    fixture.verify(2);
  }
  {
    Fixture fixture(key, auth);
    auto bad = frame;
    bad.back() ^= 1;
    fixture.feed(bad + frame);
    fixture.verify(1);
  }
  {
    Fixture fixture(key, auth);
    fixture.feed(frame.substr(0, 25));
    fixture.loop();
    fixture.source.connected = false;
    fixture.loop();
    fixture.source.connected = true;
    fixture.loop();
    fixture.feed(frame.substr(25) + frame);
    fixture.verify(1);
  }
  {
    Fixture fixture(key, auth);
    fixture.feed(frame.substr(0, 25));
    fixture.loop();
    fixture.mqtt.connected = false;
    fixture.loop();
    fixture.feed(frame.substr(25) + frame);
    fixture.loop();
    assert(fixture.source.connected && fixture.source.bytes.empty());
    fixture.mqtt.connected = true;
    fixture.loop();
    fixture.feed(frame);
    fixture.verify(1);
  }
  {
    Fixture fixture(key, auth);
    std::string malformed("/K\x00\x82\x00\x01", 6);
    fixture.feed(malformed + frame);
    fixture.verify(1);
  }
  {
    Fixture fixture(key, auth);
    std::string oversized("/K\x00\x82\xff\xff", 6);
    fixture.feed(oversized + frame);
    fixture.verify(1);
  }
  {
    Fixture fixture(key, auth);
    fixture.feed("/Kincomplete" + frame);
    fixture.verify(1);
  }
  {
    Fixture fixture(key, auth);
    fixture.feed(frame.substr(0, frame.size() - 1));
    fixture.verify(0);
    fixture.feed(frame.substr(frame.size() - 1));
    fixture.verify(1);
  }
  {
    Fixture fixture(key, auth);
    std::string large_text = text + "#" + std::string(32739 - text.size() - 1, 'x');
    auto large_frame = encrypt(large_text, key, auth);
    assert(large_frame.size() == 32768);
    fixture.feed(large_frame);
    fixture.verify(1);
  }
  {
    Fixture fixture(key, auth);
    fixture.mqtt.fail = true;
    fixture.feed(frame);
    fixture.loop(10);
    assert(fixture.mqtt.messages.empty());
    fixture.mqtt.fail = false;
    fixture.loop();
    fixture.feed(frame);
    fixture.verify(1);
  }
  std::cout << "PASS: encrypted embedded marker, all splits, GCM, recovery, MQTT and source resets\n";
}
