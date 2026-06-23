// End-to-end scenarios for CollectorCore against a recording mock platform.
// Expected frames generated with the integration's protocol.py / fake_collector_lib.py.
#include <string>
#include <vector>

#include "core.h"
#include "minitest.h"

using namespace eybond;

namespace {

struct Call {
  std::string kind;  // udp_reply | tcp_connect | tcp_send | tcp_close | uart_send
  std::vector<uint8_t> data;
  std::string host;
  uint16_t port = 0;
};

class MockActions : public CollectorCore::Actions {
 public:
  std::vector<Call> calls;

  void udp_reply(const uint8_t *data, size_t len) override {
    calls.push_back({"udp_reply", {data, data + len}, "", 0});
  }
  void tcp_connect(const std::string &host, uint16_t port) override {
    calls.push_back({"tcp_connect", {}, host, port});
  }
  void tcp_send(const uint8_t *data, size_t len) override {
    calls.push_back({"tcp_send", {data, data + len}, "", 0});
  }
  void tcp_close() override { calls.push_back({"tcp_close", {}, "", 0}); }
  void uart_send(const uint8_t *data, size_t len) override {
    calls.push_back({"uart_send", {data, data + len}, "", 0});
  }
  std::string wifi_rssi() override { return "-55"; }
  std::string time_string() override { return "20260613120000"; }

  std::vector<Call> take() {
    std::vector<Call> out = calls;
    calls.clear();
    return out;
  }
};

struct Fixture {
  MockActions actions;
  CollectorCore core;

  explicit Fixture(CoreConfig config = CoreConfig{})
      : core(&actions, make_profile(), config) {}

  static CollectorProfile make_profile() {
    CollectorProfile profile;
    profile.pn = "V00000200000000001";
    profile.uart = "2400,8,1,NONE";
    return profile;
  }

  // Brings the link up: discovery at t=1000, connected at t=1100.
  void connect() {
    const std::string discovery = "set>server=192.0.2.10:8899;";
    core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 1000);
    core.loop(1000);
    core.on_tcp_connected(1100);
    actions.calls.clear();
  }

  void feed_frame(uint16_t tid, uint16_t devcode, uint8_t devaddr, uint8_t fcode,
                  const std::vector<uint8_t> &payload, uint32_t now_ms) {
    const std::vector<uint8_t> frame =
        build_frame(tid, devcode, devaddr, fcode, payload.data(), payload.size());
    core.on_tcp_data(frame.data(), frame.size(), now_ms);
  }
};

const char *HEARTBEAT_HEX = "80010000001001015630303030303230303030303030";

}  // namespace

TEST(core_discovery_replies_and_connects) {
  Fixture fx;
  const std::string discovery = "set>server=192.0.2.10:8899;";
  fx.core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 1000);
  fx.core.loop(1000);

  const auto calls = fx.actions.take();
  CHECK(calls.size() == 2);
  CHECK_STR(calls[0].kind, "udp_reply");
  CHECK_STR(std::string(calls[0].data.begin(), calls[0].data.end()), "rsp>server=2;");
  CHECK_STR(calls[1].kind, "tcp_connect");
  CHECK_STR(calls[1].host, "192.0.2.10");
  CHECK(calls[1].port == 8899);
}

TEST(core_ignores_non_discovery_udp) {
  Fixture fx;
  const std::string noise = "hello world";
  fx.core.on_udp_datagram(reinterpret_cast<const uint8_t *>(noise.data()), noise.size(), 1000);
  fx.core.loop(1000);
  CHECK(fx.actions.calls.empty());
}

TEST(core_sends_heartbeat_on_connect) {
  Fixture fx;
  const std::string discovery = "set>server=192.0.2.10:8899;";
  fx.core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 1000);
  fx.core.loop(1000);
  fx.actions.calls.clear();

  fx.core.on_tcp_connected(1100);
  const auto calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(calls[0].kind, "tcp_send");
  // build_unsolicited_heartbeat(tid=0x8001, pn=..., devcode=0, collector_addr=1)
  CHECK_HEX(calls[0].data, HEARTBEAT_HEX);
}

