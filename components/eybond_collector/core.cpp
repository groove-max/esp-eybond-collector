#include "core.h"

#include <cctype>
#include <cstdlib>

namespace eybond {

namespace {
constexpr size_t HEARTBEAT_PN_LEN = 14;
constexpr size_t MAX_UART_RESPONSE = 512;
}  // namespace

bool parse_uart_settings(const std::string &value, UartSettings *out) {
  // Split on commas into up to 4 fields: baud, data, stop, parity.
  std::string fields[4];
  size_t count = 0;
  size_t start = 0;
  for (size_t i = 0; i <= value.size() && count < 4; i++) {
    if (i == value.size() || value[i] == ',') {
      fields[count++] = value.substr(start, i - start);
      start = i + 1;
    }
  }
  if (count == 0 || fields[0].empty()) {
    return false;
  }

  uint32_t baud = 0;
  for (char ch : fields[0]) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    baud = baud * 10 + static_cast<uint32_t>(ch - '0');
  }
  if (baud == 0) {
    return false;
  }

  UartSettings settings;
  settings.baud = baud;
  if (count >= 2 && !fields[1].empty()) {
    settings.data_bits = static_cast<uint8_t>(std::atoi(fields[1].c_str()));
  }
  if (count >= 3 && !fields[2].empty()) {
    settings.stop_bits = static_cast<uint8_t>(std::atoi(fields[2].c_str()));
  }
  if (count >= 4 && !fields[3].empty()) {
    std::string parity = fields[3];
    for (char &ch : parity) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    if (parity == "EVEN" || parity == "E") {
      settings.parity = UartParity::EVEN;
    } else if (parity == "ODD" || parity == "O") {
      settings.parity = UartParity::ODD;
    } else {
      settings.parity = UartParity::NONE;
    }
  }
  *out = settings;
  return true;
}

std::string build_vdtu_capabilities(const CollectorProfile &profile, const CoreConfig &config) {
  return std::string("esp-collector,") + BRIDGE_VERSION +
         ";features=local_only,no_cloud,wifi_params,endpoint_write,reboot" +
         ";uart=" + profile.uart +
         ";spacing_ms=" + std::to_string(config.command_spacing_ms) +
         ";queue=" + std::to_string(config.forward_queue_limit);
}

bool parse_server_endpoint(const std::string &value, std::string *host, uint16_t *port) {
  // Split off a trailing ",TCP"/",UDP" transport token if present.
  std::string body = value;
  const size_t transport = body.find_last_of(',');
  if (transport != std::string::npos) {
    const std::string tail = body.substr(transport + 1);
    bool tail_is_alpha = !tail.empty();
    for (char ch : tail) {
      if (!std::isalpha(static_cast<unsigned char>(ch))) {
        tail_is_alpha = false;
        break;
      }
    }
    if (tail_is_alpha) {
      body = body.substr(0, transport);
    }
  }

  // Host/port separator is ',' (factory form) or ':'.
  size_t sep = body.find_last_of(',');
  if (sep == std::string::npos) {
    sep = body.find_last_of(':');
  }
  if (sep == std::string::npos || sep == 0 || sep + 1 >= body.size()) {
    return false;
  }

  const std::string host_text = body.substr(0, sep);
  uint32_t parsed_port = 0;
  for (size_t i = sep + 1; i < body.size(); i++) {
    const char ch = body[i];
    if (ch < '0' || ch > '9') {
      return false;
    }
    parsed_port = parsed_port * 10 + static_cast<uint32_t>(ch - '0');
    if (parsed_port > 65535) {
      return false;
    }
  }
  if (parsed_port == 0 || host_text.empty()) {
    return false;
  }
  *host = host_text;
  *port = static_cast<uint16_t>(parsed_port);
  return true;
}

