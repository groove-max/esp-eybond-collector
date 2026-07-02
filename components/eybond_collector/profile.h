// Virtual collector identity. Values mirror the integration's reference fake
// collector defaults so the bridge is indistinguishable from a factory unit.
// PN must be synthetic (project rule): letter + 13 or letter + 17 digits.
#pragma once

#include <string>

namespace eybond {

// Bridge firmware version, embedded in the FC=2 param-6 hardware_version marker
// ("esp-collector/<version>/<platform>") the integration keys the bridge on.
constexpr const char *BRIDGE_VERSION = "0.1.7";

struct CollectorProfile {
  std::string pn;  // set at startup (config override or synthesized from MAC)
  // AT+FWVER / FC=2 param 5. Defaults to OUR bridge version — the factory logger's
  // version is deliberately not hardcoded here; a reflashed unit that must mirror the
  // original for the original cloud can override it via YAML later (see roadmap).
  std::string firmware_version = BRIDGE_VERSION;
  std::string at_version = "1.11";
  std::string collector_type = "Wi-Fi.DTU";
  std::string upload_mode = "OFF";
  std::string uart = "9600,8,1,NONE";  // reported AT+UART value; keep in sync with real UART config
  std::string link_status = "connected";
  std::string wifi_scan_list;  // AT+INTPARA49
};

}  // namespace eybond