TEST(core_periodic_heartbeat_with_incrementing_tid) {
  Fixture fx;
  fx.connect();
  fx.core.loop(1100 + 59999);
  CHECK(fx.actions.calls.empty());
  fx.core.loop(1100 + 60000);
  const auto calls = fx.actions.take();
  CHECK(calls.size() == 1);
  // tid advances 0x8001 -> 0x8002
  CHECK_HEX(calls[0].data, "80020000001001015630303030303230303030303030");
}

TEST(core_answers_server_heartbeat_with_same_tid) {
  Fixture fx;
  fx.connect();
  fx.feed_frame(0x0042, 0x0000, 0x01, FC_HEARTBEAT, {}, 2000);
  const auto calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_HEX(calls[0].data, "00420000001001015630303030303230303030303030");
}

TEST(core_fc2_param5_returns_firmware) {
  Fixture fx;
  fx.connect();
  fx.feed_frame(7, 0x0994, 0x10, FC_QUERY_COLLECTOR, {5}, 2000);
  const auto calls = fx.actions.take();
  CHECK(calls.size() == 1);
  // build_collector_request(7, b"\x00\x05"+b"8.50.12.3", devcode=0x0994, collector_addr=0x10, fcode=2)
  CHECK_HEX(calls[0].data, "00070994000d10020005382e35302e31322e33");
}

TEST(core_fc2_other_param_fails) {
  Fixture fx;
  fx.connect();
  fx.feed_frame(8, 0x0994, 0x10, FC_QUERY_COLLECTOR, {14}, 2000);
  const auto calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_HEX(calls[0].data, "000809940004100201" "0e");
}

TEST(core_fc3_set_refused) {
  Fixture fx;
  fx.connect();
  fx.feed_frame(9, 0x0994, 0x10, FC_SET_COLLECTOR, {21, 1, 2, 3}, 2000);
  const auto calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_HEX(calls[0].data, "000909940004100301" "15");
}

TEST(core_at_query_over_tcp) {
  Fixture fx;
  fx.connect();
  const std::string line = "AT+DTUPN?\r\n";
  fx.core.on_tcp_data(reinterpret_cast<const uint8_t *>(line.data()), line.size(), 2000);
  const auto calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(std::string(calls[0].data.begin(), calls[0].data.end()), "AT+DTUPN:V00000200000000001\r\n");
}

TEST(core_at_cldsrvhost1_tracks_discovery_endpoint) {
  Fixture fx;
  fx.connect();
  const std::string line = "AT+CLDSRVHOST1?\r\n";
  fx.core.on_tcp_data(reinterpret_cast<const uint8_t *>(line.data()), line.size(), 2000);
  const auto calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(std::string(calls[0].data.begin(), calls[0].data.end()),
            "AT+CLDSRVHOST1:192.0.2.10,8899,TCP\r\n");
}

TEST(core_fc4_forwards_and_frames_response) {
  Fixture fx;
  fx.connect();
  const std::vector<uint8_t> request = {0x01, 0x03, 0x00, 0x64, 0x00, 0x02, 0x85, 0xD4};
  fx.feed_frame(0x0011, 0x0994, 0x10, FC_FORWARD_TO_DEVICE, request, 2000);
  fx.core.loop(2000);

  auto calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(calls[0].kind, "uart_send");
  CHECK_HEX(calls[0].data, "01030064000285d4");

  // Inverter responds in two chunks; completion after uart_gap_ms of silence.
  const std::vector<uint8_t> resp1 = {0x01, 0x03, 0x04};
  const std::vector<uint8_t> resp2 = {0x00, 0x01, 0x00, 0x02, 0x2A, 0x32};
  fx.core.on_uart_data(resp1.data(), resp1.size(), 2100);
  fx.core.on_uart_data(resp2.data(), resp2.size(), 2120);
  fx.core.loop(2120 + 59);
  CHECK(fx.actions.calls.empty());
  fx.core.loop(2120 + 60);

  calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(calls[0].kind, "tcp_send");
  // same tid/devcode/devaddr, payload = raw inverter bytes
  CHECK_HEX(calls[0].data, "00110994000b100401030400010002" "2a32");
}

