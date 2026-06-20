#include "discovery.h"

namespace eybond {

namespace {
constexpr const char *PREFIX = "set>server=";
constexpr size_t PREFIX_LEN = 11;

bool is_space(char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }
}  // namespace

bool parse_discovery_redirect(const uint8_t *data, size_t len, DiscoveryRedirect *out) {
  std::string text(reinterpret_cast<const char *>(data), len);
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && is_space(text[begin])) {
    begin++;
  }
  while (end > begin && is_space(text[end - 1])) {
    end--;
  }
  const std::string normalized = text.substr(begin, end - begin);

  if (normalized.size() <= PREFIX_LEN + 3 || normalized.rfind(PREFIX, 0) != 0 || normalized.back() != ';') {
    return false;
  }

  const std::string body = normalized.substr(PREFIX_LEN, normalized.size() - PREFIX_LEN - 1);
  const size_t colon = body.find(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= body.size()) {
    return false;
  }

  const std::string host = body.substr(0, colon);
  for (char ch : host) {
    if (ch == ';' || ch == ':' || is_space(ch)) {
      return false;
    }
  }

  uint32_t port = 0;
  for (size_t i = colon + 1; i < body.size(); i++) {
    const char ch = body[i];
    if (ch < '0' || ch > '9') {
      return false;
    }
    port = port * 10 + static_cast<uint32_t>(ch - '0');
    if (port > 65535) {
      return false;
    }
  }
  if (port == 0) {
    return false;
  }

  out->server_ip = host;
  out->server_port = static_cast<uint16_t>(port);
  return true;
}

std::string synthesize_pn(const uint8_t mac[6]) {
  uint64_t value = 0;
  for (int i = 0; i < 6; i++) {
    value = (value << 8) | mac[i];
  }
  char digits[16];
  for (int i = 14; i >= 0; i--) {
    digits[i] = static_cast<char>('0' + (value % 10));
    value /= 10;
  }
  digits[15] = '\0';
  return std::string("V00") + digits;
}

}  // namespace eybond