CollectorCore::CollectorCore(Actions *actions, CollectorProfile profile, CoreConfig config)
    : actions_(actions), profile_(std::move(profile)), config_(config) {
  splitter_.on_at_line = [this](const std::string &line) { this->handle_at_line_(line); };
  splitter_.on_frame = [this](const FrameHeader &header, const uint8_t *payload, size_t len) {
    this->handle_frame_(header, payload, len);
  };
  splitter_.on_protocol_error = [this]() {
    actions_->log("tcp stream desync, dropping connection");
    // Mark disconnected first so the platform's on_tcp_closed callback is a no-op.
    link_state_ = LinkState::DISCONNECTED;
    actions_->tcp_close();
    this->drop_link_state_(event_now_ms_, true);
  };
}

void CollectorCore::on_udp_datagram(const uint8_t *data, size_t len, uint32_t now_ms) {
  DiscoveryRedirect redirect;
  if (!parse_discovery_redirect(data, len, &redirect)) {
    return;
  }
  actions_->udp_reply(reinterpret_cast<const uint8_t *>(DISCOVERY_UDP_REPLY),
                      std::char_traits<char>::length(DISCOVERY_UDP_REPLY));
  set_server_endpoint(redirect.server_ip, redirect.server_port, now_ms);
}

void CollectorCore::update_uart_description(const std::string &uart) {
  profile_.uart = uart;
  if (!profile_.vdtu.empty()) {
    profile_.vdtu = build_vdtu_capabilities(profile_, config_);
  }
}

void CollectorCore::set_server_endpoint(const std::string &host, uint16_t port, uint32_t now_ms) {
  const bool same = have_endpoint_ && host == server_host_ && port == server_port_;
  if (same) {
    if (link_state_ == LinkState::DISCONNECTED) {
      reconnect_at_ms_ = now_ms;
    }
    return;
  }

  if (link_state_ != LinkState::DISCONNECTED) {
    // Mark disconnected before closing so a re-entrant on_tcp_closed is a no-op.
    link_state_ = LinkState::DISCONNECTED;
    actions_->tcp_close();
    drop_link_state_(now_ms, false);
  }
  have_endpoint_ = true;
  server_host_ = host;
  server_port_ = port;
  cloud_endpoint_ = host + "," + std::to_string(port) + ",TCP";
  reconnect_backoff_ms_ = 0;
  reconnect_at_ms_ = now_ms;
  // Endpoint genuinely changed (the early return above covers the no-op case);
  // let the platform persist it so the bridge reconnects here after a reboot.
  actions_->on_server_endpoint_changed(server_host_, server_port_);
}

void CollectorCore::on_tcp_connected(uint32_t now_ms) {
  event_now_ms_ = now_ms;
  link_state_ = LinkState::CONNECTED;
  reconnect_backoff_ms_ = 0;
  splitter_.reset();
  send_heartbeat_(next_unsolicited_tid_());
  heartbeat_at_ms_ = now_ms + config_.heartbeat_interval_ms;
}

void CollectorCore::on_tcp_connect_failed(uint32_t now_ms) {
  event_now_ms_ = now_ms;
  if (link_state_ == LinkState::CONNECTING) {
    link_state_ = LinkState::DISCONNECTED;
  }
  drop_link_state_(now_ms, true);
}

void CollectorCore::on_tcp_closed(uint32_t now_ms) {
  event_now_ms_ = now_ms;
  if (link_state_ == LinkState::DISCONNECTED) {
    return;
  }
  link_state_ = LinkState::DISCONNECTED;
  drop_link_state_(now_ms, true);
}

void CollectorCore::on_tcp_data(const uint8_t *data, size_t len, uint32_t now_ms) {
  event_now_ms_ = now_ms;
  splitter_.feed(data, len);
}

void CollectorCore::on_uart_data(const uint8_t *data, size_t len, uint32_t now_ms) {
  if (bridge_state_ != BridgeState::WAITING_RESPONSE) {
    return;  // unsolicited noise between commands
  }
  uart_response_.insert(uart_response_.end(), data, data + len);
  uart_last_byte_at_ms_ = now_ms;
  if (uart_response_.size() > MAX_UART_RESPONSE) {
    finish_forward_(true, now_ms);
  }
}