TEST(core_fc4_timeout_sends_nothing) {
  Fixture fx;
  fx.connect();
  const std::vector<uint8_t> request = {0x01, 0x03, 0x00, 0x64, 0x00, 0x02, 0x85, 0xD4};
  fx.feed_frame(0x0011, 0x0994, 0x10, FC_FORWARD_TO_DEVICE, request, 2000);
  fx.core.loop(2000);
  fx.actions.calls.clear();

  fx.core.loop(2000 + 3000);  // uart_timeout_ms elapses with zero bytes
  const auto calls = fx.actions.take();
  CHECK(calls.empty());
}

TEST(core_fc4_requests_are_serialized_with_spacing) {
  Fixture fx;
  fx.connect();
  const std::vector<uint8_t> req_a = {0x01, 0x03, 0x00, 0x64, 0x00, 0x02, 0x85, 0xD4};
  const std::vector<uint8_t> req_b = {0x01, 0x03, 0x00, 0xC8, 0x00, 0x01, 0x04, 0x14};
  fx.feed_frame(1, 0x0994, 0x10, FC_FORWARD_TO_DEVICE, req_a, 2000);
  fx.feed_frame(2, 0x0994, 0x10, FC_FORWARD_TO_DEVICE, req_b, 2001);
  fx.core.loop(2001);

  auto calls = fx.actions.take();
  CHECK(calls.size() == 1);  // only the first request hits the UART
  CHECK_STR(calls[0].kind, "uart_send");

  // First completes at t=2100 (response) + 60 gap.
  const std::vector<uint8_t> resp = {0x01, 0x03, 0x02, 0x00, 0x2A, 0x38, 0x49};
  fx.core.on_uart_data(resp.data(), resp.size(), 2100);
  fx.core.loop(2160);
  calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(calls[0].kind, "tcp_send");

  // Second must wait command_spacing_ms (850) after completion.
  fx.core.loop(2160 + 849);
  CHECK(fx.actions.calls.empty());
  fx.core.loop(2160 + 850);
  calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(calls[0].kind, "uart_send");
  CHECK_HEX(calls[0].data, "010300c800010414");
}

TEST(core_uart_noise_outside_request_ignored) {
  Fixture fx;
  fx.connect();
  const uint8_t noise[3] = {1, 2, 3};
  fx.core.on_uart_data(noise, sizeof(noise), 2000);
  fx.core.loop(3000);
  CHECK(fx.actions.calls.empty());
}

TEST(core_reconnects_with_backoff_after_close) {
  Fixture fx;
  fx.connect();
  fx.core.on_tcp_closed(5000);
  fx.core.loop(5000 + 1999);
  CHECK(fx.actions.calls.empty());
  fx.core.loop(5000 + 2000);  // reconnect_min_ms
  auto calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(calls[0].kind, "tcp_connect");

  // Failure doubles the backoff: next attempt 4000 ms later.
  fx.core.on_tcp_connect_failed(7100);
  fx.core.loop(7100 + 3999);
  CHECK(fx.actions.calls.empty());
  fx.core.loop(7100 + 4000);
  calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(calls[0].kind, "tcp_connect");
}

TEST(core_new_discovery_endpoint_reconnects) {
  Fixture fx;
  fx.connect();
  const std::string discovery = "set>server=192.0.2.20:8899;";
  fx.core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 9000);
  fx.core.loop(9000);
  const auto calls = fx.actions.take();
  // udp reply + close old + connect new
  CHECK(calls.size() == 3);
  CHECK_STR(calls[0].kind, "udp_reply");
  CHECK_STR(calls[1].kind, "tcp_close");
  CHECK_STR(calls[2].kind, "tcp_connect");
  CHECK_STR(calls[2].host, "192.0.2.20");
}

