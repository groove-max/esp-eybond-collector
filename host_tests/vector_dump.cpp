// Dumps the C++ core's wire bytes for cross-checking against the integration's
// python builders (see cross_check.py). Output: one "name<TAB>hex" line each.
#include <cstdio>
#include <string>
#include <vector>

#include "at_handler.h"
#include "core.h"
#include "discovery.h"
#include "frame.h"

using namespace eybond;

static void dump(const char *name, const std::vector<uint8_t> &data) {
  std::printf("%s\t", name);
  for (uint8_t byte : data) {
    std::printf("%02x", byte);
  }
  std::printf("\n");
}

static void dump_str(const char *name, const std::string &text) {
  dump(name, std::vector<uint8_t>(text.begin(), text.end()));
}

int main() {
  CollectorProfile profile;
  profile.pn = "V00000200000000001";
  profile.uart = "2400,8,1,NONE";

  AtRuntimeValues runtime;
  runtime.cloud_endpoint = "192.0.2.10,8899,TCP";
  runtime.wifi_rssi = "-55";
  runtime.time_string = "20260613120000";

  uint8_t header[HEADER_SIZE];
  encode_header(header, 0x1234, 0x0994, 10, 0x10, 4);
  dump("encode_header", std::vector<uint8_t>(header, header + HEADER_SIZE));

  std::string pn14 = profile.pn.substr(0, 14);
  pn14.resize(14, '\0');
  dump("heartbeat", build_frame(0x8001, 0x0000, 0x01, FC_HEARTBEAT,
                                reinterpret_cast<const uint8_t *>(pn14.data()), pn14.size()));

  std::vector<uint8_t> fc2 = {0x00, 0x05};
  fc2.insert(fc2.end(), profile.firmware_version.begin(), profile.firmware_version.end());
  dump("fc2_param5", build_frame(7, 0x0994, 0x10, FC_QUERY_COLLECTOR, fc2.data(), fc2.size()));

  profile.vdtu = build_vdtu_capabilities(profile, CoreConfig{});
  const char *queries[] = {"DTUPN", "ATVER", "ENUPMODE", "SYST", "WFSS",  "UART",      "DTUTYPE",
                           "FWVER", "CLDSRVHOST1", "HTBT", "LINK", "INTPARA49", "VDTU", "UNKNOWNCMD"};
  for (const char *name : queries) {
    AtCommand command;
    const std::string line = std::string("AT+") + name + "?\r\n";
    if (!parse_at_line(line, &command)) {
      std::printf("at_%s\tPARSE_FAILED\n", name);
      continue;
    }
    dump_str((std::string("at_") + name).c_str(), build_at_reply(command, profile, runtime));
  }

  AtCommand write_command;
  parse_at_line("AT+CLDSRVHOST1=192.0.2.99,9999,TCP\r\n", &write_command);
  dump_str("at_write_ack", build_at_reply(write_command, profile, runtime));

  const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42};
  dump_str("pn_synth", synthesize_pn(mac));
  return 0;
}
