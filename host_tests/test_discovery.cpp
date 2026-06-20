#include "discovery.h"
#include "minitest.h"

using namespace eybond;

namespace {
bool parse_text(const std::string &text, DiscoveryRedirect *out) {
  return parse_discovery_redirect(reinterpret_cast<const uint8_t *>(text.data()), text.size(), out);
}
}  // namespace

TEST(discovery_parses_all_integration_variants) {
  // discovery.py build_discovery_messages emits: bare, "\r\n"-suffixed, "\n"-suffixed
  for (const std::string &variant : {std::string("set>server=192.0.2.10:8899;"),
                                     std::string("set>server=192.0.2.10:8899;\r\n"),
                                     std::string("set>server=192.0.2.10:8899;\n")}) {
    DiscoveryRedirect redirect;
    CHECK(parse_text(variant, &redirect));
    CHECK_STR(redirect.server_ip, "192.0.2.10");
    CHECK(redirect.server_port == 8899);
  }
}

TEST(discovery_rejects_invalid) {
  DiscoveryRedirect redirect;
  CHECK(!parse_text("rsp>server=2;", &redirect));
  CHECK(!parse_text("set>server=192.0.2.10:8899", &redirect));  // no ';'
  CHECK(!parse_text("set>server=192.0.2.10;", &redirect));      // no port
  CHECK(!parse_text("set>server=:8899;", &redirect));           // no host
  CHECK(!parse_text("set>server=192.0.2.10:99999;", &redirect));
  CHECK(!parse_text("set>server=192.0.2.10:0;", &redirect));
  CHECK(!parse_text("set>server=192.0.2.10:88x9;", &redirect));
  CHECK(!parse_text("", &redirect));
}

TEST(pn_synthesis_format) {
  const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42};
  const std::string pn = synthesize_pn(mac);
  // PN18 format: letter + 17 digits, "V00" synthetic prefix — a neutral,
  // non-eybond prefix (real collectors start with E) per the project rule.
  CHECK(pn.size() == 18);
  CHECK(pn[0] == 'V');
  CHECK(pn[1] == '0');
  CHECK(pn[2] == '0');
  for (size_t i = 1; i < pn.size(); i++) {
    CHECK(pn[i] >= '0' && pn[i] <= '9');
  }
  // 0xDEADBEEF0042 = 244837814042690 -> zero-padded to 15 digits
  CHECK_STR(pn, "V00244837814042690");
}

TEST(pn_synthesis_small_mac_zero_padded) {
  const uint8_t mac[6] = {0, 0, 0, 0, 0, 7};
  CHECK_STR(synthesize_pn(mac), "V00000000000000007");
}