TEST(core_same_endpoint_discovery_keeps_connection) {
  Fixture fx;
  fx.connect();
  const std::string discovery = "set>server=192.0.2.10:8899;";
  fx.core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 9000);
  fx.core.loop(9000);
  const auto calls = fx.actions.take();
  CHECK(calls.size() == 1);  // only the UDP reply, no reconnect churn
  CHECK_STR(calls[0].kind, "udp_reply");
}

TEST(core_drops_inflight_on_disconnect) {
  Fixture fx;
  fx.connect();
  const std::vector<uint8_t> request = {0x01, 0x03, 0x00, 0x64, 0x00, 0x02, 0x85, 0xD4};
  fx.feed_frame(1, 0x0994, 0x10, FC_FORWARD_TO_DEVICE, request, 2000);
  fx.core.loop(2000);
  fx.actions.calls.clear();
  fx.core.on_tcp_closed(2050);
  fx.actions.calls.clear();

  // Late inverter bytes must not produce a frame on the next connection.
  const std::vector<uint8_t> resp = {0x01, 0x03, 0x02, 0x00, 0x2A, 0x38, 0x49};
  fx.core.on_uart_data(resp.data(), resp.size(), 2100);
  fx.core.loop(4050);  // past reconnect_min -> tcp_connect only
  fx.core.on_tcp_connected(4100);
  fx.core.loop(4200);
  for (const auto &call : fx.actions.take()) {
    CHECK(call.kind != "uart_send");
    if (call.kind == "tcp_send") {
      // only the connect heartbeat is allowed
      CHECK(to_hex(call.data).substr(4) == std::string(HEARTBEAT_HEX).substr(4));
    }
  }
}

TEST(core_static_server_endpoint_without_discovery) {
  Fixture fx;
  fx.core.set_server_endpoint("192.0.2.50", 8899, 500);
  fx.core.loop(500);
  const auto calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(calls[0].kind, "tcp_connect");
  CHECK_STR(calls[0].host, "192.0.2.50");
}

TEST(core_vdtu_capabilities_string) {
  CollectorProfile profile = Fixture::make_profile();
  CoreConfig config;
  CHECK_STR(build_vdtu_capabilities(profile, config),
            "esp-collector,0.1.4;features=local_only,no_cloud,wifi_params,endpoint_write,reboot;uart=2400,8,1,NONE;"
            "spacing_ms=850;queue=4");

  config.command_spacing_ms = 100;
  config.forward_queue_limit = 8;
  profile.uart = "9600,8,1,NONE";
  CHECK_STR(build_vdtu_capabilities(profile, config),
            "esp-collector,0.1.4;features=local_only,no_cloud,wifi_params,endpoint_write,reboot;uart=9600,8,1,NONE;"
            "spacing_ms=100;queue=8");
}

TEST(core_vdtu_served_over_tcp) {
  MockActions actions;
  CollectorProfile profile = Fixture::make_profile();
  CoreConfig config;
  profile.vdtu = build_vdtu_capabilities(profile, config);
  CollectorCore core(&actions, profile, config);

  const std::string discovery = "set>server=192.0.2.10:8899;";
  core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 1000);
  core.loop(1000);
  core.on_tcp_connected(1100);
  actions.calls.clear();

  const std::string line = "AT+VDTU?\r\n";
  core.on_tcp_data(reinterpret_cast<const uint8_t *>(line.data()), line.size(), 2000);
  const auto calls = actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(std::string(calls[0].data.begin(), calls[0].data.end()),
            "AT+VDTU:esp-collector,0.1.4;features=local_only,no_cloud,wifi_params,endpoint_write,reboot;uart=2400,8,1,NONE;"
            "spacing_ms=850;queue=4\r\n");
}

namespace {

// Mock with parameter hooks, emulating the ESPHome glue's WiFi handlers.
class ParamActions : public MockActions {
 public:
  std::vector<std::pair<uint8_t, std::string>> writes;

  bool query_param(uint8_t parameter, std::string *out) override {
    if (parameter == 41) {
      *out = "bench-net";
      return true;
    }
    if (parameter == 49) {
      *out = "[bench-net,-40][synthetic-guest,-71]";
      return true;
    }
    return false;
  }

