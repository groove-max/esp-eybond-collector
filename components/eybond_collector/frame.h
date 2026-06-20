// EyeBond collector frame header codec.
// Wire format (big-endian, 8 bytes): tid(u16) devcode(u16) wire_len(u16) devaddr(u8) fcode(u8)
// wire_len = total_len - 6; total_len = 8 + payload_len.
// Mirrors ha-eybond-local custom_components/eybond_local/collector/protocol.py.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eybond {

constexpr uint8_t FC_HEARTBEAT = 1;
constexpr uint8_t FC_QUERY_COLLECTOR = 2;
constexpr uint8_t FC_SET_COLLECTOR = 3;
constexpr uint8_t FC_FORWARD_TO_DEVICE = 4;

constexpr size_t HEADER_SIZE = 8;
constexpr size_t WIRE_LEN_OFFSET = 6;

struct FrameHeader {
  uint16_t tid = 0;
  uint16_t devcode = 0;
  uint16_t wire_len = 0;
  uint8_t devaddr = 0;
  uint8_t fcode = 0;

  size_t total_len() const { return static_cast<size_t>(wire_len) + WIRE_LEN_OFFSET; }
  // Only valid when total_len() >= HEADER_SIZE; callers must validate first.
  size_t payload_len() const { return total_len() - HEADER_SIZE; }
  bool valid() const { return total_len() >= HEADER_SIZE; }
};

void encode_header(uint8_t out[HEADER_SIZE], uint16_t tid, uint16_t devcode, size_t total_len,
                   uint8_t devaddr, uint8_t fcode);

FrameHeader decode_header(const uint8_t *data);

std::vector<uint8_t> build_frame(uint16_t tid, uint16_t devcode, uint8_t devaddr, uint8_t fcode,
                                 const uint8_t *payload, size_t payload_len);

}  // namespace eybond
