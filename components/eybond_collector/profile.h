// Virtual collector identity. Values mirror the integration's reference fake
// collector defaults so the bridge is indistinguishable from a factory unit.
// PN must be synthetic (project rule): letter + 13 or letter + 17 digits.
#pragma once

#include <string>

namespace eybond {

// Bridge firmware version, advertised via AT+VDTU (virtual-DTU capability probe).
constexpr const char *BRIDGE_VERSION = "0.1.2";

struct CollectorProfile {
  std::string pn;  // set at startup (config override or synthesized from MAC)
  std::string firmware_version = "8.50.12.3";
  std::string at_version = "1.11";
  std::string collector_type = "Wi-Fi.DTU";
  std::string upload_mode = "OFF";
  std::string uart = "2400,8,1,NONE";  // reported AT+UART value; keep in sync with real UART config
  std::string link_status = "connected";
  std::string wifi_scan_list;  // AT+INTPARA49
  // AT+VDTU capability string ("esp-collector,<ver>;features=...;uart=...").
  // Empty -> reply carries an empty value, byte-identical to the factory
  // reference behavior for unknown commands; the platform glue opts in.
  std::string vdtu;
};

}  // namespace eybond