  uint8_t set_param(uint8_t parameter, const std::string &value) override {
    writes.emplace_back(parameter, value);
    return (parameter == 41 || parameter == 43 || parameter == 29) ? 0 : 1;
  }
};

}  // namespace

TEST(core_fc2_params_from_core_state) {
  Fixture fx;
  fx.connect();

  // param 2 collector_pn -> {0, 2} + PN text
  fx.feed_frame(0x21, 0x0994, 0x10, FC_QUERY_COLLECTOR, {2}, 2000);
  auto calls = fx.actions.take();
  CHECK(calls.size() == 1);
  const std::string pn_payload(calls[0].data.begin() + 10, calls[0].data.end());
  CHECK(calls[0].data[8] == 0 && calls[0].data[9] == 2);
  CHECK_STR(pn_payload, "V00000200000000001");

  // param 34 serial_baudrate -> leading uart field
  fx.feed_frame(0x22, 0x0994, 0x10, FC_QUERY_COLLECTOR, {34}, 2001);
  calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK(calls[0].data[8] == 0 && calls[0].data[9] == 34);
  CHECK_STR(std::string(calls[0].data.begin() + 10, calls[0].data.end()), "2400");

  // param 21 domain_address_1 -> current discovery endpoint
  fx.feed_frame(0x23, 0x0994, 0x10, FC_QUERY_COLLECTOR, {21}, 2002);
  calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(std::string(calls[0].data.begin() + 10, calls[0].data.end()), "192.0.2.10,8899,TCP");

  // unknown param without a platform hook -> {1, param}
  fx.feed_frame(0x24, 0x0994, 0x10, FC_QUERY_COLLECTOR, {55}, 2003);
  calls = fx.actions.take();
  CHECK(calls.size() == 1);
  CHECK_HEX(calls[0].data, "002409940004100201" "37");
}

TEST(core_fc2_param_via_platform_hook) {
  ParamActions actions;
  CollectorCore core(&actions, Fixture::make_profile(), CoreConfig{});
  const std::string discovery = "set>server=192.0.2.10:8899;";
  core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 1000);
  core.loop(1000);
  core.on_tcp_connected(1100);
  actions.calls.clear();

  const std::vector<uint8_t> query = build_frame(0x31, 0x0994, 0x10, FC_QUERY_COLLECTOR,
                                                 (const uint8_t[]){41}, 1);
  core.on_tcp_data(query.data(), query.size(), 2000);
  auto calls = actions.take();
  CHECK(calls.size() == 1);
  CHECK(calls[0].data[8] == 0 && calls[0].data[9] == 41);
  CHECK_STR(std::string(calls[0].data.begin() + 10, calls[0].data.end()), "bench-net");
}

TEST(core_fc3_write_via_platform_hook) {
  ParamActions actions;
  CollectorCore core(&actions, Fixture::make_profile(), CoreConfig{});
  const std::string discovery = "set>server=192.0.2.10:8899;";
  core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 1000);
  core.loop(1000);
  core.on_tcp_connected(1100);
  actions.calls.clear();

  // FC=3 payload = param byte + ascii value (smartess_local.build_set_collector_payload)
  std::vector<uint8_t> set_payload = {41};
  const char *ssid = "synthetic-net";
  set_payload.insert(set_payload.end(), ssid, ssid + 13);
  const std::vector<uint8_t> frame =
      build_frame(0x41, 0x0994, 0x10, FC_SET_COLLECTOR, set_payload.data(), set_payload.size());
  core.on_tcp_data(frame.data(), frame.size(), 2000);

  auto calls = actions.take();
  CHECK(calls.size() == 1);
  CHECK_HEX(calls[0].data, "004109940004100300" "29");  // status 0, param 41
  CHECK(actions.writes.size() == 1);
  CHECK(actions.writes[0].first == 41);
  CHECK_STR(actions.writes[0].second, "synthetic-net");

  // Refused parameter -> status 1 (factory default behavior preserved)
  const std::vector<uint8_t> refused =
      build_frame(0x42, 0x0994, 0x10, FC_SET_COLLECTOR, (const uint8_t[]){21, '1'}, 2);
  core.on_tcp_data(refused.data(), refused.size(), 2001);
  calls = actions.take();
  CHECK(calls.size() == 1);
  CHECK_HEX(calls[0].data, "004209940004100301" "15");
}