void CollectorCore::loop(uint32_t now_ms) {
  event_now_ms_ = now_ms;

  if (link_state_ == LinkState::DISCONNECTED && have_endpoint_ && time_reached_(now_ms, reconnect_at_ms_)) {
    link_state_ = LinkState::CONNECTING;
    actions_->tcp_connect(server_host_, server_port_);
  }

  if (link_state_ == LinkState::CONNECTED && time_reached_(now_ms, heartbeat_at_ms_)) {
    send_heartbeat_(next_unsolicited_tid_());
    heartbeat_at_ms_ = now_ms + config_.heartbeat_interval_ms;
  }

  if (bridge_state_ == BridgeState::WAITING_RESPONSE) {
    if (!uart_response_.empty() && time_reached_(now_ms, uart_last_byte_at_ms_ + config_.uart_gap_ms)) {
      finish_forward_(true, now_ms);
    } else if (time_reached_(now_ms, forward_sent_at_ms_ + config_.uart_timeout_ms)) {
      // Empty response -> honest timeout: no TCP reply at all.
      finish_forward_(!uart_response_.empty(), now_ms);
    }
  }

  maybe_start_forward_(now_ms);
}

void CollectorCore::handle_at_line_(const std::string &line) {
  AtCommand command;
  if (!parse_at_line(line, &command)) {
    return;
  }
  if (command.is_write && command.command == "CLDSRVHOST1") {
    // Retarget the reverse-TCP link to the written endpoint so AT+CLDSRVHOST1?
    // and FC=2 param 21 keep reporting the address we actually use. Malformed
    // values are ignored (the ack stays W000, factory-mirror).
    std::string host;
    uint16_t port = 0;
    if (parse_server_endpoint(command.value, &host, &port)) {
      set_server_endpoint(host, port, event_now_ms_);
    }
  }
  if (command.is_write && command.command == "UART") {
    // Applied for real by platforms that support runtime re-configuration;
    // they call update_uart_description() from inside the hook.
    actions_->apply_uart(command.value);
  }
  AtRuntimeValues runtime;
  runtime.cloud_endpoint = cloud_endpoint_;
  runtime.wifi_rssi = actions_->wifi_rssi();
  runtime.time_string = actions_->time_string();
  const std::string reply = build_at_reply(command, profile_, runtime);
  actions_->tcp_send(reinterpret_cast<const uint8_t *>(reply.data()), reply.size());
}

void CollectorCore::handle_frame_(const FrameHeader &header, const uint8_t *payload, size_t len) {
  switch (header.fcode) {
    case FC_HEARTBEAT:
      send_heartbeat_(header.tid);
      return;

    case FC_QUERY_COLLECTOR: {
      const uint8_t parameter = len != 0 ? payload[0] : 0;
      std::string text;
      bool ok = true;
      // Parameters the core can answer from its own state (registry ids).
      if (parameter == 2) {  // collector_pn
        text = profile_.pn;
      } else if (parameter == 5) {  // firmware_version
        text = profile_.firmware_version;
      } else if (parameter == 21) {  // domain_address_1: current server endpoint
        text = cloud_endpoint_;
      } else if (parameter == 34) {  // serial_baudrate: leading field of "2400,8,1,NONE"
        text = profile_.uart.substr(0, profile_.uart.find(','));
      } else {
        ok = actions_->query_param(parameter, &text);
      }

      std::vector<uint8_t> response;
      response.push_back(ok ? 0 : 1);
      response.push_back(parameter);
      if (ok) {
        response.insert(response.end(), text.begin(), text.end());
      }
      send_frame_(header.tid, header.devcode, header.devaddr, FC_QUERY_COLLECTOR, response.data(),
                  response.size());
      return;
    }

    case FC_SET_COLLECTOR: {
      const uint8_t parameter = len != 0 ? payload[0] : 0;
      // FC=3 payload = parameter byte + ascii value (smartess_local.py).
      std::string value;
      if (len > 1) {
        value.assign(reinterpret_cast<const char *>(payload) + 1, len - 1);
      }
      const uint8_t status = actions_->set_param(parameter, value);
      const uint8_t response[2] = {status, parameter};
      send_frame_(header.tid, header.devcode, header.devaddr, FC_SET_COLLECTOR, response, sizeof(response));
      return;
    }

    case FC_FORWARD_TO_DEVICE: {
      if (forward_queue_.size() >= config_.forward_queue_limit) {
        actions_->log("forward queue full, dropping request");
        return;
      }
      ForwardRequest request;
      request.tid = header.tid;
      request.devcode = header.devcode;
      request.devaddr = header.devaddr;
      request.payload.assign(payload, payload + len);
      forward_queue_.push_back(std::move(request));
      maybe_start_forward_(event_now_ms_);
      return;
    }

    default:
      return;  // unsupported function codes are ignored, like the reference collector
  }
}

