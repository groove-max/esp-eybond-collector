#ifdef USE_ARDUINO

#include "eybond_collector.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include <Arduino.h>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

#ifdef USE_EYBOND_BLE
#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/esp32_ble/ble_uuid.h"
#include "esphome/components/esp32_ble_server/ble_server.h"
#include "esphome/components/esp32_ble_server/ble_service.h"
#include "esphome/components/esp32_ble_server/ble_2902.h"
#include <esp_gatt_common_api.h>  // esp_ble_gatt_set_local_mtu
#include <esp_gap_ble_api.h>      // esp_ble_tx_power_set
#endif

#ifdef USE_ESP8266
#include <ESP8266WiFi.h>
#else
// ESP32 (esp-arduino) and BK72xx/RTL87xx (LibreTiny) both provide the ESP32-style
// Arduino WiFi/WiFiClient/WiFiUDP classes via <WiFi.h>.
#include <WiFi.h>
#endif

namespace esphome {
namespace eybond_collector {

static const char *const TAG = "eybond_collector";

void EybondCollector::setup() {
  if (status_led_pin_ != nullptr) {
    status_led_pin_->setup();
    this->write_status_led_(false);
  }

  baud_pref_ = global_preferences->make_preference<uint32_t>(BAUD_PREF_HASH);
  this->restore_persisted_baud_rate_();

  eybond::CollectorProfile profile;
  uint8_t mac[6];
  get_mac_address_raw(mac);
  if (!pn_override_.empty()) {
    profile.pn = pn_override_;
  } else {
    profile.pn = eybond::synthesize_pn(mac);
  }
  profile.uart = this->uart_settings_string_();
  profile.vdtu = eybond::build_vdtu_capabilities(profile, core_config_);

  core_ = std::unique_ptr<eybond::CollectorCore>(
      new eybond::CollectorCore(this, profile, core_config_));

#ifdef USE_EYBOND_BLE
  // Stash the identity the BLE provisioning service advertises and reports; the
  // GATT service itself is created lazily in loop() (see setup_ble_).
  ble_pn_ = profile.pn;
  ble_fw_version_ = profile.firmware_version;
  ble_at_version_ = profile.at_version;
#endif

  this->configure_fallback_ap_(profile.pn);

  // A runtime-written server endpoint (HA "HA only" bind / move to another HA)
  // is persisted in NVS and takes priority over the YAML static_server option,
  // mirroring how ESPHome treats a saved Wi-Fi STA over the YAML default.
  // Incoming discovery still overrides it live.
  endpoint_pref_ = global_preferences->make_preference<PersistedEndpoint>(ENDPOINT_PREF_HASH);
  PersistedEndpoint saved{};
  bool restored = false;
  if (endpoint_pref_.load(&saved) && saved.host[0] != '\0' && saved.port != 0) {
    saved.host[sizeof(saved.host) - 1] = '\0';
    suppress_endpoint_persist_ = true;  // avoid rewriting the same value at boot
    core_->set_server_endpoint(std::string(saved.host), saved.port, millis());
    suppress_endpoint_persist_ = false;
    restored = true;
    ESP_LOGI(TAG, "Restored persisted server endpoint %s:%u", saved.host, saved.port);
  }
  if (!restored && !static_server_host_.empty()) {
    core_->set_server_endpoint(static_server_host_, static_server_port_, millis());
  }
}

void EybondCollector::loop() {
  if (core_ == nullptr) {
    return;
  }
  const uint32_t now = millis();

#ifdef USE_EYBOND_BLE
  // BLE provisioning must run even with no network — that is its whole point
  // (bring the bridge onto Wi-Fi over Bluetooth), so it precedes the
  // network-down early return below.
  if (ble_enabled_) {
    if (ble_ == nullptr) {
      this->setup_ble_();  // lazy: retries until esp32_ble_server is up
    }
    this->process_ble_(now);
    this->poll_wifi_scan_(now);  // keep the scan cache fresh for AT+INTPARA49?
  }
#endif

  if (!network::is_connected()) {
    if (tcp_up_) {
      tcp_.stop();
      tcp_up_ = false;
      core_->on_tcp_closed(now);
    }
    this->process_reboot_(now);
    this->update_status_led_(now);
    return;
  }

  if (!udp_started_) {
    udp_started_ = udp_.begin(udp_port_) != 0;
    if (udp_started_) {
      ESP_LOGI(TAG, "Discovery listener on UDP %u", udp_port_);
    }
  }

  // UDP discovery datagrams
  if (udp_started_) {
    int packet_len = udp_.parsePacket();
    while (packet_len > 0) {
      uint8_t buffer[128];
      const int read_len = udp_.read(buffer, sizeof(buffer));
      if (read_len > 0) {
        // udp_reply() inside the core call replies to udp_.remoteIP()/remotePort()
        core_->on_udp_datagram(buffer, static_cast<size_t>(read_len), now);
      }
      packet_len = udp_.parsePacket();
    }
  }

  // TCP link state + inbound data
  if (tcp_up_ && !tcp_.connected()) {
    tcp_.stop();
    tcp_up_ = false;
    ESP_LOGW(TAG, "Server connection lost");
    core_->on_tcp_closed(now);
  }
  if (tcp_up_) {
    int available = tcp_.available();
    while (available > 0) {
      uint8_t buffer[128];
      const int chunk = available > static_cast<int>(sizeof(buffer)) ? sizeof(buffer) : available;
      const int read_len = tcp_.read(buffer, chunk);
      if (read_len <= 0) {
        break;
      }
      this->note_activity_(now);
      core_->on_tcp_data(buffer, static_cast<size_t>(read_len), now);
      available = tcp_.available();
    }
  }

  // Inverter UART -> core
  while (this->available() > 0) {
    uint8_t byte;
    if (!this->read_byte(&byte)) {
      break;
    }
    this->note_activity_(millis());
    core_->on_uart_data(&byte, 1, millis());
  }

  core_->loop(millis());
  this->process_pending_connect_();
  this->process_wifi_apply_(millis());
  this->process_reboot_(millis());
  this->poll_wifi_scan_(millis());
  this->update_status_led_(millis());
}

bool EybondCollector::query_param(uint8_t parameter, std::string *out) {
  switch (parameter) {
    case 6:  // hardware_version
#if defined(USE_ESP8266)
      *out = "ESP8266";
#elif defined(USE_LIBRETINY)
      *out = "BK72xx/RTL87xx";
#else
      *out = "ESP32";
#endif
      return true;
    case 16: {  // local_ip_address (IPAddress::toString() is absent on LibreTiny)
      const IPAddress ip = WiFi.localIP();
      char buffer[16];
      std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
      *out = buffer;
      return true;
    }
    case 30:  // reboot_required: "1" while a staged WiFi apply is pending
      *out = wifi_apply_requested_ ? "1" : "0";
      return true;
    case 41:  // router_ssid (current STA network)
      *out = WiFi.SSID().c_str();
      return true;
    case 48:  // network_diagnostics; the integration extracts the RSSI from text
      *out = this->wifi_rssi();
      return true;
    case 49: {  // wifi_scan_list: "[ssid,rssi][ssid2,rssi2]..."
      if (!wifi_scan_running_) {
        WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
        wifi_scan_running_ = true;
      }
      if (wifi_scan_cache_.empty()) {
        // Scan still in flight: report at least the network we are on.
        *out = std::string("[") + WiFi.SSID().c_str() + "," + this->wifi_rssi() + "]";
      } else {
        *out = wifi_scan_cache_;
      }
      return true;
    }
    default:
      return false;
  }
}

uint8_t EybondCollector::set_param(uint8_t parameter, const std::string &value) {
  if (parameter == 34) {  // serial_baudrate
    const uint32_t baud = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
    return this->apply_baud_rate(baud) ? 0 : 1;
  }
  if (parameter == 21) {  // domain_address_1: staged server endpoint
    pending_endpoint_ = value;
    return 0;
  }
  if (parameter == 29) {  // system_operation: commit staged changes or restart
    bool had_staged = !pending_endpoint_.empty();
#ifdef USE_WIFI
    had_staged = had_staged || !pending_wifi_ssid_.empty() || !pending_wifi_password_.empty();
#endif
    bool committed = false;
    if (!pending_endpoint_.empty()) {
      std::string host;
      uint16_t port = 0;
      if (eybond::parse_server_endpoint(pending_endpoint_, &host, &port) && core_ != nullptr) {
        core_->set_server_endpoint(host, port, millis());  // persists via callback
        committed = true;
      }
      pending_endpoint_.clear();
    }
#ifdef USE_WIFI
    if (!pending_wifi_ssid_.empty()) {
      // Defer the Wi-Fi reconnect so the FC=3 response reaches HA first.
      wifi_apply_requested_ = true;
      wifi_apply_at_ms_ = millis() + 750;
      committed = true;
    }
#endif
    if (committed) {
      return 0;
    }
    if (had_staged) {
      return 1;  // staged but invalid/incomplete; do not reinterpret as restart
    }
    this->request_reboot_(millis());
    return 0;
  }
#ifdef USE_WIFI
  switch (parameter) {
    case 41:  // target router SSID (staged)
      pending_wifi_ssid_ = value;
      return 0;
    case 43:  // target router password (staged)
      pending_wifi_password_ = value;
      return 0;
    default:
      break;
  }
#endif
  (void) value;
  return 1;
}

void EybondCollector::on_server_endpoint_changed(const std::string &host, uint16_t port) {
  if (suppress_endpoint_persist_) {
    return;  // restoring from NVS at boot; don't rewrite the same value
  }
  PersistedEndpoint saved{};
  std::snprintf(saved.host, sizeof(saved.host), "%s", host.c_str());
  saved.port = port;
  endpoint_pref_.save(&saved);
  global_preferences->sync();
  ESP_LOGI(TAG, "Persisted server endpoint %s:%u", saved.host, static_cast<unsigned>(port));
}

bool EybondCollector::baud_supported_(uint32_t baud) {
  static const uint32_t SUPPORTED[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
  for (uint32_t rate : SUPPORTED) {
    if (rate == baud) {
      return true;
    }
  }
  return false;
}

// Apply UART settings to the hardware at runtime. Returns true when the platform
// supports it (ESP8266/ESP32 via load_settings). LibreTiny's UARTComponent has no
// load_settings(), so runtime reconfiguration is unsupported there: the call only
// "succeeds" when the requested values already match the YAML-configured ones.
bool EybondCollector::reconfigure_uart_(uint32_t baud, uint8_t data_bits, uint8_t stop_bits,
                                        uart::UARTParityOptions parity) {
#if defined(USE_ESP8266) || defined(USE_ESP32)
  auto *u = this->parent_;
  bool changed = false;
  if (u->get_baud_rate() != baud) { u->set_baud_rate(baud); changed = true; }
  if (u->get_data_bits() != data_bits) { u->set_data_bits(data_bits); changed = true; }
  if (u->get_stop_bits() != stop_bits) { u->set_stop_bits(stop_bits); changed = true; }
  if (u->get_parity() != parity) { u->set_parity(parity); changed = true; }
  if (changed) {
    u->load_settings(false);
    ESP_LOGI(TAG, "Inverter UART re-configured to %s", this->uart_settings_string_().c_str());
  }
  return true;
#else
  auto *u = this->parent_;
  const bool already = u->get_baud_rate() == baud && u->get_data_bits() == data_bits &&
                       u->get_stop_bits() == stop_bits && u->get_parity() == parity;
  if (!already) {
    ESP_LOGW(TAG, "Runtime UART reconfiguration not supported on this platform; set it in YAML");
  }
  return already;
#endif
}

void EybondCollector::apply_uart(const std::string &value) {
  // AT+UART=<baud,data,stop,parity> (e.g. "9600,8,1,EVEN"). Apply the full framing.
  eybond::UartSettings s;
  if (!eybond::parse_uart_settings(value, &s) || !this->baud_supported_(s.baud)) {
    return;  // malformed/unsupported; the AT reply is already W000 (factory mirror)
  }
  const uart::UARTParityOptions parity = s.parity == eybond::UartParity::EVEN ? uart::UART_CONFIG_PARITY_EVEN
                                         : s.parity == eybond::UartParity::ODD ? uart::UART_CONFIG_PARITY_ODD
                                                                               : uart::UART_CONFIG_PARITY_NONE;
  if (!this->reconfigure_uart_(s.baud, s.data_bits, s.stop_bits, parity)) {
    return;
  }
  this->persist_baud_rate_(s.baud);
  if (core_ != nullptr) {
    core_->update_uart_description(this->uart_settings_string_());
  }
  if (baud_listener_) {
    baud_listener_(s.baud);
  }
}

bool EybondCollector::apply_baud_rate(uint32_t baud) {
  if (!this->baud_supported_(baud)) {
    ESP_LOGW(TAG, "Rejected unsupported baud rate %u", static_cast<unsigned>(baud));
    return false;
  }
  auto *u = this->parent_;
  if (!this->reconfigure_uart_(baud, u->get_data_bits(), u->get_stop_bits(), u->get_parity())) {
    return false;
  }
  this->persist_baud_rate_(baud);
  if (core_ != nullptr) {
    core_->update_uart_description(this->uart_settings_string_());
  }
  if (baud_listener_) {
    baud_listener_(baud);
  }
  return true;
}

uint32_t EybondCollector::current_baud_rate() const { return this->parent_->get_baud_rate(); }

void EybondCollector::restore_persisted_baud_rate_() {
  uint32_t saved = 0;
  if (!baud_pref_.load(&saved) || saved == 0) {
    return;
  }
  if (!this->baud_supported_(saved)) {
    ESP_LOGW(TAG, "Ignoring unsupported persisted inverter UART baud rate %u", static_cast<unsigned>(saved));
    return;
  }
  auto *u = this->parent_;
  if (!this->reconfigure_uart_(saved, u->get_data_bits(), u->get_stop_bits(), u->get_parity())) {
    ESP_LOGW(TAG, "Persisted inverter UART baud rate %u cannot be applied on this platform",
             static_cast<unsigned>(saved));
    return;
  }
  ESP_LOGI(TAG, "Restored inverter UART baud rate %u", static_cast<unsigned>(saved));
}

void EybondCollector::persist_baud_rate_(uint32_t baud) {
  if (!baud_pref_.save(&baud)) {
    ESP_LOGW(TAG, "Failed to persist inverter UART baud rate %u", static_cast<unsigned>(baud));
    return;
  }
  global_preferences->sync();
}

void EybondCollector::process_wifi_apply_(uint32_t now) {
#ifdef USE_WIFI
  if (!wifi_apply_requested_ || static_cast<int32_t>(now - wifi_apply_at_ms_) < 0) {
    return;
  }
  wifi_apply_requested_ = false;
  ESP_LOGI(TAG, "Applying new WiFi credentials (ssid length %u)",
           static_cast<unsigned>(pending_wifi_ssid_.size()));
  wifi::global_wifi_component->save_wifi_sta(pending_wifi_ssid_, pending_wifi_password_);
  pending_wifi_ssid_.clear();
  pending_wifi_password_.clear();
#else
  (void) now;
#endif
}

void EybondCollector::request_reboot_(uint32_t now) {
  reboot_requested_ = true;
  reboot_at_ms_ = now + 1500;
  ESP_LOGI(TAG, "Collector restart requested; rebooting after response flush");
}

void EybondCollector::process_reboot_(uint32_t now) {
  if (!reboot_requested_ || static_cast<int32_t>(now - reboot_at_ms_) < 0) {
    return;
  }
  reboot_requested_ = false;
  ESP_LOGI(TAG, "Restarting collector");
  ESP.restart();
}

void EybondCollector::poll_wifi_scan_(uint32_t now) {
  (void) now;
  if (!wifi_scan_running_) {
    return;
  }
  const int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) {
    return;
  }
  wifi_scan_running_ = false;
  if (count <= 0) {
    return;  // failed or empty: keep whatever cache we had
  }
  std::vector<eybond::BleWifiNetwork> entries;
  std::string cache;
  int emitted = 0;
  for (int i = 0; i < count && emitted < 10; i++) {
    const std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty() || ssid.find_first_of(",[]") != std::string::npos) {
      continue;  // would break the [ssid,rssi] framing
    }
    const int rssi = static_cast<int>(WiFi.RSSI(i));
    entries.push_back(eybond::BleWifiNetwork{ssid, rssi});
    char entry[64];
    std::snprintf(entry, sizeof(entry), "[%s,%d]", ssid.c_str(), rssi);
    cache += entry;
    emitted++;
  }
  WiFi.scanDelete();
  if (!entries.empty()) {
    wifi_scan_entries_ = entries;
  }
  if (!cache.empty()) {
    wifi_scan_cache_ = cache;
  }
  ESP_LOGD(TAG, "WiFi scan finished: %d networks, %d cached", count, emitted);
}

#ifdef USE_EYBOND_BLE
void EybondCollector::setup_ble_() {
  using namespace esp32_ble_server;
  if (global_ble_server == nullptr) {
    return;  // esp32_ble_server not up yet; retry next loop
  }

  ble_ = std::unique_ptr<eybond::BleProvisioning>(new eybond::BleProvisioning(this));

  // Raise the local ATT MTU so a client that negotiates up (HA host BlueZ, an
  // ESP32 BT proxy) isn't capped at the 23-byte default — short responses fit
  // either way, but this gives headroom for the Wi-Fi scan list.
  esp_ble_gatt_set_local_mtu(512);

  // Marginal-link robustness: the board's advertisements and notify responses
  // must reach the Home Assistant Bluetooth adapter, which may be several metres
  // away. ESPHome leaves BLE TX power at the IDF default (~+3 dBm); raise it to
  // the maximum it exposes (+9 dBm) for both advertising and connections so a
  // distant host adapter can still complete the GATT exchange.
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);

