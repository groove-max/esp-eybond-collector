#pragma once

#include <string>

// API-shape adapters: support both the release ESPHome toolchain and current
// native-IDF ESPHome without version-number guesses or unsafe casts.
namespace eybond {
namespace compat {

template<typename WiFi>
auto connected_ssid(WiFi *wifi, int) -> decltype(wifi->wifi_ssid(), std::string()) {
  return wifi->wifi_ssid();
}

template<typename WiFi>
std::string connected_ssid(WiFi *wifi, long) {
  char buffer[33]{};  // IEEE 802.11 SSID: at most 32 bytes plus terminator.
  return wifi->wifi_ssid_to(buffer);
}

template<typename WiFi>
auto retain_scan_results(WiFi *wifi, int) -> decltype(wifi->set_keep_scan_results(true), bool()) {
  wifi->set_keep_scan_results(true);
  return true;
}

template<typename WiFi>
bool retain_scan_results(WiFi *, long) { return false; }

template<typename IP>
auto ip_string(const IP &ip, int) -> decltype(ip.str()) { return ip.str(); }

template<typename IP>
std::string ip_string(const IP &ip, long) {
  char buffer[64]{};  // Covers both IPv4 and IPv6 textual forms.
  return ip.str_to(buffer);
}

template<typename Pin>
auto pin_summary(Pin *pin, int) -> decltype(pin->dump_summary(nullptr, 0), std::string()) {
  char buffer[96]{};
  pin->dump_summary(buffer, sizeof(buffer));
  buffer[sizeof(buffer) - 1] = '\0';
  return buffer;
}

template<typename Pin>
auto pin_summary(Pin *pin, long) -> decltype(pin->dump_summary()) {
  return pin->dump_summary();
}

template<typename Request>
auto request_url(Request *request, int) -> decltype(request->url(), std::string()) {
  return request->url().c_str();
}

template<typename Request>
std::string request_url(Request *request, long) {
  char buffer[Request::URL_BUF_SIZE]{};
  // Copy before the stack buffer expires; never return the temporary StringRef.
  return request->url_to(buffer).c_str();
}

template<typename Request>
auto form_parameter(Request *request, const char *name, int) -> decltype(request->getParam(name, true)) {
  auto *parameter = request->getParam(name, true);
  return parameter != nullptr ? parameter : request->getParam(name);
}

template<typename Request>
auto form_parameter(Request *request, const char *name, long) -> decltype(request->getParam(name)) {
  // ESPHome's native-IDF request parser searches the POST body before the URL.
  return request->getParam(name);
}

template<typename Request>
auto request_header(Request *request, const char *name, int) -> decltype(request->get_header(name), std::string()) {
  return request->get_header(name).value_or("");
}

template<typename Request>
auto request_header(Request *request, const char *name, long) -> decltype(request->header(name), std::string()) {
  return request->header(name).c_str();
}

template<typename Request>
bool same_origin(Request *request) {
  // Preserve support for native clients without Origin; browser-originated
  // configuration requests must match the full Host, including its port.
  if (!request->hasHeader("Origin")) return true;
  const std::string host = request_header(request, "Host", 0);
  return !host.empty() && request_header(request, "Origin", 0) == "http://" + host;
}

}  // namespace compat
}  // namespace eybond
