#pragma once

// Generated esphome.h includes every header in this component's directory, so
// this file is pulled into builds that do not use the select platform too.
// Guard on USE_SELECT so it is a no-op (and never needs select/select.h) unless
// a select: platform entry actually loaded the select component.
#if defined(USE_ARDUINO) && defined(USE_SELECT)

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

#include "eybond_collector.h"

namespace esphome {
namespace eybond_collector {

// Select entity for the inverter UART baud rate (e.g. PI30 2400 <-> SMG 9600)
// without reflashing. Stays in sync when the rate is changed through the
// collector protocol instead (AT+UART= or FC=3 param 34).
class EybondBaudSelect : public select::Select, public Component, public Parented<EybondCollector> {
 public:
  void set_restore_value(bool restore) { restore_value_ = restore; }

  void setup() override {
    this->parent_->set_baud_listener([this](uint32_t baud) { this->publish_baud_(baud); });
    uint32_t baud = this->parent_->current_baud_rate();
    if (restore_value_) {
      pref_ = global_preferences->make_preference<uint32_t>(this->get_object_id_hash());
      uint32_t saved = 0;
      if (pref_.load(&saved) && saved != 0 && this->parent_->apply_baud_rate(saved)) {
        baud = saved;
      }
    }
    this->publish_baud_(baud);
  }

 protected:
  void control(const std::string &value) override {
    const auto baud = parse_number<uint32_t>(value);
    if (!baud.has_value() || !this->parent_->apply_baud_rate(*baud)) {
      return;  // listener never fires; state stays on the previous option
    }
    if (restore_value_) {
      pref_.save(&*baud);
    }
  }

  void publish_baud_(uint32_t baud) { this->publish_state(to_string(baud)); }

  bool restore_value_{false};
  ESPPreferenceObject pref_;
};

}  // namespace eybond_collector
}  // namespace esphome

#endif  // USE_ARDUINO && USE_SELECT
