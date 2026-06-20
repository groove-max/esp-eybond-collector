// Incremental parser for the reverse-TCP stream coming from the HA integration.
// The stream interleaves two kinds of messages:
//   - ASCII AT lines:   "AT+..." terminated by '\n'
//   - binary frames:    8-byte EyeBond header + payload
// Disambiguation matches the reference fake collector: peek 3 bytes, "AT+" means a line.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "frame.h"

namespace eybond {

class StreamSplitter {
 public:
  static constexpr size_t MAX_AT_LINE = 256;
  static constexpr size_t MAX_PAYLOAD = 512;

  // line includes the full "AT+...\n" text as received.
  std::function<void(const std::string &line)> on_at_line;
  std::function<void(const FrameHeader &header, const uint8_t *payload, size_t payload_len)> on_frame;
  // Desync (oversized line/payload or invalid header). Buffer is cleared; the
  // owner should drop the TCP connection because resync is not possible.
  std::function<void()> on_protocol_error;

  void feed(const uint8_t *data, size_t len);
  void reset() { buffer_.clear(); }
  size_t buffered() const { return buffer_.size(); }

 private:
  bool process_one_();
  void fail_();

  std::vector<uint8_t> buffer_;
};

}  // namespace eybond