void CollectorCore::maybe_start_forward_(uint32_t now_ms) {
  if (bridge_state_ != BridgeState::IDLE || forward_queue_.empty()) {
    return;
  }
  if (!time_reached_(now_ms, next_command_at_ms_)) {
    return;
  }
  const ForwardRequest &request = forward_queue_.front();
  uart_response_.clear();
  bridge_state_ = BridgeState::WAITING_RESPONSE;
  forward_sent_at_ms_ = now_ms;
  uart_last_byte_at_ms_ = now_ms;
  actions_->uart_send(request.payload.data(), request.payload.size());
}

void CollectorCore::finish_forward_(bool send_response, uint32_t now_ms) {
  if (forward_queue_.empty()) {
    bridge_state_ = BridgeState::IDLE;
    return;
  }
  const ForwardRequest request = std::move(forward_queue_.front());
  forward_queue_.pop_front();
  bridge_state_ = BridgeState::IDLE;
  next_command_at_ms_ = now_ms + config_.command_spacing_ms;

  if (send_response && link_state_ == LinkState::CONNECTED) {
    send_frame_(request.tid, request.devcode, request.devaddr, FC_FORWARD_TO_DEVICE,
                uart_response_.data(), uart_response_.size());
  } else if (!send_response) {
    actions_->log("inverter timeout, request dropped");
  }
  uart_response_.clear();
}

void CollectorCore::send_heartbeat_(uint16_t tid) {
  uint8_t payload[HEARTBEAT_PN_LEN] = {0};
  const size_t pn_len = profile_.pn.size() < HEARTBEAT_PN_LEN ? profile_.pn.size() : HEARTBEAT_PN_LEN;
  for (size_t i = 0; i < pn_len; i++) {
    payload[i] = static_cast<uint8_t>(profile_.pn[i]);
  }
  send_frame_(tid, config_.heartbeat_devcode, config_.collector_addr, FC_HEARTBEAT, payload,
              HEARTBEAT_PN_LEN);
}

uint16_t CollectorCore::next_unsolicited_tid_() {
  unsolicited_tid_ = static_cast<uint16_t>((unsolicited_tid_ + 1) & 0xFFFF);
  if (unsolicited_tid_ < 0x8000) {
    unsolicited_tid_ = 0x8000;
  }
  return unsolicited_tid_;
}

void CollectorCore::send_frame_(uint16_t tid, uint16_t devcode, uint8_t devaddr, uint8_t fcode,
                                const uint8_t *payload, size_t len) {
  if (link_state_ != LinkState::CONNECTED) {
    return;
  }
  const std::vector<uint8_t> frame = build_frame(tid, devcode, devaddr, fcode, payload, len);
  actions_->tcp_send(frame.data(), frame.size());
}

void CollectorCore::drop_link_state_(uint32_t now_ms, bool backoff) {
  splitter_.reset();
  forward_queue_.clear();
  uart_response_.clear();
  bridge_state_ = BridgeState::IDLE;
  if (backoff) {
    if (reconnect_backoff_ms_ == 0) {
      reconnect_backoff_ms_ = config_.reconnect_min_ms;
    }
    reconnect_at_ms_ = now_ms + reconnect_backoff_ms_;
    reconnect_backoff_ms_ = reconnect_backoff_ms_ * 2 < config_.reconnect_max_ms
                                ? reconnect_backoff_ms_ * 2
                                : config_.reconnect_max_ms;
  }
}

}  // namespace eybond
