"""ESPHome external component: local eybond/SmartESS collector emulator.

Turns an ESP8266/ESP32 wired to an inverter (RS485 or TTL UART) into a virtual
eybond WiFi collector fully compatible with the ha-eybond-local integration:
UDP discovery, reverse TCP, heartbeats, AT replies and a transparent
payload <-> UART bridge. The bridge never talks to the SmartESS cloud.
"""

import re

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import pins
from esphome.components import uart
from esphome.const import CONF_ID
from esphome.core import CORE

CODEOWNERS = ["@groove-max"]
DEPENDENCIES = ["uart", "network"]

CONF_PN = "pn"
CONF_UDP_PORT = "udp_port"
CONF_SERVER_HOST = "server_host"
CONF_SERVER_PORT = "server_port"
CONF_HEARTBEAT_INTERVAL = "heartbeat_interval"
CONF_RESPONSE_GAP = "response_gap"
CONF_RESPONSE_TIMEOUT = "response_timeout"
CONF_COMMAND_SPACING = "command_spacing"
CONF_DEVCODE = "devcode"
CONF_COLLECTOR_ADDR = "collector_addr"
CONF_FLOW_CONTROL_PIN = "flow_control_pin"
CONF_STATUS_LED_PIN = "status_led_pin"
CONF_COM_LED_PIN = "com_led_pin"
CONF_BLE_PROVISIONING = "ble_provisioning"

eybond_collector_ns = cg.esphome_ns.namespace("eybond_collector")
EybondCollector = eybond_collector_ns.class_("EybondCollector", cg.Component, uart.UARTDevice)

_PN_RE = re.compile(r"[A-Z]\d{13}|[A-Z]\d{17}")


def _validate_pn(value):
    value = cv.string_strict(value).strip().upper()
    if not _PN_RE.fullmatch(value):
        raise cv.Invalid(
            "PN must be one upper-case letter followed by 13 or 17 digits "
            "(synthetic only, e.g. V00000200000000001)"
        )
    return value


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(EybondCollector),
            cv.Optional(CONF_PN): _validate_pn,
            cv.Optional(CONF_UDP_PORT, default=58899): cv.port,
            cv.Optional(CONF_SERVER_HOST): cv.string_strict,
            cv.Optional(CONF_SERVER_PORT, default=8899): cv.port,
            cv.Optional(CONF_HEARTBEAT_INTERVAL, default="60s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_RESPONSE_GAP, default="60ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_RESPONSE_TIMEOUT, default="3s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_COMMAND_SPACING, default="850ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_DEVCODE, default=0x0000): cv.hex_uint16_t,
            cv.Optional(CONF_COLLECTOR_ADDR, default=0x01): cv.hex_uint8_t,
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_STATUS_LED_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_COM_LED_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_BLE_PROVISIONING, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA),
    cv.only_with_arduino,
)


def _final_validate(config):
    """ble_provisioning needs ESP32 BLE and the esp32_ble_server GATT server."""
    if not config.get(CONF_BLE_PROVISIONING):
        return config
    if not CORE.is_esp32:
        raise cv.Invalid(
            "ble_provisioning is only supported on ESP32 (it needs on-chip BLE); "
            "ESP8266 and BK72xx/LibreTiny have no usable BLE server."
        )
    full_config = fv.full_config.get()
    if "esp32_ble_server" not in full_config:
        raise cv.Invalid(
            "ble_provisioning requires an 'esp32_ble_server:' entry in your YAML — "
            "it provides the BLE GATT server the bridge attaches to. Add an empty "
            "'esp32_ble_server:' block (see examples/esp32-ble.yaml)."
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_PN in config:
        cg.add(var.set_pn(config[CONF_PN]))
    cg.add(var.set_udp_port(config[CONF_UDP_PORT]))
    if CONF_SERVER_HOST in config:
        cg.add(var.set_static_server(config[CONF_SERVER_HOST], config[CONF_SERVER_PORT]))
    cg.add(var.set_heartbeat_interval(config[CONF_HEARTBEAT_INTERVAL].total_milliseconds))
    cg.add(var.set_response_gap(config[CONF_RESPONSE_GAP].total_milliseconds))
    cg.add(var.set_response_timeout(config[CONF_RESPONSE_TIMEOUT].total_milliseconds))
    cg.add(var.set_command_spacing(config[CONF_COMMAND_SPACING].total_milliseconds))
    cg.add(var.set_devcode(config[CONF_DEVCODE]))
    cg.add(var.set_collector_addr(config[CONF_COLLECTOR_ADDR]))
    if CONF_FLOW_CONTROL_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
        cg.add(var.set_flow_control_pin(pin))
    if CONF_STATUS_LED_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_STATUS_LED_PIN])
        cg.add(var.set_status_led_pin(pin))
    if CONF_COM_LED_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_COM_LED_PIN])
        cg.add(var.set_com_led_pin(pin))
    if config[CONF_BLE_PROVISIONING]:
        cg.add_define("USE_EYBOND_BLE")
        cg.add(var.set_ble_provisioning(True))
