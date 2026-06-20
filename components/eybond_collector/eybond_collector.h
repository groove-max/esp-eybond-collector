#pragma once

#ifdef USE_ARDUINO

#include <functional>
#include <memory>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"
#include "esphome/components/uart/uart.h"

#include "core.h"
#include "ble_provisioning.h"

#if defined(USE_ESP8266)
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#elif defined(USE_ESP32)
#include <WiFi.h>
#else
#include <WiFiClient.h>
#include <WiFiUdp.h>
#endif

#ifdef USE_EYBOND_BLE
#include "esphome/components/esp32_ble_server/ble_characteristic.h"
#include <vector>
#endif

namespace esphome {
namespace eybond_collector {

// NVS-persisted reverse-TCP server endpoint (HA address). Lets the bridge
// reconnect to the same HA after a reboot without waiting for a discovery
// broadcast; discovery still overrides it live.
struct PersistedEndpoint {
  char host[64];
  uint16_t port;
} PACKED;

static const uint32_t ENDPOINT_PREF_HASH = 0xE7B0'DC01;

class EybondCollector : public Component, public uart::UARTDevice, private eybond::CollectorCore::Actions
#ifdef USE_EYBOND_BLE
                        ,
                        private eybond::BleProvisioning::Actions
#endif
{
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // Run just before Wi-Fi so the component can derive the virtual collector PN
  // and replace the default fallback AP SSID before ESPHome starts SoftAP.
  float get_setup_priority() const override { return setup_priority::WIFI + 0.5f; }

  void set_pn(const std::string &pn) { pn_override_ = pn; }
  void set_udp_port(uint16_t port) { udp_port_ = port; }
  void set_static_server(const std::string &host, uint16_t port) {
    static_server_host_ = host;
    static_server_port_ = port;
  }
  void set_heartbeat_interval(uint32_t ms) { core_config_.heartbeat_interval_ms = ms; }
  void set_response_gap(uint32_t ms) { core_config_.uart_gap_ms = ms; }
  void set_response_timeout(uint32_t ms) { core_config_.uart_timeout_ms = ms; }
  void set_command_spacing(uint32_t ms) { core_config_.command_spacing_ms = ms; }
  void set_devcode(uint16_t devcode) { core_config_.heartbeat_devcode = devcode; }
  void set_collector_addr(uint8_t addr) { core_config_.collector_addr = addr; }
  void set_flow_control_pin(GPIOPin *pin) { flow_control_pin_ = pin; }
  void set_status_led_pin(GPIOPin *pin) { status_led_pin_ = pin; }
  void set_ble_provisioning(bool enabled) { ble_enabled_ = enabled; }

  // Runtime inverter-UART re-baud (select entity, AT+UART=, FC=3 param 34).
  // Returns false when the rate is rejected.
  bool apply_baud_rate(uint32_t baud);
  static bool baud_supported_(uint32_t baud);
  bool reconfigure_uart_(uint32_t baud, uint8_t data_bits, uint8_t stop_bits,
                         uart::UARTParityOptions parity);
  uint32_t current_baud_rate() const;
  void set_baud_listener(std::function<void(uint32_t)> listener) { baud_listener_ = std::move(listener); }

 protected:
  // eybond::CollectorCore::Actions
  void udp_reply(const uint8_t *data, size_t len) override;
  void tcp_connect(const std::string &host, uint16_t port) override;
  void tcp_send(const uint8_t *data, size_t len) override;
  void tcp_close() override;
  void uart_send(const uint8_t *data, size_t len) override;
  std::string wifi_rssi() override;
  std::string time_string() override { return ""; }
  bool query_param(uint8_t parameter, std::string *out) override;
  uint8_t set_param(uint8_t parameter, const std::string &value) override;
  void apply_uart(const std::string &value) override;
  void on_server_endpoint_changed(const std::string &host, uint16_t port) override;
  void log(const char *message) override;

  void process_pending_connect_();
  void process_wifi_apply_(uint32_t now);
  void poll_wifi_scan_(uint32_t now);
  void configure_fallback_ap_(const std::string &pn);
  void note_activity_(uint32_t now);
  void update_status_led_(uint32_t now);
  void write_status_led_(bool on);
  std::string uart_settings_string_() const;

#ifdef USE_EYBOND_BLE
  // eybond::BleProvisioning::Actions
  std::string ble_fw_version() override { return ble_fw_version_; }
  std::string ble_at_version() override { return ble_at_version_; }
  void ble_apply_wifi(const std::string &ssid, const std::string &password) override;
  bool ble_wifi_connected() override;
  std::vector<eybond::BleWifiNetwork> ble_wifi_scan() override;

  // Build the SmartESS-compatible GATT provisioning service on the shared
  // esp32_ble_server. Called lazily from loop() because esp32_ble_server sets
  // global_ble_server in its own setup(), which runs after this component's.
  void setup_ble_();
  void ble_feed_(const std::vector<uint8_t> &data);  // GATT write -> rx buffer
  void process_ble_(uint32_t now);                   // settle, parse, notify
#endif

  std::unique_ptr<eybond::CollectorCore> core_;
  eybond::CoreConfig core_config_{};

  WiFiUDP udp_;
  WiFiClient tcp_;
  bool udp_started_{false};
  bool tcp_up_{false};

  // tcp_connect is requested by the core mid-loop and executed afterwards,
  // because WiFiClient::connect blocks and the core must not be re-entered.
  bool connect_pending_{false};
  std::string pending_host_;
  uint16_t pending_port_{0};

  GPIOPin *flow_control_pin_{nullptr};
  GPIOPin *status_led_pin_{nullptr};
  bool status_led_state_{true};
  uint32_t status_led_activity_until_ms_{0};
  uint32_t status_led_last_toggle_ms_{0};
  std::string pn_override_;
  uint16_t udp_port_{58899};
  std::string static_server_host_;
  uint16_t static_server_port_{8899};

  // WiFi reconfiguration via the integration's collector parameter flow:
  // FC=3 writes stage ssid (41) and password (43); FC=3 param 29 ("apply")
  // commits them. The commit is deferred to loop() so the FC=3 response
  // reaches HA before the STA reconnects.
  std::string pending_wifi_ssid_;
  std::string pending_wifi_password_;
  bool wifi_apply_requested_{false};
  uint32_t wifi_apply_at_ms_{0};

  // Wi-Fi scan cache for FC=2 param 49 ("[ssid,rssi][ssid2,rssi2]...").
  std::string wifi_scan_cache_;
  bool wifi_scan_running_{false};

  // Notified after a successful runtime re-baud (keeps the select entity in sync).
  std::function<void(uint32_t)> baud_listener_;

  // Server-endpoint write (FC=3 param 21 staged, committed by param 29) and its
  // NVS persistence.
  std::string pending_endpoint_;
  ESPPreferenceObject endpoint_pref_;
  bool suppress_endpoint_persist_{false};

  // Cached scan results in structured form (the source for both the FC=2
  // param-49 string cache and the BLE AT+INTPARA49? list).
  std::vector<eybond::BleWifiNetwork> wifi_scan_entries_;

  // BLE Wi-Fi provisioning (ESP32 only). Emulates the factory collector's
  // SmartESS BLE pairing so the integration can provision the bridge over
  // Bluetooth. Disabled unless ble_provisioning: true and built for ESP32.
  bool ble_enabled_{false};
#ifdef USE_EYBOND_BLE
  std::unique_ptr<eybond::BleProvisioning> ble_;
  esp32_ble_server::BLECharacteristic *ble_notify_char_{nullptr};
  std::string ble_pn_;
  std::string ble_fw_version_;
  std::string ble_at_version_;
  // GATT writes can arrive fragmented across ATT MTUs; accumulate and process
  // after a short quiet period (the client always waits for our notify before
  // sending the next command, so a settle window cleanly frames one command).
  std::string ble_rx_;
  uint32_t ble_rx_last_ms_{0};
  bool ble_rx_pending_{false};
  // Wi-Fi scan refresh is deferred to after the BLE notify (see ble_wifi_scan /
  // process_ble_) so it never contends with the notification under coexistence.
  bool ble_scan_pending_{false};
  uint32_t ble_last_scan_ms_{0};
  static const uint32_t BLE_RX_SETTLE_MS = 60;
#endif
};

}  // namespace eybond_collector
}  // namespace esphome

#endif  // USE_ARDUINO
