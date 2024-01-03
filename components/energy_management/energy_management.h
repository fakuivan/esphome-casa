#pragma once
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace energy_management {
using sw = esphome::switch_::Switch;
using bs = esphome::binary_sensor::BinarySensor;

static const char *const TAG = "energy_management";

class EnergyManagement {
 public:
  ~EnergyManagement() = delete;
  EnergyManagement(sw *turn_on_after_shedding, sw *load_shed, sw *energy_saving,
                   bs *requested_shedding_stop, bs *energy_saving_overwritten,
                   std::function<bool(bool)> &&set_device_state)
      : turn_on_after_shedding(turn_on_after_shedding),
        load_shed(load_shed),
        energy_saving(energy_saving),
        requested_shedding_stop(requested_shedding_stop),
        energy_saving_overwritten(energy_saving_overwritten),
        set_device_state(set_device_state) {
    load_shed->add_on_state_callback([this](bool state) {
      this->setting_up_load_shedding = true;
      this->set_device_state(!state);
    });

    energy_saving->add_on_state_callback([this](bool turned_on) {
      if (turned_on) {
        this->setting_up_energy_saving = true;
        if (this->set_device_state(false)) {
          this->energy_saving_overwritten->publish_state(true);
        }
      } else {
        if (!this->energy_saving_overwritten->state) {
          this->setting_up_energy_saving = true;
          this->set_device_state(true);
        }
        this->energy_saving_overwritten->publish_state(false);
      }
    });
  }

  void on_device_turn_off() {
    if (!setting_up_load_shedding) {
      turn_on_after_shedding->turn_off();
    } else {
      requested_shedding_stop->publish_state(false);
    }
    setting_up_load_shedding = false;
    energy_saving_overwritten->publish_state(energy_saving->state &&
                                             !setting_up_energy_saving);
    setting_up_energy_saving = false;
  }

  bool on_device_turn_on() {
    if (!setting_up_load_shedding && load_shed->state) {
      requested_shedding_stop->publish_state(true);
      turn_on_after_shedding->turn_on();
    }
    if (device_can_turn_on()) {
      requested_shedding_stop->publish_state(false);
      turn_on_after_shedding->turn_on();
      setting_up_load_shedding = false;
      energy_saving_overwritten->publish_state(energy_saving->state &&
                                               !setting_up_energy_saving);
      setting_up_energy_saving = false;
      return true;
    }
    setting_up_load_shedding = false;
    return false;
  }

  [[nodiscard]] bool device_can_turn_on() const {
    return setting_up_load_shedding ? turn_on_after_shedding->state
                                    : !load_shed->state;
  }

  sw *const turn_on_after_shedding;
  sw *const load_shed;
  sw *const energy_saving;
  bs *const requested_shedding_stop;
  bs *const energy_saving_overwritten;
  std::function<bool(bool)> set_device_state;
  static constexpr char const *class_name =
      "energy_management::EnergyManagement";

 private:
  bool setting_up_load_shedding = false;
  bool setting_up_energy_saving = false;
};

class EnergyManagementComponent : public Component {
 public:
  void setup() override {
    if (energy_management_ != nullptr) {
      ESP_LOGE(TAG, "Tried to setup initialized component");
      this->mark_failed();
    }
    auto set_device_state_lambda = this->device_state_lambda_.value();
    energy_management_ = new EnergyManagement(
        turn_on_after_shedding_, load_shed_, energy_saving_,
        requested_shedding_stop_, energy_saving_overwritten_,
        [this](bool new_state) {
          return this->device_state_lambda_->operator()(new_state);
        });
  }
  void dump_config() override{
      // ESP_LOGCONFIG(TAG, "EnergyManagement:");
  };
  void set_switches_and_sensors(sw *turn_on_after_shedding, sw *load_shed,
                                sw *energy_saving, bs *requested_shedding_stop,
                                bs *energy_saving_overwritten) {
    turn_on_after_shedding_ = turn_on_after_shedding;
    load_shed_ = load_shed;
    energy_saving_ = energy_saving;
    requested_shedding_stop_ = requested_shedding_stop;
    energy_saving_overwritten_ = energy_saving_overwritten;
  };
  void set_device_state_lambda(std::function<bool(bool)> &&f) {
    device_state_lambda_ = f;
  }

  float get_setup_priority() const override { return setup_priority::DATA; }
  void set_device_state(bool state) {
    if (state) {
      this->energy_management_->on_device_turn_on();
    } else {
      this->energy_management_->on_device_turn_off();
    }
  }

 private:
  sw *turn_on_after_shedding_;
  sw *load_shed_;
  sw *energy_saving_;
  bs *requested_shedding_stop_;
  bs *energy_saving_overwritten_;
  optional<std::function<bool(bool)>> device_state_lambda_;
  EnergyManagement *energy_management_{nullptr};
};

template <typename... Ts>
class EnergyManagementSetDeviceStateAction : public Action<Ts...> {
  EnergyManagementSetDeviceStateAction(EnergyManagementComponent *parent)
      : parent_(parent) {}
  TEMPLATABLE_VALUE(bool, state)

  void play(Ts... x) override {
    this->parent_->set_device_state(this->state_.value(x...));
  }

 protected:
  EnergyManagementComponent *parent_;
};

}  // namespace energy_management
}  // namespace esphome