import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, uart

CODEOWNERS = []
DEPENDENCIES = ["uart", "mqtt", "light"]
AUTO_LOAD = ["json"]

dlms_meter_ns = cg.esphome_ns.namespace("dlms_meter")
DlmsMeter = dlms_meter_ns.class_("DlmsMeter", cg.Component, uart.UARTDevice)

CONF_ENCRYPTION_KEY = "encryption_key"
CONF_AUTHENTICATION_KEY = "authentication_key"
CONF_STATUS_LIGHT = "status_light"
CONF_MQTT_TOPIC_PREFIX = "mqtt_topic_prefix"


def validate_key(value):
    value = cv.string_strict(value)
    if len(value) != 32 or any(char not in "0123456789abcdefABCDEF" for char in value):
        raise cv.Invalid("Key must contain exactly 32 hexadecimal characters")
    return value


def validate_topic_prefix(value):
    value = cv.publish_topic(value)
    if value.endswith("/"):
        raise cv.Invalid("MQTT topic prefix must not end with '/'")
    if len(value.encode("utf-8")) > 1024:
        raise cv.Invalid("MQTT topic prefix must not exceed 1024 bytes")
    return value


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DlmsMeter),
        cv.Required(CONF_ENCRYPTION_KEY): cv.sensitive(validate_key),
        cv.Required(CONF_AUTHENTICATION_KEY): cv.sensitive(validate_key),
        cv.Required(CONF_STATUS_LIGHT): cv.use_id(light.LightState),
        cv.Optional(CONF_MQTT_TOPIC_PREFIX, default="homeassistant/sensor"): validate_topic_prefix,
    }
).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config["id"])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_mqtt_topic_prefix(config[CONF_MQTT_TOPIC_PREFIX]))
    cg.add(var.set_encryption_key(cg.ArrayInitializer(*bytes.fromhex(config[CONF_ENCRYPTION_KEY]))))
    cg.add(var.set_authentication_key(cg.ArrayInitializer(*bytes.fromhex(config[CONF_AUTHENTICATION_KEY]))))
    cg.add(var.set_status_light(await cg.get_variable(config[CONF_STATUS_LIGHT])))
