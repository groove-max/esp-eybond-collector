// Linux host simulator: runs the real CollectorCore with POSIX sockets and a PTY
// in place of the inverter UART. Lets the actual HA integration (or e2e_sim.py)
// talk to the exact protocol logic that ships in the firmware.
//
// Prints to stdout on startup:
//   PTY <path>   - attach a fake inverter here (e.g. e2e_sim.py)
//   UDP <port>   - discovery listener port
//   PN <pn>      - collector identity
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pty.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "core.h"

namespace {

uint32_t millis_now() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint32_t>(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

class SimPlatform : public eybond::CollectorCore::Actions {
 public:
  eybond::CollectorCore *core = nullptr;
  int udp_fd = -1;
  int tcp_fd = -1;
  int pty_master = -1;
  sockaddr_in last_udp_peer{};

  void udp_reply(const uint8_t *data, size_t len) override {
    sendto(udp_fd, data, len, 0, reinterpret_cast<sockaddr *>(&last_udp_peer), sizeof(last_udp_peer));
  }

  void tcp_connect(const std::string &host, uint16_t port) override {
    close_tcp();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *result = nullptr;
    const std::string port_text = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0 || result == nullptr) {
      std::fprintf(stderr, "resolve %s failed\n", host.c_str());
      core->on_tcp_connect_failed(millis_now());
      return;
    }
    const int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0 || connect(fd, result->ai_addr, result->ai_addrlen) != 0) {
      std::fprintf(stderr, "connect %s:%u failed: %s\n", host.c_str(), port, strerror(errno));
      if (fd >= 0) {
        close(fd);
      }
      freeaddrinfo(result);
      core->on_tcp_connect_failed(millis_now());
      return;
    }
    freeaddrinfo(result);
    const int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    tcp_fd = fd;
    std::fprintf(stderr, "connected to %s:%u\n", host.c_str(), port);
    core->on_tcp_connected(millis_now());
  }

  void tcp_send(const uint8_t *data, size_t len) override {
    if (tcp_fd >= 0) {
      const ssize_t written = send(tcp_fd, data, len, MSG_NOSIGNAL);
      (void) written;
    }
  }

  void tcp_close() override { close_tcp(); }

  void uart_send(const uint8_t *data, size_t len) override {
    if (pty_master >= 0) {
      const ssize_t written = write(pty_master, data, len);
      (void) written;
    }
  }

  void log(const char *message) override { std::fprintf(stderr, "core: %s\n", message); }

  void close_tcp() {
    if (tcp_fd >= 0) {
      close(tcp_fd);
      tcp_fd = -1;
    }
  }
};

uint32_t arg_value(int argc, char **argv, const char *name, uint32_t fallback) {
  for (int i = 1; i + 1 < argc; i++) {
    if (std::strcmp(argv[i], name) == 0) {
      return static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 10));
    }
  }
  return fallback;
}

const char *arg_text(int argc, char **argv, const char *name, const char *fallback) {
  for (int i = 1; i + 1 < argc; i++) {
    if (std::strcmp(argv[i], name) == 0) {
      return argv[i + 1];
    }
  }
  return fallback;
}

}  // namespace

int main(int argc, char **argv) {
  const uint16_t udp_port = static_cast<uint16_t>(arg_value(argc, argv, "--udp-port", 58899));

  eybond::CoreConfig config;
  config.heartbeat_interval_ms = arg_value(argc, argv, "--heartbeat-interval", config.heartbeat_interval_ms);
  config.command_spacing_ms = arg_value(argc, argv, "--command-spacing", config.command_spacing_ms);
  config.uart_timeout_ms = arg_value(argc, argv, "--uart-timeout", config.uart_timeout_ms);
  config.uart_gap_ms = arg_value(argc, argv, "--uart-gap", config.uart_gap_ms);

  eybond::CollectorProfile profile;
  profile.pn = arg_text(argc, argv, "--pn", "V00000200000000001");

  SimPlatform platform;
  eybond::CollectorCore core(&platform, profile, config);
  platform.core = &core;

  platform.udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
  const int one = 1;
  setsockopt(platform.udp_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in bind_addr{};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind_addr.sin_port = htons(udp_port);
  if (bind(platform.udp_fd, reinterpret_cast<sockaddr *>(&bind_addr), sizeof(bind_addr)) != 0) {
    std::fprintf(stderr, "udp bind %u failed: %s\n", udp_port, strerror(errno));
    return 1;
  }

  int pty_slave = -1;
  char pty_name[128];
  if (openpty(&platform.pty_master, &pty_slave, pty_name, nullptr, nullptr) != 0) {
    std::fprintf(stderr, "openpty failed: %s\n", strerror(errno));
    return 1;
  }

  std::printf("PTY %s\nUDP %u\nPN %s\n", pty_name, udp_port, profile.pn.c_str());
  std::fflush(stdout);

  while (true) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(platform.udp_fd, &read_fds);
    FD_SET(platform.pty_master, &read_fds);
    int max_fd = platform.udp_fd > platform.pty_master ? platform.udp_fd : platform.pty_master;
    if (platform.tcp_fd >= 0) {
      FD_SET(platform.tcp_fd, &read_fds);
      max_fd = platform.tcp_fd > max_fd ? platform.tcp_fd : max_fd;
    }
    timeval timeout{0, 10 * 1000};
    select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);

    uint8_t buffer[1024];
    if (FD_ISSET(platform.udp_fd, &read_fds)) {
      socklen_t peer_len = sizeof(platform.last_udp_peer);
      const ssize_t received =
          recvfrom(platform.udp_fd, buffer, sizeof(buffer), 0,
                   reinterpret_cast<sockaddr *>(&platform.last_udp_peer), &peer_len);
      if (received > 0) {
        core.on_udp_datagram(buffer, static_cast<size_t>(received), millis_now());
      }
    }

    if (platform.tcp_fd >= 0 && FD_ISSET(platform.tcp_fd, &read_fds)) {
      const ssize_t received = recv(platform.tcp_fd, buffer, sizeof(buffer), 0);
      if (received > 0) {
        core.on_tcp_data(buffer, static_cast<size_t>(received), millis_now());
      } else if (received == 0) {
        platform.close_tcp();
        std::fprintf(stderr, "server closed connection\n");
        core.on_tcp_closed(millis_now());
      }
    }

    if (FD_ISSET(platform.pty_master, &read_fds)) {
      const ssize_t received = read(platform.pty_master, buffer, sizeof(buffer));
      if (received > 0) {
        core.on_uart_data(buffer, static_cast<size_t>(received), millis_now());
      }
    }

    core.loop(millis_now());
  }
}
