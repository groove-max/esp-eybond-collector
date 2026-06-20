#include "ble_provisioning.h"
#include "minitest.h"

using namespace eybond;

namespace {

// Mock Actions that records what the parser asked the platform to do.
struct MockBleActions : public BleProvisioning::Actions {
  std::string fw = "8.50.12.3";
  std::string at = "1.11";
  bool connected = false;
  std::vector<BleWifiNetwork> scan;

  // Captured side effects.
  int apply_calls = 0;
  std::string last_ssid;
  std::string last_password;

  std::string ble_fw_version() override { return fw; }
  std::string ble_at_version() override { return at; }
  void ble_apply_wifi(const std::string &ssid, const std::string &password) override {
    apply_calls++;
    last_ssid = ssid;
    last_password = password;
  }
  bool ble_wifi_connected() override { return connected; }
  std::vector<BleWifiNetwork> ble_wifi_scan() override { return scan; }
};

}  // namespace

TEST(ble_version_probes) {
  MockBleActions actions;
  BleProvisioning prov(&actions);
  CHECK_STR(prov.handle_command("AT+FWVER?"), "AT+FWVER:8.50.12.3");
  CHECK_STR(prov.handle_command("AT+ATVER?"), "AT+ATVER:1.11");
}

TEST(ble_tolerates_crlf_and_whitespace) {
  MockBleActions actions;
  BleProvisioning prov(&actions);
  // The client sends some commands with a trailing CRLF (append_crlf=True).
  CHECK_STR(prov.handle_command("AT+FWVER?\r\n"), "AT+FWVER:8.50.12.3");
  CHECK_STR(prov.handle_command("  AT+ATVER?  "), "AT+ATVER:1.11");
}

TEST(ble_wflkap_applies_ssid_and_password) {
  MockBleActions actions;
  BleProvisioning prov(&actions);
  // Exact format the integration emits: AT+WFLKAP={ssid},AES,WPA2_PSK,{password}
  CHECK_STR(prov.handle_command("AT+WFLKAP=ExampleSSID,AES,WPA2_PSK,ExamplePassword"), "AT+WFLKAP:W000");
  CHECK(actions.apply_calls == 1);
  CHECK_STR(actions.last_ssid, "ExampleSSID");
  CHECK_STR(actions.last_password, "ExamplePassword");
}

TEST(ble_wflkap_password_may_contain_commas) {
  MockBleActions actions;
  BleProvisioning prov(&actions);
  // The 4th field is the remainder, so commas in the password survive.
  CHECK_STR(prov.handle_command("AT+WFLKAP=Net,AES,WPA2_PSK,a,b,c"), "AT+WFLKAP:W000");
  CHECK_STR(actions.last_ssid, "Net");
  CHECK_STR(actions.last_password, "a,b,c");
}

TEST(ble_link_status_reflects_connection) {
  MockBleActions actions;
  BleProvisioning prov(&actions);
  actions.connected = false;
  CHECK_STR(prov.handle_command("AT+LINK?"), "AT+LINK:W051");  // degraded while joining
  actions.connected = true;
  CHECK_STR(prov.handle_command("AT+LINK?"), "AT+LINK:W000");  // success once associated
}

TEST(ble_intpara_branch_stages_then_applies) {
  MockBleActions actions;
  BleProvisioning prov(&actions);
  CHECK_STR(prov.handle_command("AT+INTPARA=41,MyNet"), "AT+INTPARA:W000");
  CHECK(actions.apply_calls == 0);  // ssid only staged, not applied yet
  CHECK_STR(prov.handle_command("AT+INTPARA=43,secretpw"), "AT+INTPARA:W000");
  CHECK(actions.apply_calls == 1);
  CHECK_STR(actions.last_ssid, "MyNet");
  CHECK_STR(actions.last_password, "secretpw");
  CHECK_STR(prov.handle_command("AT+INTPARA=29,1"), "AT+INTPARA:W000");  // commit/restart
}

TEST(ble_intpara48_status) {
  MockBleActions actions;
  BleProvisioning prov(&actions);
  actions.connected = false;
  // station=0,cloud=1 -> DEGRADED (W051) in parse_intpara48_provision_result.
  CHECK_STR(prov.handle_command("AT+INTPARA48?"), "AT+INTPARA:48,0,0,1");
  actions.connected = true;
  // detected=1,station=0,cloud=0 -> SUCCESS (W000).
  CHECK_STR(prov.handle_command("AT+INTPARA48?"), "AT+INTPARA:48,1,0,0");
}

TEST(ble_wifi_scan_list_format) {
  MockBleActions actions;
  actions.scan = {{"HomeNet", -45}, {"Neighbor", -78}};
  BleProvisioning prov(&actions);
  // parse_wifi_scan_response splits on "AT+INTPARA:49," then matches [ssid,sig].
  CHECK_STR(prov.handle_command("AT+INTPARA49?"), "AT+INTPARA:49,[HomeNet,-45][Neighbor,-78]");
}

TEST(ble_wifi_scan_skips_unframeable_ssids) {
  MockBleActions actions;
  actions.scan = {{"Good", -50}, {"Bad,SSID", -60}, {"Bracket]", -61}, {"", -62}};
  BleProvisioning prov(&actions);
  CHECK_STR(prov.handle_command("AT+INTPARA49?"), "AT+INTPARA:49,[Good,-50]");
}

TEST(ble_empty_scan_list) {
  MockBleActions actions;
  BleProvisioning prov(&actions);
  CHECK_STR(prov.handle_command("AT+INTPARA49?"), "AT+INTPARA:49,");
}

TEST(ble_wifi_scan_list_capped_to_mtu_budget) {
  // Many long-named networks (like a busy area) must not produce a notification
  // longer than the MTU budget, or the integration times out reading the list.
  MockBleActions actions;
  for (int i = 0; i < 40; i++) {
    actions.scan.push_back({std::string("LongNetworkName_") + std::to_string(i), -40 - i});
  }
  BleProvisioning prov(&actions);
  const std::string resp = prov.handle_command("AT+INTPARA49?");
  CHECK(resp.size() <= 180);                       // within the conservative budget
  CHECK(resp.rfind("AT+INTPARA:49,", 0) == 0);     // still well-formed
  CHECK(resp.find("[LongNetworkName_0,") != std::string::npos);  // strongest kept
  // The response stays parseable: it ends on a complete [..] entry (never mid-entry).
  CHECK(resp.back() == ']');
}

TEST(ble_unknown_command_is_silent) {
  MockBleActions actions;
  BleProvisioning prov(&actions);
  CHECK_STR(prov.handle_command("AT+SOMETHINGELSE?"), "");
  CHECK_STR(prov.handle_command(""), "");
  CHECK_STR(prov.handle_command("\r\n"), "");
  CHECK(actions.apply_calls == 0);
}

TEST(ble_manufacturer_data_layout) {
  // [0xE5,0x02] (Espressif company id LE) + ASCII PN.
  const std::vector<uint8_t> data = build_ble_manufacturer_data("V00000200000000001");
  CHECK(data.size() == 2 + 18);
  CHECK(data[0] == 0xE5);
  CHECK(data[1] == 0x02);
  CHECK(data[2] == 'V');
  CHECK(data[3] == '0');
  CHECK(data[4] == '0');
  // The PN bytes round-trip as text for the integration's PN extraction.
  std::string pn(data.begin() + 2, data.end());
  CHECK_STR(pn, "V00000200000000001");
}
