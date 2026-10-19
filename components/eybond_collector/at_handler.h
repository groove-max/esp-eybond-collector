// AT command line parsing and reply building.
// Port of ha-eybond-local collector/at.py (parse) and
// .local/tools/fake_collector_lib.py build_at_reply (reply table).
// Replies are byte-exact: "AT+<CMD>:<value>\r\n".
#pragma once

#include <string>

#include "profile.h"

namespace eybond {

struct AtCommand {
  std::string command;  // normalized: upper-case, no AT+ prefix, no ?/= suffix
  bool is_write = false;
  std::string value;  // write payload, trimmed
};

// Dynamic values the reply table needs from the platform.
struct AtRuntimeValues {
  std::string cloud_endpoint;  // "IP,PORT,TCP" of the current HA server
  std::string wifi_rssi;       // e.g. "-55"
  std::string time_string;     // "%Y%m%d%H%M%S" UTC, empty when no time source
};

// Returns false when the line is not a valid AT query/write.
bool parse_at_line(const std::string &line, AtCommand *out);

std::string build_at_response(const std::string &command, const std::string &value);

// Pure reply generation cannot execute a write. Core/platform confirm writes;
// this helper refuses them rather than fabricating success.
std::string build_at_reply(const AtCommand &command, const CollectorProfile &profile,
                           const AtRuntimeValues &runtime);

}  // namespace eybond