  // Advertise the PN in manufacturer data so the integration's BLE scan can
  // discover the bridge. set_manufacturer_data() stores it on the server and
  // re-applies it on every advertising restart (a direct esp32_ble call would
  // be clobbered by the server's own restart_advertising_).
  global_ble_server->set_manufacturer_data(eybond::build_ble_manufacturer_data(ble_pn_));

  // PROVISION layout (smartess_ble.PROVISION_LAYOUT): the 0x1827 service with a
  // write characteristic (0x2ADB) and a notify characteristic (0x2ADC). The
  // integration picks this layout from the discovered GATT services and then
  // uses plain notify-based exchanges.
  BLEService *svc = global_ble_server->create_service(
      esp32_ble::ESPBTUUID::from_raw("00001827-0000-1000-8000-00805f9b34fb"), /*advertise=*/false);
  if (svc == nullptr) {
    ESP_LOGE(TAG, "BLE provisioning: service create failed");
    return;
  }
  BLECharacteristic *write_char = svc->create_characteristic(
      "00002adb-0000-1000-8000-00805f9b34fb",
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  ble_notify_char_ = svc->create_characteristic("00002adc-0000-1000-8000-00805f9b34fb",
                                                BLECharacteristic::PROPERTY_NOTIFY |
                                                    BLECharacteristic::PROPERTY_READ);
  // Notifications are only delivered to clients that subscribed via the CCCD,
  // so the notify characteristic must carry a 0x2902 descriptor.
  ble_notify_char_->add_descriptor(new BLE2902());  // NOLINT(cppcoreguidelines-owning-memory)

  write_char->EventEmitter<BLECharacteristicEvt::VectorEvt, std::vector<uint8_t>, uint16_t>::on(
      BLECharacteristicEvt::VectorEvt::ON_WRITE,
      [this](const std::vector<uint8_t> &data, uint16_t /*conn_id*/) { this->ble_feed_(data); });

  global_ble_server->enqueue_start_service(svc);
  ESP_LOGI(TAG, "BLE provisioning service ready (advertising PN %s)", ble_pn_.c_str());
}

void EybondCollector::ble_feed_(const std::vector<uint8_t> &data) {
  ble_rx_.append(reinterpret_cast<const char *>(data.data()), data.size());
  ble_rx_last_ms_ = millis();
  ble_rx_pending_ = true;
}

void EybondCollector::process_ble_(uint32_t now) {
  if (!ble_rx_pending_ || ble_ == nullptr) {
    return;
  }
  // Coalesce ATT fragments: the client waits for our notify before sending the
  // next command, so a brief quiet period reliably frames exactly one command.
  if (static_cast<int32_t>(now - ble_rx_last_ms_) < static_cast<int32_t>(BLE_RX_SETTLE_MS)) {
    return;
  }
  ble_rx_pending_ = false;
  const std::string command = ble_rx_;
  ble_rx_.clear();

  const std::string response = ble_->handle_command(command);
  if (response.empty() || ble_notify_char_ == nullptr) {
    return;
  }
  ble_notify_char_->set_value(response + "\r\n");
  ble_notify_char_->notify();
  // Log only the response (status codes); the command may carry a Wi-Fi password.
  ESP_LOGD(TAG, "BLE provisioning -> %s", response.c_str());

  // Now that the response is on the air, refresh the Wi-Fi scan cache if a scan
  // was requested. Deferring it here keeps the radio idle during the notify, so
  // the response is never lost to BLE/Wi-Fi coexistence on a weak link.
  if (ble_scan_pending_ && !wifi_scan_running_) {
    ble_scan_pending_ = false;
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
    wifi_scan_running_ = true;
    ble_last_scan_ms_ = (now == 0) ? 1 : now;
  }
}

void EybondCollector::ble_apply_wifi(const std::string &ssid, const std::string &password) {
#ifdef USE_WIFI
  pending_wifi_ssid_ = ssid;
  pending_wifi_password_ = password;
  wifi_apply_requested_ = true;
  // The BLE notify is independent of the STA link, so there is no in-band
  // response to protect (unlike the FC=3 TCP path): apply almost immediately.
  wifi_apply_at_ms_ = millis() + 200;
  ESP_LOGI(TAG, "BLE provisioning: staged WiFi credentials (ssid length %u)",
           static_cast<unsigned>(ssid.size()));
#else
  (void) ssid;
  (void) password;
#endif
}

bool EybondCollector::ble_wifi_connected() {
#ifdef USE_WIFI
  return wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected();
#else
  return false;
#endif
}

std::vector<eybond::BleWifiNetwork> EybondCollector::ble_wifi_scan() {
  // IMPORTANT: do NOT start a Wi-Fi scan here. Scanning the radio contends with
  // BLE under coexistence and, on a weak BLE link, drops the very notification
  // that carries this response — the integration then reports "could not read
  // the Wi-Fi list / weak connection". Answer instantly from the cache plus the
  // network we are on, and schedule a refresh to run AFTER the notify (see
  // process_ble_), throttled so repeated refreshes don't keep hammering the radio.
  std::vector<eybond::BleWifiNetwork> out = wifi_scan_entries_;
  const std::string ssid = WiFi.SSID().c_str();
  if (!ssid.empty() && ssid.find_first_of(",[]") == std::string::npos) {
    bool present = false;
    for (const auto &n : out) {
      if (n.ssid == ssid) {
        present = true;
        break;
      }
    }
    if (!present) {
      out.insert(out.begin(), eybond::BleWifiNetwork{ssid, std::atoi(this->wifi_rssi().c_str())});
    }
  }
  const uint32_t now = millis();
  if (!wifi_scan_running_ && (ble_last_scan_ms_ == 0 || static_cast<int32_t>(now - ble_last_scan_ms_) > 20000)) {
    ble_scan_pending_ = true;  // refreshed after we notify, for the next request
  }
  return out;
}
#endif  // USE_EYBOND_BLE

void EybondCollector::process_pending_connect_() {
  if (!connect_pending_) {
    return;
  }
  connect_pending_ = false;
  ESP_LOGI(TAG, "Connecting to %s:%u", pending_host_.c_str(), pending_port_);
  tcp_.stop();
  if (tcp_.connect(pending_host_.c_str(), pending_port_)) {
#ifndef USE_LIBRETINY
    tcp_.setNoDelay(true);  // LibreTiny's WiFiClient (LwIPClient) has no setNoDelay
#endif
    tcp_up_ = true;
    ESP_LOGI(TAG, "Connected to %s:%u", pending_host_.c_str(), pending_port_);
    core_->on_tcp_connected(millis());
  } else {
    tcp_up_ = false;
    ESP_LOGW(TAG, "Connect to %s:%u failed", pending_host_.c_str(), pending_port_);
    core_->on_tcp_connect_failed(millis());
  }
}

void EybondCollector::udp_reply(const uint8_t *data, size_t len) {
  udp_.beginPacket(udp_.remoteIP(), udp_.remotePort());
  udp_.write(data, len);
  udp_.endPacket();
}

void EybondCollector::tcp_connect(const std::string &host, uint16_t port) {
  connect_pending_ = true;
  pending_host_ = host;
  pending_port_ = port;
}

void EybondCollector::tcp_send(const uint8_t *data, size_t len) {
  if (tcp_up_) {
    this->note_activity_(millis());
    tcp_.write(data, len);
  }
}

void EybondCollector::tcp_close() {
  tcp_.stop();
  tcp_up_ = false;
}

void EybondCollector::uart_send(const uint8_t *data, size_t len) {
  this->note_activity_(millis());
  // Drop stale inverter bytes so they cannot masquerade as the new response.
  while (this->available() > 0) {
    uint8_t discard;
    if (!this->read_byte(&discard)) {
      break;
    }
  }
  if (flow_control_pin_ != nullptr) {
    flow_control_pin_->digital_write(true);
  }
  this->write_array(data, len);
  this->flush();
  if (flow_control_pin_ != nullptr) {
    flow_control_pin_->digital_write(false);
  }
}

void EybondCollector::note_activity_(uint32_t now) {
  status_led_activity_until_ms_ = now + 90;
}

void EybondCollector::update_status_led_(uint32_t now) {
  if (status_led_pin_ == nullptr) {
    return;
  }

  if (static_cast<int32_t>(status_led_activity_until_ms_ - now) > 0) {
    this->write_status_led_(false);
    return;
  }

  if (!network::is_connected()) {
    // Wi-Fi is not connected yet: quick blink so the board is visibly alive.
    if (now - status_led_last_toggle_ms_ >= 250) {
      this->write_status_led_(!status_led_state_);
      status_led_last_toggle_ms_ = now;
    }
    return;
  }

  if (!tcp_up_) {
    // Wi-Fi is up, but EyeBond Local is not connected yet: slow blink.
    if (now - status_led_last_toggle_ms_ >= 1000) {
      this->write_status_led_(!status_led_state_);
      status_led_last_toggle_ms_ = now;
    }
    return;
  }

  // Connected and idle.
  this->write_status_led_(true);
}

void EybondCollector::write_status_led_(bool on) {
  if (status_led_pin_ == nullptr || status_led_state_ == on) {
    return;
  }
  status_led_pin_->digital_write(on);
  status_led_state_ = on;
}

std::string EybondCollector::wifi_rssi() {
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%d", wifi::global_wifi_component->wifi_rssi());
    return buffer;
  }
#endif
  return "-50";
}

