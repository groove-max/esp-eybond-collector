#include <string>
#include <vector>

#include "frame.h"
#include "stream_splitter.h"
#include "minitest.h"

using namespace eybond;

namespace {

struct Captured {
  std::vector<std::string> at_lines;
  std::vector<std::pair<FrameHeader, std::vector<uint8_t>>> frames;
  int errors = 0;
};

StreamSplitter make_splitter(Captured *captured) {
  StreamSplitter splitter;
  splitter.on_at_line = [captured](const std::string &line) { captured->at_lines.push_back(line); };
  splitter.on_frame = [captured](const FrameHeader &header, const uint8_t *payload, size_t len) {
    captured->frames.emplace_back(header, std::vector<uint8_t>(payload, payload + len));
  };
  splitter.on_protocol_error = [captured]() { captured->errors++; };
  return splitter;
}

}  // namespace

TEST(splitter_handles_interleaved_at_and_frames) {
  Captured captured;
  StreamSplitter splitter = make_splitter(&captured);

  const std::string at_line = "AT+WFSS?\r\n";
  const uint8_t payload[2] = {0xAB, 0xCD};
  const std::vector<uint8_t> frame = build_frame(3, 0x0994, 0x10, FC_FORWARD_TO_DEVICE, payload, 2);

  std::vector<uint8_t> stream(at_line.begin(), at_line.end());
  stream.insert(stream.end(), frame.begin(), frame.end());
  stream.insert(stream.end(), at_line.begin(), at_line.end());

  splitter.feed(stream.data(), stream.size());
  CHECK(captured.at_lines.size() == 2);
  CHECK_STR(captured.at_lines[0], at_line);
  CHECK(captured.frames.size() == 1);
  CHECK(captured.frames[0].first.tid == 3);
  CHECK_HEX(captured.frames[0].second, "abcd");
  CHECK(captured.errors == 0);
}

TEST(splitter_survives_byte_at_a_time_delivery) {
  Captured captured;
  StreamSplitter splitter = make_splitter(&captured);

  const std::string at_line = "AT+DTUPN?\r\n";
  const uint8_t payload[3] = {1, 2, 3};
  const std::vector<uint8_t> frame = build_frame(9, 0, 1, FC_HEARTBEAT, payload, 3);

  std::vector<uint8_t> stream(at_line.begin(), at_line.end());
  stream.insert(stream.end(), frame.begin(), frame.end());

  for (uint8_t byte : stream) {
    splitter.feed(&byte, 1);
  }
  CHECK(captured.at_lines.size() == 1);
  CHECK(captured.frames.size() == 1);
  CHECK(captured.frames[0].first.tid == 9);
  CHECK_HEX(captured.frames[0].second, "010203");
}

TEST(splitter_zero_payload_frame) {
  Captured captured;
  StreamSplitter splitter = make_splitter(&captured);
  const std::vector<uint8_t> frame = build_frame(1, 0, 1, FC_HEARTBEAT, nullptr, 0);
  splitter.feed(frame.data(), frame.size());
  CHECK(captured.frames.size() == 1);
  CHECK(captured.frames[0].second.empty());
}

TEST(splitter_detects_invalid_header) {
  Captured captured;
  StreamSplitter splitter = make_splitter(&captured);
  // wire_len 0 -> total_len 6 < HEADER_SIZE: not parseable, must signal desync
  const uint8_t bad[HEADER_SIZE] = {0, 1, 0, 0, 0, 0, 1, 1};
  splitter.feed(bad, sizeof(bad));
  CHECK(captured.errors == 1);
  CHECK(splitter.buffered() == 0);
}

TEST(splitter_detects_oversized_payload) {
  Captured captured;
  StreamSplitter splitter = make_splitter(&captured);
  uint8_t header[HEADER_SIZE];
  encode_header(header, 1, 0, HEADER_SIZE + StreamSplitter::MAX_PAYLOAD + 1, 1, FC_FORWARD_TO_DEVICE);
  splitter.feed(header, sizeof(header));
  CHECK(captured.errors == 1);
}

TEST(splitter_detects_runaway_at_line) {
  Captured captured;
  StreamSplitter splitter = make_splitter(&captured);
  std::string runaway = "AT+";
  runaway.append(StreamSplitter::MAX_AT_LINE + 10, 'X');  // no newline
  splitter.feed(reinterpret_cast<const uint8_t *>(runaway.data()), runaway.size());
  CHECK(captured.errors == 1);
}
