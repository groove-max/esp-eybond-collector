#include "stream_splitter.h"

namespace eybond {

void StreamSplitter::feed(const uint8_t *data, size_t len) {
  buffer_.insert(buffer_.end(), data, data + len);
  while (process_one_()) {
  }
}

void StreamSplitter::fail_() {
  buffer_.clear();
  if (on_protocol_error) {
    on_protocol_error();
  }
}

bool StreamSplitter::process_one_() {
  if (buffer_.size() < 3) {
    return false;
  }

  if (buffer_[0] == 'A' && buffer_[1] == 'T' && buffer_[2] == '+') {
    for (size_t i = 3; i < buffer_.size(); i++) {
      if (buffer_[i] == '\n') {
        std::string line(reinterpret_cast<const char *>(buffer_.data()), i + 1);
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(i + 1));
        if (on_at_line) {
          on_at_line(line);
        }
        return true;
      }
    }
    if (buffer_.size() > MAX_AT_LINE) {
      fail_();
    }
    return false;
  }

  if (buffer_.size() < HEADER_SIZE) {
    return false;
  }
  const FrameHeader header = decode_header(buffer_.data());
  if (!header.valid() || header.payload_len() > MAX_PAYLOAD) {
    fail_();
    return false;
  }
  const size_t total = header.total_len();
  if (buffer_.size() < total) {
    return false;
  }
  std::vector<uint8_t> frame(buffer_.begin(), buffer_.begin() + static_cast<long>(total));
  buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(total));
  if (on_frame) {
    on_frame(header, frame.data() + HEADER_SIZE, total - HEADER_SIZE);
  }
  return true;
}

}  // namespace eybond