void EybondCollector::configure_fallback_ap_(const std::string &pn) {
#ifdef USE_WIFI
  if (wifi::global_wifi_component == nullptr) {
    return;
  }

  auto ap = wifi::global_wifi_component->get_ap();
  const std::string current = ap.get_ssid();
  if (!current.empty() && current != "Eybond-Bridge Setup") {
    return;  // Respect a user-provided fallback AP name.
  }

  std::string ssid = pn;

  ap.set_ssid(ssid);
  wifi::global_wifi_component->set_ap(ap);
  ESP_LOGI(TAG, "Fallback setup AP SSID: %s", ssid.c_str());
#else
  (void) pn;
#endif
}

void EybondCollector::log(const char *message) { ESP_LOGD(TAG, "%s", message); }

std::string EybondCollector::uart_settings_string_() const {
  const auto *parent = this->parent_;
  const char *parity = "NONE";
  switch (parent->get_parity()) {
    case uart::UART_CONFIG_PARITY_EVEN:
      parity = "EVEN";
      break;
    case uart::UART_CONFIG_PARITY_ODD:
      parity = "ODD";
      break;
    default:
      break;
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%" PRIu32 ",%u,%u,%s", parent->get_baud_rate(),
                parent->get_data_bits(), parent->get_stop_bits(), parity);
  return buffer;
}

void EybondCollector::dump_config() {
  ESP_LOGCONFIG(TAG, "Eybond Collector bridge:");
  if (core_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  UDP discovery port: %u", udp_port_);
    ESP_LOGCONFIG(TAG, "  Heartbeat interval: %" PRIu32 " ms", core_config_.heartbeat_interval_ms);
    ESP_LOGCONFIG(TAG, "  Response gap/timeout: %" PRIu32 "/%" PRIu32 " ms", core_config_.uart_gap_ms,
                  core_config_.uart_timeout_ms);
    ESP_LOGCONFIG(TAG, "  Command spacing: %" PRIu32 " ms", core_config_.command_spacing_ms);
    ESP_LOGCONFIG(TAG, "  UART: %s", uart_settings_string_().c_str());
  }
  if (!static_server_host_.empty()) {
    ESP_LOGCONFIG(TAG, "  Static server: %s:%u", static_server_host_.c_str(), static_server_port_);
  }
  ESP_LOGCONFIG(TAG, "  Status LED: %s", status_led_pin_ == nullptr ? "disabled" : "enabled");
}

}  // namespace eybond_collector
}  // namespace esphome

#endif  // USE_ARDUINO
