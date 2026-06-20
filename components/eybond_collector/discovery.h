// UDP discovery: parse the HA `set>server=IP:PORT;` redirect and identity helpers.
// Mirrors fake_collector_lib.parse_discovery_redirect (regex ^set>server=([^:;\s]+):(\d+);$
// after strip) and the factory reply "rsp>server=2;".
#pragma once

#include <cstdint>
#include <string>

namespace eybond {

constexpr const char *DISCOVERY_UDP_REPLY = "rsp>server=2;";
constexpr uint16_t DISCOVERY_UDP_PORT = 58899;

struct DiscoveryRedirect {
  std::string server_ip;
  uint16_t server_port = 0;
};

bool parse_discovery_redirect(const uint8_t *data, size_t len, DiscoveryRedirect *out);

// Synthesize a stable collector PN from the WiFi MAC: "V00" + 15-digit decimal
// of the 48-bit MAC. Matches the integration's PN18 format ^[A-Z]\d{17}$. The
// "V" (virtual) prefix is deliberately NEUTRAL — real eybond/SmartESS collectors
// start with "E", so this never impersonates a real collector's PN range.
std::string synthesize_pn(const uint8_t mac[6]);

}  // namespace eybond
