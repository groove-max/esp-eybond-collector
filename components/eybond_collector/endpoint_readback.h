#pragma once

#include <cstdint>
#include <string>

namespace eybond {

inline bool endpoint_apply_pending(const std::string &pending, bool applying) {
  return applying || !pending.empty();
}

// Configured state is distinct from the socket's currently applied endpoint.
// Keep the accepted value visible during the deferred Apply/ACK window.
inline bool pending_endpoint_readback(const std::string &pending, bool applying,
                                     const std::string &host, uint16_t port, std::string *out) {
  if (applying) {
    *out = host + "," + std::to_string(port) + ",TCP";
    return true;
  }
  if (!pending.empty()) {
    *out = pending;
    return true;
  }
  return false;  // The core supplies its applied endpoint.
}

}  // namespace eybond