namespace {

class RestartParamActions : public MockActions {
 public:
  bool reboot_scheduled = false;
  bool endpoint_committed = false;
  std::string pending_endpoint;

  uint8_t set_param(uint8_t parameter, const std::string &value) override {
    if (parameter == 21) {
      pending_endpoint = value;
      return 0;
    }
    if (parameter != 29) {
      return 1;
    }
    if (!pending_endpoint.empty()) {
      endpoint_committed = true;
      pending_endpoint.clear();
      return 0;
    }
    reboot_scheduled = true;
    return 0;
  }
};

std::vector<uint8_t> set_param_frame(uint16_t tid, uint8_t parameter, const char *value) {
  std::vector<uint8_t> payload = {parameter};
  while (*value != '\0') {
    payload.push_back(static_cast<uint8_t>(*value++));
  }
  return build_frame(tid, 0x0994, 0x10, FC_SET_COLLECTOR, payload.data(), payload.size());
}

}  // namespace

TEST(core_fc3_param29_restart_when_nothing_staged) {
  RestartParamActions actions;
  CollectorCore core(&actions, Fixture::make_profile(), CoreConfig{});
  const std::string discovery = "set>server=192.0.2.10:8899;";
  core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 1000);
  core.loop(1000);
  core.on_tcp_connected(1100);
  actions.calls.clear();

  const std::vector<uint8_t> restart = set_param_frame(0x43, 29, "1");
  core.on_tcp_data(restart.data(), restart.size(), 2000);

  const auto calls = actions.take();
  CHECK(calls.size() == 1);
  CHECK_HEX(calls[0].data, "004309940004100300" "1d");  // status 0, param 29
  CHECK(actions.reboot_scheduled);
  CHECK(!actions.endpoint_committed);
}

TEST(core_fc3_param29_commits_staged_endpoint_without_restart) {
  RestartParamActions actions;
  CollectorCore core(&actions, Fixture::make_profile(), CoreConfig{});
  const std::string discovery = "set>server=192.0.2.10:8899;";
  core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 1000);
  core.loop(1000);
  core.on_tcp_connected(1100);
  actions.calls.clear();

  const std::vector<uint8_t> stage = set_param_frame(0x44, 21, "192.0.2.10,8899,TCP");
  core.on_tcp_data(stage.data(), stage.size(), 2000);
  auto calls = actions.take();
  CHECK(calls.size() == 1);
  CHECK_HEX(calls[0].data, "004409940004100300" "15");  // status 0, param 21

  const std::vector<uint8_t> apply = set_param_frame(0x45, 29, "1");
  core.on_tcp_data(apply.data(), apply.size(), 2001);
  calls = actions.take();
  CHECK(calls.size() == 1);
  CHECK_HEX(calls[0].data, "004509940004100300" "1d");  // status 0, param 29
  CHECK(actions.endpoint_committed);
  CHECK(!actions.reboot_scheduled);
}

namespace {

class UartActions : public MockActions {
 public:
  std::string uart_write_value;
  void apply_uart(const std::string &value) override { uart_write_value = value; }
};

}  // namespace

