// Reply vectors generated with fake_collector_lib.build_at_reply (the behavior spec).
#include "at_handler.h"
#include "minitest.h"

using namespace eybond;

namespace {

CollectorProfile test_profile() {
  CollectorProfile profile;
  profile.pn = "V00000200000000001";
  profile.uart = "2400,8,1,NONE";
  return profile;
}

AtRuntimeValues test_runtime() {
  AtRuntimeValues runtime;
  runtime.cloud_endpoint = "192.0.2.10,8899,TCP";
  runtime.wifi_rssi = "-55";
  runtime.time_string = "20260613120000";
  return runtime;
}

std::string reply_for(const std::string &line) {
  AtCommand command;
  if (!parse_at_line(line, &command)) {
    return "<parse failed>";
  }
  return build_at_reply(command, test_profile(), test_runtime());
}

}  // namespace

TEST(at_parse_query) {
  AtCommand command;
  CHECK(parse_at_line("AT+WFSS?\r\n", &command));
  CHECK_STR(command.command, "WFSS");
  CHECK(!command.is_write);
}

TEST(at_parse_write) {
  AtCommand command;
  CHECK(parse_at_line("AT+CLDSRVHOST1=192.0.2.99,9999,TCP\r\n", &command));
  CHECK_STR(command.command, "CLDSRVHOST1");
  CHECK(command.is_write);
  CHECK_STR(command.value, "192.0.2.99,9999,TCP");
}

TEST(at_parse_rejects_garbage) {
  AtCommand command;
  CHECK(!parse_at_line("QPIGS\r\n", &command));
  CHECK(!parse_at_line("AT+NOSEPARATOR\r\n", &command));
  CHECK(!parse_at_line("", &command));
}

TEST(at_replies_match_python_spec) {
  CHECK_STR(reply_for("AT+DTUPN?\r\n"), "AT+DTUPN:V00000200000000001\r\n");
  CHECK_STR(reply_for("AT+ATVER?\r\n"), "AT+ATVER:1.11\r\n");
  CHECK_STR(reply_for("AT+ENUPMODE?\r\n"), "AT+ENUPMODE:OFF\r\n");
  CHECK_STR(reply_for("AT+WFSS?\r\n"), "AT+WFSS:-55\r\n");
  CHECK_STR(reply_for("AT+UART?\r\n"), "AT+UART:2400,8,1,NONE\r\n");
  CHECK_STR(reply_for("AT+DTUTYPE?\r\n"), "AT+DTUTYPE:Wi-Fi.DTU\r\n");
  CHECK_STR(reply_for("AT+FWVER?\r\n"), "AT+FWVER:8.50.12.3\r\n");
  CHECK_STR(reply_for("AT+CLDSRVHOST1?\r\n"), "AT+CLDSRVHOST1:192.0.2.10,8899,TCP\r\n");
  CHECK_STR(reply_for("AT+HTBT?\r\n"), "AT+HTBT:\r\n");
  CHECK_STR(reply_for("AT+LINK?\r\n"), "AT+LINK:connected\r\n");
  CHECK_STR(reply_for("AT+INTPARA49?\r\n"), "AT+INTPARA49:\r\n");
  CHECK_STR(reply_for("AT+UNKNOWNCMD?\r\n"), "AT+UNKNOWNCMD:\r\n");
  CHECK_STR(reply_for("AT+SYST?\r\n"), "AT+SYST:20260613120000\r\n");
}

TEST(at_write_acked_with_w000) {
  CHECK_STR(reply_for("AT+CLDSRVHOST1=192.0.2.99,9999,TCP\r\n"), "AT+CLDSRVHOST1:W000\r\n");
  CHECK_STR(reply_for("AT+HTBT=30\r\n"), "AT+HTBT:W000\r\n");
}

TEST(at_lower_case_normalized) {
  CHECK_STR(reply_for("at+wfss?\r\n"), "<parse failed>");  // prefix must be exact "AT+" on the wire
  AtCommand command;
  CHECK(parse_at_line("AT+wfss?\r\n", &command));
  CHECK_STR(command.command, "WFSS");
}

TEST(at_vdtu_capability_probe) {
  // Default profile: no capability string -> byte-identical to the factory
  // reference reply for an unknown command.
  CHECK_STR(reply_for("AT+VDTU?\r\n"), "AT+VDTU:\r\n");

  CollectorProfile profile = test_profile();
  profile.vdtu = "esp-collector,0.1.1;features=local_only,no_cloud,wifi_params,endpoint_write;uart=2400,8,1,NONE;spacing_ms=850;queue=4";
  AtCommand command;
  CHECK(parse_at_line("AT+VDTU?\r\n", &command));
  CHECK_STR(build_at_reply(command, profile, test_runtime()),
            "AT+VDTU:esp-collector,0.1.1;features=local_only,no_cloud,wifi_params,endpoint_write;uart=2400,8,1,NONE;"
            "spacing_ms=850;queue=4\r\n");
}
