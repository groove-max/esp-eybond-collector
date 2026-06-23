// CollectorCore: the platform-independent brain of the virtual eybond collector.
//
// Sans-IO design: the platform (ESPHome glue, host simulator, unit test) feeds
// events in (UDP datagram, TCP connect/data/close, UART bytes, time ticks) and
// the core emits actions through the Actions interface. No sockets, no timers,
// no Arduino/ESP-IDF includes — fully testable on the host with g++.
//
// Behavior contract = ha-eybond-local tests/helpers/fake_collector.py:
//   - UDP 58899: "set>server=IP:PORT;" -> reply "rsp>server=2;" + reverse TCP connect
//   - on connect: unsolicited FC=1 heartbeat (payload = PN[:14] NUL-padded), then periodic
//   - server FC=1 -> heartbeat reply with the request tid
//   - server FC=2 param 5 -> {0x00, 0x05} + firmware version; other params -> {0x01, param}
//   - server FC=3 -> platform-controlled collector params (Wi-Fi/endpoint
//     staging, param 29 apply/reboot, UART baud) or {0x01, param} refused
//   - server FC=4 -> payload to UART verbatim, response framed back with the same
//     tid/devcode/devaddr; inverter silence -> NO reply (the integration relies on
//     distinguishing timeout from data)
//   - AT+...\n lines interleaved on the same TCP stream -> reply table (at_handler)
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "at_handler.h"
#include "discovery.h"
#include "frame.h"
#include "profile.h"
#include "stream_splitter.h"

namespace eybond {

struct CoreConfig {
  uint32_t heartbeat_interval_ms = 60000;
  // Inverter response is complete after this much UART silence following >=1 byte.
  uint32_t uart_gap_ms = 60;
  // No first UART byte within this window -> request dropped (timeout semantics).
  uint32_t uart_timeout_ms = 3000;
  // Factory protocol paces RS485 commands ~850 ms apart; keep it for safety.
  uint32_t command_spacing_ms = 850;
  uint32_t reconnect_min_ms = 2000;
  uint32_t reconnect_max_ms = 30000;
  // Identity used for collector-originated frames (heartbeats). devcode 0x0000 maps
  // to the integration's tolerated unknown_0x0000 profile.
  uint16_t heartbeat_devcode = 0x0000;
  uint8_t collector_addr = 0x01;
  size_t forward_queue_limit = 4;
};

// Compose the AT+VDTU capability value the bridge advertises, e.g.
// "esp-collector,0.2.0;features=local_only,no_cloud,reboot;uart=2400,8,1,NONE;spacing_ms=850;queue=4".
// A future integration version can probe this once to detect the virtual bridge;
// factory collectors answer the query differently (empty/garbage), so absence of
// this exact "esp-collector," prefix means "factory".
std::string build_vdtu_capabilities(const CollectorProfile &profile, const CoreConfig &config);

// Parse a collector server endpoint string into host + port. Accepts the
// factory "IP,PORT,TCP" form (FC=2 param 21 / AT+CLDSRVHOST1) as well as
// "IP,PORT" and "IP:PORT". Returns false on a malformed or out-of-range value.
bool parse_server_endpoint(const std::string &value, std::string *host, uint16_t *port);

enum class UartParity : uint8_t { NONE, EVEN, ODD };

struct UartSettings {
  uint32_t baud = 0;
  uint8_t data_bits = 8;
  uint8_t stop_bits = 1;
  UartParity parity = UartParity::NONE;
};

// Parse an AT+UART value "baud,data,stop,parity" (e.g. "2400,8,1,NONE"). Baud
// is required; data/stop/parity are optional and keep their defaults (8/1/NONE)
// when absent. Parity accepts NONE/EVEN/ODD (or N/E/O), case-insensitive.
// Returns false only when the baud field is missing or non-numeric.
bool parse_uart_settings(const std::string &value, UartSettings *out);

class CollectorCore {
 public:
  // Implemented by the platform. Calls happen synchronously inside core event handlers.
  class Actions {
   public:
    virtual ~Actions() = default;
    // Reply to the sender of the datagram currently being processed.
    virtual void udp_reply(const uint8_t *data, size_t len) = 0;
    virtual void tcp_connect(const std::string &host, uint16_t port) = 0;
    virtual void tcp_send(const uint8_t *data, size_t len) = 0;
    virtual void tcp_close() = 0;
    virtual void uart_send(const uint8_t *data, size_t len) = 0;
    virtual std::string wifi_rssi() { return "-50"; }
    // "%Y%m%d%H%M%S" UTC for AT+SYST, empty when no time source is available.
    virtual std::string time_string() { return ""; }
    // FC=2 parameter the core cannot answer from its own state (registry ids:
    // 6 hardware, 16 local ip, 30 reboot_required, 41 router ssid,
    // 48 network diagnostics, 49 wifi scan list, ...). Return true and fill
    // *out to answer with status 0; false -> status 1 (refused), the factory
    // fake's behavior for unknown parameters.
    virtual bool query_param(uint8_t parameter, std::string *out) {
      (void) parameter;
      (void) out;
      return false;
    }
    // FC=3 write (registry ids: 41 target ssid, 43 target password,
    // 29 apply/reboot, 34 serial baudrate). Return the response status:
    // 0 accepted, 1 refused. Default refuses everything, matching the factory fake.
    virtual uint8_t set_param(uint8_t parameter, const std::string &value) {
      (void) parameter;
      (void) value;
      return 1;
    }
    // AT+UART=<baud,data,stop,parity> write. The reply is always W000 (factory
    // mirror); platforms that can re-configure the inverter UART at runtime
    // apply the value here and then call update_uart_description().
    virtual void apply_uart(const std::string &value) { (void) value; }
    // Called whenever the reverse-TCP server endpoint actually changes (from a
    // discovery redirect or an HA-written endpoint). Platforms persist it here
    // so the bridge reconnects to the same HA across reboots without waiting for
    // a discovery broadcast. host/port reflect the new target.
    virtual void on_server_endpoint_changed(const std::string &host, uint16_t port) {
      (void) host;
      (void) port;
    }
    virtual void log(const char *message) { (void) message; }
  };

