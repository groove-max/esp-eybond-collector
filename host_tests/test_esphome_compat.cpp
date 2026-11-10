#include "esphome_compat.h"
#include "minitest.h"

#include <cstdio>
#include <map>
#include <optional>

namespace {
struct Parameter {
  std::string text;
  const std::string &value() const { return text; }
};
struct RequestBase {
  std::map<std::string, Parameter> query, post;
  std::map<std::string, std::string> headers;
  bool hasHeader(const char *name) const { return headers.count(name) != 0; }
  Parameter *lookup(std::map<std::string, Parameter> &params, const char *name) {
    auto it = params.find(name);
    return it == params.end() ? nullptr : &it->second;
  }
};
struct LegacyRequest : RequestBase {
  std::string url() const { return "/settings/save"; }
  std::string header(const char *name) const {
    auto it = headers.find(name);
    return it == headers.end() ? "" : it->second;
  }
  Parameter *getParam(const char *name, bool from_post = false) {
    return lookup(from_post ? post : query, name);
  }
};
struct CurrentRequest : RequestBase {
  static constexpr size_t URL_BUF_SIZE = 64;
  struct StringRef {
    char *ptr;
    const char *c_str() const { return ptr; }
  };
  StringRef url_to(char (&buffer)[URL_BUF_SIZE]) const {
    std::snprintf(buffer, URL_BUF_SIZE, "%s", "/settings/save");
    return {buffer};
  }
  std::optional<std::string> get_header(const char *name) const {
    auto it = headers.find(name);
    return it == headers.end() ? std::nullopt : std::optional<std::string>(it->second);
  }
  Parameter *getParam(const char *name) {
    auto *parameter = lookup(post, name);
    return parameter != nullptr ? parameter : lookup(query, name);
  }
};

template<typename Request> void check_request() {
  Request request;
  using namespace eybond::compat;
  const auto url = request_url(&request, 0);
  CHECK_STR(url, "/settings/save");
  request.query["port"] = {"8899"};
  request.post["port"] = {"18899"};
  CHECK_STR(form_parameter(&request, "port", 0)->value(), "18899");
  request.post.clear();
  CHECK_STR(form_parameter(&request, "port", 0)->value(), "8899");
  CHECK(form_parameter(&request, "missing", 0) == nullptr);
  request.post["empty"] = {""};
  CHECK(form_parameter(&request, "empty", 0) != nullptr);
  CHECK(same_origin(&request));  // Native client with no Origin.
  request.headers["Host"] = "192.0.2.10:8080";
  request.headers["Origin"] = "http://192.0.2.10:8080";
  CHECK(same_origin(&request));
  request.headers["Origin"] = "http://192.0.2.10";
  CHECK(!same_origin(&request));
  request.headers["Origin"] = "http://192.0.2.10:8080.attacker.invalid";
  CHECK(!same_origin(&request));
  request.headers["Origin"] = "null";
  CHECK(!same_origin(&request));
  request.headers["Origin"] = "";
  CHECK(!same_origin(&request));
  request.headers.erase("Host");
  CHECK(!same_origin(&request));
}
}  // namespace

TEST(esphome_legacy_web_request_contract) { check_request<LegacyRequest>(); }
TEST(esphome_current_web_request_contract) { check_request<CurrentRequest>(); }

TEST(esphome_gpio_summary_compatibility) {
  struct LegacyPin {
    std::string dump_summary() const { return "GPIO4"; }
  } legacy;
  struct CurrentPin {
    size_t dump_summary(char *buffer, size_t len) const {
      return std::snprintf(buffer, len, "%s", "GPIO6");
    }
  } current;
  CHECK_STR(eybond::compat::pin_summary(&legacy, 0), "GPIO4");
  CHECK_STR(eybond::compat::pin_summary(&current, 0), "GPIO6");
}

TEST(esphome_network_metadata_and_scan_ownership) {
  struct LegacyWiFi {
    std::string wifi_ssid() const { return "example-legacy"; }
  } legacy;
  struct CurrentWiFi {
    bool retain = false;
    const char *wifi_ssid_to(char (&buffer)[33]) const {
      std::snprintf(buffer, sizeof(buffer), "%s", "example-native");
      return buffer;
    }
    void set_keep_scan_results(bool value) { retain = value; }
  } current;
  CHECK_STR(eybond::compat::connected_ssid(&legacy, 0), "example-legacy");
  CHECK_STR(eybond::compat::connected_ssid(&current, 0), "example-native");
  CHECK(!eybond::compat::retain_scan_results(&legacy, 0));
  CHECK(eybond::compat::retain_scan_results(&current, 0));
  CHECK(current.retain);
  struct LegacyIP {
    std::string str() const { return "192.0.2.10"; }
  } legacy_ip;
  struct CurrentIP {
    const char *str_to(char *buffer) const {
      std::snprintf(buffer, 16, "%s", "192.0.2.11");
      return buffer;
    }
  } current_ip;
  CHECK_STR(eybond::compat::ip_string(legacy_ip, 0), "192.0.2.10");
  CHECK_STR(eybond::compat::ip_string(current_ip, 0), "192.0.2.11");
}
