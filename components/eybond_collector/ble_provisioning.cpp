#include "ble_provisioning.h"

#include <cstddef>

namespace eybond {

namespace {

// Max length of the AT+INTPARA:49 scan response, so one BLE notification never
// exceeds the negotiated MTU-3 (a too-long notify is dropped → scan timeout).
constexpr size_t BLE_WIFI_LIST_MAX_BYTES = 180;

std::string strip(const std::string &s) {
  size_t start = 0;
  size_t end = s.size();
  auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (start < end && is_space(s[start])) {
    start++;
  }
  while (end > start && is_space(s[end - 1])) {
    end--;
  }
  return s.substr(start, end - start);
}

bool starts_with(const std::string &s, const char *prefix) {
  size_t i = 0;
  for (; prefix[i] != '\0'; i++) {
    if (i >= s.size() || s[i] != prefix[i]) {
      return false;
    }
  }
  return true;
}

// Split into at most `max_parts` fields on `delim`; the final field keeps any
// remaining delimiters (so a password containing commas survives intact).
std::vector<std::string> split_max(const std::string &s, char delim, size_t max_parts) {
  std::vector<std::string> parts;
  size_t pos = 0;
  while (parts.size() + 1 < max_parts) {
    const size_t next = s.find(delim, pos);
    if (next == std::string::npos) {
      break;
    }
    parts.push_back(s.substr(pos, next - pos));
    pos = next + 1;
  }
  parts.push_back(s.substr(pos));
  return parts;
}

}  // namespace

std::string BleProvisioning::handle_command(const std::string &raw) {
  const std::string cmd = strip(raw);
  if (cmd.empty()) {
    return "";
  }

  // --- version probes (drive the client's branch selection) ---
  if (cmd == "AT+FWVER?") {
    return "AT+FWVER:" + actions_->ble_fw_version();
  }
  if (cmd == "AT+ATVER?") {
    return "AT+ATVER:" + actions_->ble_at_version();
  }

  // --- Wi-Fi scan list ---
  if (cmd == "AT+INTPARA49?") {
    std::string out = "AT+INTPARA:49,";
    for (const auto &net : actions_->ble_wifi_scan()) {
      // The integration parses re.findall(r"\[([^\]]+)\]") then rpartition(","),
      // so any ',', '[' or ']' inside an SSID would corrupt the list. The glue
      // already filters those out of the scan cache; skip defensively here too.
      if (net.ssid.empty() || net.ssid.find_first_of(",[]") != std::string::npos) {
        continue;
      }
      const std::string entry = "[" + net.ssid + "," + std::to_string(net.signal) + "]";
      // Keep the whole notification within a conservative MTU budget: a BLE
      // notification longer than the negotiated MTU-3 is silently dropped, which
      // the integration sees as a scan timeout. ~180 bytes is safe for any MTU
      // a real HA stack negotiates (host BlueZ 517, ESP32 BT proxy 247).
      if (out.size() + entry.size() > BLE_WIFI_LIST_MAX_BYTES) {
        break;
      }
      out += entry;
    }
    return out;
  }

  // --- WFLKAP branch (at_version >= 1.11) ---
  if (starts_with(cmd, "AT+WFLKAP=")) {
    const std::string args = cmd.substr(sizeof("AT+WFLKAP=") - 1);
    // AT+WFLKAP=<ssid>,AES,WPA2_PSK,<password> — 4 fields, password last.
    const std::vector<std::string> parts = split_max(args, ',', 4);
    if (parts.size() >= 4) {
      actions_->ble_apply_wifi(parts[0], parts[3]);
    } else if (!parts.empty()) {
      // Tolerate a shorter form: <ssid>,<password> or just <ssid>.
      actions_->ble_apply_wifi(parts[0], parts.size() >= 2 ? parts.back() : std::string());
    }
    return "AT+WFLKAP:W000";
  }
  if (cmd == "AT+LINK?") {
    return std::string("AT+LINK:") + (actions_->ble_wifi_connected() ? "W000" : "W051");
  }

  // --- INTPARA branch (legacy, at_version < 1.11) ---
  if (starts_with(cmd, "AT+INTPARA=41,")) {
    intpara_ssid_ = cmd.substr(sizeof("AT+INTPARA=41,") - 1);
    return "AT+INTPARA:W000";
  }
  if (starts_with(cmd, "AT+INTPARA=43,")) {
    const std::string password = cmd.substr(sizeof("AT+INTPARA=43,") - 1);
    actions_->ble_apply_wifi(intpara_ssid_, password);
    return "AT+INTPARA:W000";
  }
  if (starts_with(cmd, "AT+INTPARA=29")) {  // commit / restart
    return "AT+INTPARA:W000";
  }
  if (cmd == "AT+INTPARA48?") {
    // parse_intpara48_provision_result: SUCCESS needs station==0, cloud==0 and
    // detected>0; "0,0,1" (cloud==1) maps to a benign DEGRADED (W051) while the
    // STA is still associating.
    return actions_->ble_wifi_connected() ? "AT+INTPARA:48,1,0,0" : "AT+INTPARA:48,0,0,1";
  }

  // Unknown command: stay silent. The client treats a notify timeout as a
  // version-probe fallback, so silence is safe and never wedges the flow.
  return "";
}

std::vector<uint8_t> build_ble_manufacturer_data(const std::string &pn) {
  std::vector<uint8_t> data;
  data.reserve(2 + pn.size());
  data.push_back(0xE5);  // Espressif SIG company id 0x02E5, little-endian
  data.push_back(0x02);
  for (const char c : pn) {
    data.push_back(static_cast<uint8_t>(c));
  }
  return data;
}

}  // namespace eybond
