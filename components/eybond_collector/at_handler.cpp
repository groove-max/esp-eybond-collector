#include "at_handler.h"

namespace eybond {

namespace {

std::string trim(const std::string &text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) {
    begin++;
  }
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) {
    end--;
  }
  return text.substr(begin, end - begin);
}

bool is_ascii(const std::string &text) {
  for (char ch : text) {
    if (static_cast<unsigned char>(ch) > 0x7F) {
      return false;
    }
  }
  return true;
}

// Port of at.py normalize_at_command: upper-case, strip AT+ prefix and ?/= suffix.
bool normalize_command(const std::string &raw, std::string *out) {
  std::string normalized = trim(raw);
  for (char &ch : normalized) {
    if (ch >= 'a' && ch <= 'z') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }
  }
  if (normalized.rfind("AT+", 0) == 0) {
    normalized = normalized.substr(3);
  }
  while (!normalized.empty() && (normalized.back() == '?' || normalized.back() == '=')) {
    normalized.pop_back();
  }
  if (normalized.empty() || !is_ascii(normalized)) {
    return false;
  }
  *out = normalized;
  return true;
}

std::string vdtu_value(const CollectorProfile &profile) {
  if (!profile.vdtu.empty()) {
    return profile.vdtu;
  }
  return std::string("esp-collector,") + BRIDGE_VERSION +
         ";features=local_only,no_cloud,wifi_params,endpoint_write" +
         ";uart=" + profile.uart;
}

}  // namespace

bool parse_at_line(const std::string &line, AtCommand *out) {
  const std::string normalized = trim(line);
  if (normalized.rfind("AT+", 0) != 0) {
    return false;
  }
  const std::string remainder = normalized.substr(3);

  if (!remainder.empty() && remainder.back() == '?') {
    std::string command;
    if (!normalize_command(remainder.substr(0, remainder.size() - 1), &command)) {
      return false;
    }
    out->command = command;
    out->is_write = false;
    out->value.clear();
    return true;
  }

  const size_t separator = remainder.find('=');
  if (separator == std::string::npos) {
    return false;
  }
  std::string command;
  if (!normalize_command(remainder.substr(0, separator), &command)) {
    return false;
  }
  out->command = command;
  out->is_write = true;
  out->value = trim(remainder.substr(separator + 1));
  return true;
}

std::string build_at_response(const std::string &command, const std::string &value) {
  return "AT+" + command + ":" + value + "\r\n";
}

std::string build_at_reply(const AtCommand &command, const CollectorProfile &profile,
                           const AtRuntimeValues &runtime) {
  if (command.is_write) {
    return build_at_response(command.command, "W000");
  }

  const std::string &name = command.command;
  if (name == "DTUPN") {
    return build_at_response(name, profile.pn);
  }
  if (name == "ATVER") {
    return build_at_response(name, profile.at_version);
  }
  if (name == "ENUPMODE") {
    return build_at_response(name, profile.upload_mode);
  }
  if (name == "SYST") {
    return build_at_response(name, runtime.time_string);
  }
  if (name == "WFSS") {
    return build_at_response(name, runtime.wifi_rssi);
  }
  if (name == "UART") {
    return build_at_response(name, profile.uart);
  }
  if (name == "DTUTYPE") {
    return build_at_response(name, profile.collector_type);
  }
  if (name == "FWVER") {
    return build_at_response(name, profile.firmware_version);
  }
  if (name == "CLDSRVHOST1") {
    return build_at_response(name, runtime.cloud_endpoint);
  }
  if (name == "HTBT") {
    return build_at_response(name, "");
  }
  if (name == "LINK") {
    return build_at_response(name, profile.link_status);
  }
  if (name == "INTPARA49") {
    return build_at_response(name, profile.wifi_scan_list);
  }
  if (name == "VDTU") {
    // Virtual-DTU capability probe. The public ESP bridge must fail closed to
    // "local bridge" identity; an empty/factory-like reply makes HA expose
    // cloud-only controls for a virtual collector.
    return build_at_response(name, vdtu_value(profile));
  }
  return build_at_response(name, "");
}

}  // namespace eybond