TEST(core_runtime_uart_reconfiguration) {
  UartActions actions;
  CollectorProfile profile = Fixture::make_profile();
  CoreConfig config;
  profile.vdtu = build_vdtu_capabilities(profile, config);
  CollectorCore core(&actions, profile, config);

  const std::string discovery = "set>server=192.0.2.10:8899;";
  core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 1000);
  core.loop(1000);
  core.on_tcp_connected(1100);
  actions.calls.clear();

  // AT+UART= write reaches the platform hook and is acked with W000.
  const std::string write = "AT+UART=9600,8,1,NONE\r\n";
  core.on_tcp_data(reinterpret_cast<const uint8_t *>(write.data()), write.size(), 2000);
  auto calls = actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(std::string(calls[0].data.begin(), calls[0].data.end()), "AT+UART:W000\r\n");
  CHECK_STR(actions.uart_write_value, "9600,8,1,NONE");

  // The platform applies the change and reports it back to the core...
  core.update_uart_description("9600,8,1,NONE");

  // ...which is then visible via AT+UART?, FC=2 param 34 and AT+VDTU.
  const std::string query = "AT+UART?\r\n";
  core.on_tcp_data(reinterpret_cast<const uint8_t *>(query.data()), query.size(), 2001);
  calls = actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(std::string(calls[0].data.begin(), calls[0].data.end()), "AT+UART:9600,8,1,NONE\r\n");

  const std::vector<uint8_t> param34 = build_frame(0x51, 0x0994, 0x10, FC_QUERY_COLLECTOR,
                                                   (const uint8_t[]){34}, 1);
  core.on_tcp_data(param34.data(), param34.size(), 2002);
  calls = actions.take();
  CHECK(calls.size() == 1);
  CHECK_STR(std::string(calls[0].data.begin() + 10, calls[0].data.end()), "9600");

  const std::string vdtu_query = "AT+VDTU?\r\n";
  core.on_tcp_data(reinterpret_cast<const uint8_t *>(vdtu_query.data()), vdtu_query.size(), 2003);
  calls = actions.take();
  CHECK(calls.size() == 1);
  const std::string vdtu_reply(calls[0].data.begin(), calls[0].data.end());
  CHECK(vdtu_reply.find(";uart=9600,8,1,NONE;") != std::string::npos);
}

TEST(parse_server_endpoint_forms) {
  std::string host;
  uint16_t port = 0;
  CHECK(parse_server_endpoint("192.0.2.10,8899,TCP", &host, &port));
  CHECK_STR(host, "192.0.2.10");
  CHECK(port == 8899);

  CHECK(parse_server_endpoint("192.0.2.20:9000", &host, &port));
  CHECK_STR(host, "192.0.2.20");
  CHECK(port == 9000);

  CHECK(parse_server_endpoint("192.0.2.30,8899", &host, &port));
  CHECK_STR(host, "192.0.2.30");
  CHECK(port == 8899);

  CHECK(parse_server_endpoint("ha.local,1234,TCP", &host, &port));
  CHECK_STR(host, "ha.local");
  CHECK(port == 1234);

  CHECK(!parse_server_endpoint("", &host, &port));
  CHECK(!parse_server_endpoint("192.0.2.10", &host, &port));
  CHECK(!parse_server_endpoint("192.0.2.10,0,TCP", &host, &port));
  CHECK(!parse_server_endpoint("192.0.2.10,99999", &host, &port));
  CHECK(!parse_server_endpoint(",8899,TCP", &host, &port));
}

namespace {

// Records endpoint-change notifications for persistence assertions.
class EndpointActions : public MockActions {
 public:
  std::vector<std::pair<std::string, uint16_t>> endpoint_changes;
  void on_server_endpoint_changed(const std::string &host, uint16_t port) override {
    endpoint_changes.emplace_back(host, port);
  }
};

}  // namespace

TEST(core_endpoint_change_notifies_for_persistence) {
  EndpointActions actions;
  CollectorCore core(&actions, Fixture::make_profile(), CoreConfig{});

  const std::string discovery = "set>server=192.0.2.10:8899;";
  core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 1000);
  CHECK(actions.endpoint_changes.size() == 1);
  CHECK_STR(actions.endpoint_changes[0].first, "192.0.2.10");
  CHECK(actions.endpoint_changes[0].second == 8899);

  // Same endpoint again -> no new notification (no redundant NVS write).
  core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 1100);
  CHECK(actions.endpoint_changes.size() == 1);

  // A different discovery endpoint -> one more notification.
  const std::string moved = "set>server=192.0.2.20:8899;";
  core.on_udp_datagram(reinterpret_cast<const uint8_t *>(moved.data()), moved.size(), 1200);
  CHECK(actions.endpoint_changes.size() == 2);
  CHECK_STR(actions.endpoint_changes[1].first, "192.0.2.20");
}

