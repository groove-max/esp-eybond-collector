// Vectors generated with the integration's own protocol.py / fake_collector_lib.py.
#include "frame.h"
#include "minitest.h"

using namespace eybond;

TEST(encode_header_matches_python) {
  // encode_header(0x1234, 0x0994, 10, 0x10, 4) -> 1234099400041004
  uint8_t out[HEADER_SIZE];
  encode_header(out, 0x1234, 0x0994, 10, 0x10, 4);
  CHECK_HEX(std::vector<uint8_t>(out, out + HEADER_SIZE), "1234099400041004");
}

TEST(decode_header_roundtrip) {
  uint8_t out[HEADER_SIZE];
  encode_header(out, 0xFFFF, 0x0001, HEADER_SIZE + 5, 0x22, FC_FORWARD_TO_DEVICE);
  const FrameHeader header = decode_header(out);
  CHECK(header.tid == 0xFFFF);
  CHECK(header.devcode == 0x0001);
  CHECK(header.total_len() == HEADER_SIZE + 5);
  CHECK(header.payload_len() == 5);
  CHECK(header.devaddr == 0x22);
  CHECK(header.fcode == FC_FORWARD_TO_DEVICE);
  CHECK(header.valid());
}

TEST(build_frame_heartbeat_matches_python) {
  // build_unsolicited_heartbeat(tid=0x8001, pn="V00000200000000001", devcode=0, collector_addr=1)
  const char *pn14 = "V0000020000000";  // 18-char PN truncated to 14
  const std::vector<uint8_t> frame =
      build_frame(0x8001, 0x0000, 0x01, FC_HEARTBEAT, reinterpret_cast<const uint8_t *>(pn14), 14);
  CHECK_HEX(frame, "80010000001001015630303030303230303030303030");
}

TEST(build_frame_fc2_matches_python) {
  // build_collector_request(7, b"\x00\x05" + b"1.0.0", devcode=0x0994, collector_addr=0x10, fcode=2)
  std::vector<uint8_t> payload = {0x00, 0x05};
  const char *fw = "1.0.0";
  payload.insert(payload.end(), fw, fw + 5);
  const std::vector<uint8_t> frame =
      build_frame(7, 0x0994, 0x10, FC_QUERY_COLLECTOR, payload.data(), payload.size());
  CHECK_HEX(frame, "00070994000910020005312e302e30");
}

TEST(invalid_header_detected) {
  // wire_len 0 -> total_len 6 < HEADER_SIZE
  const uint8_t raw[HEADER_SIZE] = {0, 1, 0, 0, 0, 0, 1, 1};
  CHECK(!decode_header(raw).valid());
}
