#include "endpoint_readback.h"
#include "minitest.h"

using namespace eybond;

TEST(endpoint_readback_staged_apply_and_completed) {
  std::string value = "applied";
  CHECK(!pending_endpoint_readback("", false, "", 0, &value));
  CHECK_STR(value, "applied");
  CHECK(!endpoint_apply_pending("", false));

  const std::string target = "192.0.2.10,18897,TCP";
  CHECK(pending_endpoint_readback(target, false, "", 0, &value));
  CHECK_STR(value, target);
  CHECK(endpoint_apply_pending(target, false));

  // The staged string is consumed by Apply; keep reporting the scheduled
  // endpoint until the component loop has actually switched the socket.
  CHECK(pending_endpoint_readback("", true, "192.0.2.10", 18897, &value));
  CHECK_STR(value, target);
  CHECK(endpoint_apply_pending("", true));
  CHECK(!pending_endpoint_readback("", false, "", 0, &value));
  CHECK(!endpoint_apply_pending("", false));
}

TEST(endpoint_readback_explicit_scheduled_update_takes_precedence) {
  std::string value;
  CHECK(pending_endpoint_readback("old.invalid,8899,TCP", true, "192.0.2.20", 18899, &value));
  CHECK_STR(value, "192.0.2.20,18899,TCP");
}