TEST(core_at_cldsrvhost1_write_retargets_link) {
  EndpointActions actions;
  CollectorCore core(&actions, Fixture::make_profile(), CoreConfig{});
  const std::string discovery = "set>server=192.0.2.10:8899;";
  core.on_udp_datagram(reinterpret_cast<const uint8_t *>(discovery.data()), discovery.size(), 1000);
  core.loop(1000);
  core.on_tcp_connected(1100);
  actions.calls.clear();
  actions.endpoint_changes.clear();

  // Writing a new cloud host retargets the reverse TCP link and re-persists.
  const std::string line = "AT+CLDSRVHOST1=192.0.2.50,8899,TCP\r\n";
  core.on_tcp_data(reinterpret_cast<const uint8_t *>(line.data()), line.size(), 2000);

  bool closed = false;
  std::string reply;
  for (const auto &call : actions.calls) {
    if (call.kind == "tcp_close") closed = true;
    if (call.kind == "tcp_send") reply.assign(call.data.begin(), call.data.end());
  }
  CHECK(closed);  // dropped the old link to move to the new endpoint
  CHECK_STR(reply, "AT+CLDSRVHOST1:W000\r\n");  // ack stays factory-mirror
  CHECK(actions.endpoint_changes.size() == 1);
  CHECK_STR(actions.endpoint_changes[0].first, "192.0.2.50");

  // After reconnect, FC=2 param 21 read reports the new endpoint (no lie).
  core.loop(4000);
  core.on_tcp_connected(4100);
  actions.calls.clear();
  const std::vector<uint8_t> q21 = build_frame(0x60, 0x0994, 0x10, FC_QUERY_COLLECTOR,
                                               (const uint8_t[]){21}, 1);
  core.on_tcp_data(q21.data(), q21.size(), 4200);
  const auto calls = actions.take();
  CHECK(calls.size() == 1);
  CHECK(calls[0].data[8] == 0 && calls[0].data[9] == 21);
  CHECK_STR(std::string(calls[0].data.begin() + 10, calls[0].data.end()), "192.0.2.50,8899,TCP");
}

TEST(core_vdtu_advertises_endpoint_write_and_reboot) {
  CollectorProfile profile = Fixture::make_profile();
  const std::string capabilities = build_vdtu_capabilities(profile, CoreConfig{});
  CHECK(capabilities.find("endpoint_write") != std::string::npos);
  CHECK(capabilities.find("reboot") != std::string::npos);
}

TEST(parse_uart_settings_forms) {
  UartSettings s;
  CHECK(parse_uart_settings("2400,8,1,NONE", &s));
  CHECK(s.baud == 2400 && s.data_bits == 8 && s.stop_bits == 1 && s.parity == UartParity::NONE);

  CHECK(parse_uart_settings("9600,8,1,EVEN", &s));
  CHECK(s.baud == 9600 && s.parity == UartParity::EVEN);

  CHECK(parse_uart_settings("19200,7,2,ODD", &s));
  CHECK(s.baud == 19200 && s.data_bits == 7 && s.stop_bits == 2 && s.parity == UartParity::ODD);

  // baud-only keeps 8N1 defaults
  CHECK(parse_uart_settings("4800", &s));
  CHECK(s.baud == 4800 && s.data_bits == 8 && s.stop_bits == 1 && s.parity == UartParity::NONE);

  // single-letter parity, lower case
  CHECK(parse_uart_settings("2400,8,1,e", &s));
  CHECK(s.parity == UartParity::EVEN);

  // malformed -> rejected
  CHECK(!parse_uart_settings("", &s));
  CHECK(!parse_uart_settings("abc", &s));
  CHECK(!parse_uart_settings(",8,1,NONE", &s));
  CHECK(!parse_uart_settings("0,8,1,NONE", &s));
}
