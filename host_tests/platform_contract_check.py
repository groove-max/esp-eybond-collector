#!/usr/bin/env python3
"""Exercise production glue methods with deterministic IO substitutes.

The full ESPHome translation unit is compiled separately for every supported
board. Here we execute the actual method bodies, not reimplementations. The
offline call ordering check is structural; this does not simulate an ESP32 radio.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "components/eybond_collector/eybond_collector.cpp").read_text()


def body(signature):
    start = SOURCE.index(signature)
    opening = SOURCE.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        end += 1
    return SOURCE[start:end]


def main():
    loop = body("void EybondCollector::loop()")
    assert loop.index("process_wifi_apply_(now)") < loop.index("if (!network::is_connected())")
    assert loop.index("core_->on_uart_data") < loop.index("core_->on_tcp_data")
    fixture = r'''
#include <cassert>
#include <cstdint>
#include <string>
#define USE_WIFI
#define ESP_LOGI(...) ((void)0)
struct Mutex {};
struct LockGuard { explicit LockGuard(Mutex &) {} };
uint32_t millis() { return 2000; }
namespace wifi {
struct Wifi {
  int calls = 0;
  std::string ssid, password;
  void save_wifi_sta(const std::string &s, const std::string &p) { ++calls; ssid=s; password=p; }
} instance;
Wifi *global_wifi_component = &instance;
}
struct Core { int closed = 0; void on_tcp_closed(uint32_t) { ++closed; } } core;
struct Tcp {
  size_t sent = 0; int writes = 0, stops = 0;
  size_t write(const uint8_t *, size_t) { ++writes; return sent; }
  void stop() { ++stops; }
};
class EybondCollector {
public:
  Mutex settings_mutex_;
  std::string pending_wifi_ssid_, pending_wifi_password_;
  bool wifi_apply_requested_ = false, tcp_up_ = true;
  uint32_t wifi_apply_at_ms_ = 0;
  Tcp tcp_;
  Core *core_ = &core;
  void process_wifi_apply_(uint32_t now);
  void tcp_send(const uint8_t *, size_t);
};
'''
    fixture += body("void EybondCollector::process_wifi_apply_(uint32_t now)")
    fixture += body("void EybondCollector::tcp_send(const uint8_t *data, size_t len)")
    fixture += r'''
int main() {
  EybondCollector c;
  c.pending_wifi_ssid_ = "test-network";
  c.pending_wifi_password_ = "synthetic-password";
  c.wifi_apply_requested_ = true;
  c.wifi_apply_at_ms_ = 200;
  c.process_wifi_apply_(199);
  assert(wifi::instance.calls == 0 && c.wifi_apply_requested_);
  c.process_wifi_apply_(200);
  assert(wifi::instance.calls == 1 && !c.wifi_apply_requested_);
  assert(wifi::instance.ssid == "test-network");
  assert(c.pending_wifi_ssid_.empty() && c.pending_wifi_password_.empty());
  c.process_wifi_apply_(201);
  assert(wifi::instance.calls == 1);
  const uint8_t bytes[] = {1,2,3,4};
  c.tcp_.sent = 4;
  c.tcp_send(bytes, 4);
  assert(c.tcp_up_ && core.closed == 0);
  c.tcp_.sent = 2;
  c.tcp_send(bytes, 4);
  assert(!c.tcp_up_ && core.closed == 1 && c.tcp_.stops == 1);
  c.tcp_send(bytes, 4);
  assert(c.tcp_.writes == 2); // no replay or next frame on damaged stream
}
'''
    with tempfile.TemporaryDirectory(prefix="eybond-platform-") as directory:
        source, binary = Path(directory) / "test.cpp", Path(directory) / "test"
        source.write_text(fixture)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("platform_contracts:ok (Wi-Fi apply, offline ordering, partial TCP write)")


if __name__ == "__main__":
    main()
