#include "frame.h"

namespace eybond {

void encode_header(uint8_t out[HEADER_SIZE], uint16_t tid, uint16_t devcode, size_t total_len,
                   uint8_t devaddr, uint8_t fcode) {
  const uint16_t wire_len = static_cast<uint16_t>(total_len - WIRE_LEN_OFFSET);
  out[0] = static_cast<uint8_t>(tid >> 8);
  out[1] = static_cast<uint8_t>(tid & 0xFF);
  out[2] = static_cast<uint8_t>(devcode >> 8);
  out[3] = static_cast<uint8_t>(devcode & 0xFF);
  out[4] = static_cast<uint8_t>(wire_len >> 8);
  out[5] = static_cast<uint8_t>(wire_len & 0xFF);
  out[6] = devaddr;
  out[7] = fcode;
}

FrameHeader decode_header(const uint8_t *data) {
  FrameHeader header;
  header.tid = static_cast<uint16_t>((data[0] << 8) | data[1]);
  header.devcode = static_cast<uint16_t>((data[2] << 8) | data[3]);
  header.wire_len = static_cast<uint16_t>((data[4] << 8) | data[5]);
  header.devaddr = data[6];
  header.fcode = data[7];
  return header;
}

std::vector<uint8_t> build_frame(uint16_t tid, uint16_t devcode, uint8_t devaddr, uint8_t fcode,
                                 const uint8_t *payload, size_t payload_len) {
  std::vector<uint8_t> frame(HEADER_SIZE + payload_len);
  encode_header(frame.data(), tid, devcode, HEADER_SIZE + payload_len, devaddr, fcode);
  if (payload_len != 0 && payload != nullptr) {
    for (size_t i = 0; i < payload_len; i++) {
      frame[HEADER_SIZE + i] = payload[i];
    }
  }
  return frame;
}

}  // namespace eybond