  CollectorCore(Actions *actions, CollectorProfile profile, CoreConfig config);

  // --- events from the platform ---
  void on_udp_datagram(const uint8_t *data, size_t len, uint32_t now_ms);
  void on_tcp_connected(uint32_t now_ms);
  void on_tcp_connect_failed(uint32_t now_ms);
  void on_tcp_closed(uint32_t now_ms);
  void on_tcp_data(const uint8_t *data, size_t len, uint32_t now_ms);
  void on_uart_data(const uint8_t *data, size_t len, uint32_t now_ms);
  // Drive timers: heartbeat, UART response gap/timeout, reconnect. Call often.
  void loop(uint32_t now_ms);

  // Optional static server endpoint (skip waiting for discovery).
  void set_server_endpoint(const std::string &host, uint16_t port, uint32_t now_ms);

  // Reflect a runtime UART reconfiguration in AT+UART, FC=2 param 34 and the
  // AT+VDTU capability string. uart format: "9600,8,1,NONE".
  void update_uart_description(const std::string &uart);

  bool connected() const { return link_state_ == LinkState::CONNECTED; }
  const std::string &server_host() const { return server_host_; }
  uint16_t server_port() const { return server_port_; }

 private:
  enum class LinkState : uint8_t { DISCONNECTED, CONNECTING, CONNECTED };
  enum class BridgeState : uint8_t { IDLE, WAITING_RESPONSE };

  struct ForwardRequest {
    uint16_t tid;
    uint16_t devcode;
    uint8_t devaddr;
    std::vector<uint8_t> payload;
  };

  void handle_at_line_(const std::string &line);
  void handle_frame_(const FrameHeader &header, const uint8_t *payload, size_t len);
  void send_heartbeat_(uint16_t tid);
  uint16_t next_unsolicited_tid_();
  void send_frame_(uint16_t tid, uint16_t devcode, uint8_t devaddr, uint8_t fcode,
                   const uint8_t *payload, size_t len);
  void maybe_start_forward_(uint32_t now_ms);
  void finish_forward_(bool send_response, uint32_t now_ms);
  void drop_link_state_(uint32_t now_ms, bool backoff);
  static bool time_reached_(uint32_t now_ms, uint32_t deadline_ms) {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
  }

  Actions *actions_;
  CollectorProfile profile_;
  CoreConfig config_;
  StreamSplitter splitter_;

  LinkState link_state_ = LinkState::DISCONNECTED;
  // Timestamp of the event currently being processed (for splitter callbacks).
  uint32_t event_now_ms_ = 0;
  bool have_endpoint_ = false;
  std::string server_host_;
  uint16_t server_port_ = 0;
  std::string cloud_endpoint_;  // "IP,PORT,TCP" for AT+CLDSRVHOST1
  uint32_t reconnect_at_ms_ = 0;
  uint32_t reconnect_backoff_ms_ = 0;
  uint32_t heartbeat_at_ms_ = 0;
  uint16_t unsolicited_tid_ = 0x8000;

  BridgeState bridge_state_ = BridgeState::IDLE;
  std::deque<ForwardRequest> forward_queue_;
  std::vector<uint8_t> uart_response_;
  uint32_t forward_sent_at_ms_ = 0;
  uint32_t uart_last_byte_at_ms_ = 0;
  uint32_t next_command_at_ms_ = 0;
};

}  // namespace eybond
