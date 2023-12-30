#pragma once

#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

namespace esphome {
namespace switches_select {

class SwitchesSelect : public select::Select, public Component {
 public:
  void set_switches(std::vector<switch_::Switch *> switches) {
    this->switches_ = switches;
  }
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  void control(const std::string &value) override;

  std::vector<switch_::Switch *> switches_;
  std::vector<bool> modifying_;
};

}  // namespace switches_select
}  // namespace esphome
