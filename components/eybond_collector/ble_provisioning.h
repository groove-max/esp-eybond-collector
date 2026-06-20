// Sans-IO SmartESS BLE provisioning protocol handler.
//
// Emulates the server side of the eybond/SmartESS collector's BLE Wi-Fi
// pairing so the ha-eybond-local integration can provision the bridge onto a
// Wi-Fi network over Bluetooth, exactly as it would a factory collector.
//
// Behavior contract = the integration's client in
//   custom_components/eybond_local/collector/smartess_ble.py
//   (SmartEssBleProvisioner / SmartEssBleSession).
// The client speaks short AT text commands over the GATT write characteristic
// and reads the reply from the notify characteristic:
//   AT+FWVER?     -> "AT+FWVER:<fw>"
//   AT+ATVER?     -> "AT+ATVER:<at>"          (>=1.11 selects the WFLKAP branch)
//   AT+INTPARA49? -> "AT+INTPARA:49,[ssid,sig][ssid2,sig2]..."   (Wi-Fi scan)
//   WFLKAP branch (at_version >= 1.11):
//     AT+WFLKAP=<ssid>,AES,WPA2_PSK,<password> -> "AT+WFLKAP:W000"
//     AT+LINK?  -> "AT+LINK:W000" once associated, else "AT+LINK:W051" (degraded)
//   INTPARA branch (legacy, at_version < 1.11):
//     AT+INTPARA=41,<ssid> / AT+INTPARA=43,<password> / AT+INTPARA=29,1
//       -> "AT+INTPARA:W000"
//     AT+INTPARA48? -> "AT+INTPARA:48,<detected>,<station>,<cloud>"
//        success = "1,0,0" (detected>0, station==0, cloud==0), else "0,0,1" (degraded)
//
// Like CollectorCore this is platform-independent (no Arduino/BLE includes):
// the glue feeds complete command lines in and ships the returned text back
// over the notify characteristic. All side effects go through Actions, so the
// parser is fully host-testable.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eybond {

struct BleWifiNetwork {
  std::string ssid;
  int signal = 0;
};

class BleProvisioning {
 public:
  class Actions {
   public:
    virtual ~Actions() = default;
    // Collector firmware / AT-interpreter versions (mirror CollectorProfile).
    virtual std::string ble_fw_version() = 0;
    virtual std::string ble_at_version() = 0;
    // Stage and apply STA credentials. Returns immediately; association is async
    // and reported back through ble_wifi_connected().
    virtual void ble_apply_wifi(const std::string &ssid, const std::string &password) = 0;
    // True once the STA has associated with the target network.
    virtual bool ble_wifi_connected() = 0;
    // Cached scan list for AT+INTPARA49?; may be empty while a scan is in flight.
    virtual std::vector<BleWifiNetwork> ble_wifi_scan() = 0;
    virtual void ble_log(const char *message) { (void) message; }
  };

  explicit BleProvisioning(Actions *actions) : actions_(actions) {}

  // Handle one complete command line. Leading/trailing whitespace and CR/LF are
  // tolerated. Returns the text to notify back (no trailing CRLF; the caller may
  // append one). An empty result means "send nothing" (unknown command).
  std::string handle_command(const std::string &raw);

 private:
  Actions *actions_;
  // INTPARA branch staging: AT+INTPARA=41,<ssid> is remembered until
  // AT+INTPARA=43,<password> arrives and the pair is applied together.
  std::string intpara_ssid_;
};

// Build the BLE manufacturer-specific advertising payload that carries the
// collector PN so the integration's BLE scan can extract it (it decodes the
// manufacturer-data value as text and matches the PN format). Layout:
// [company-id LE lo][company-id LE hi][ASCII PN...]. Espressif's Bluetooth SIG
// company id (0x02E5) is used: honest for an ESP device and deliberately NOT
// eybond's 0x3545 — the bridge never impersonates a factory collector.
std::vector<uint8_t> build_ble_manufacturer_data(const std::string &pn);

}  // namespace eybond
