#pragma once
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace energy_management {
using sw = esphome::switch_::Switch;
using bs = esphome::binary_sensor::BinarySensor;

static const char *const TAG = "energy_management";

template <typename T>
bool advance(T &current_value, const T &&expected_value, const T &&new_value) {
  if (current_value == expected_value) {
    current_value = new_value;
    return true;
  }
  return false;
}

class EnergyManagement {
 public:
  enum class MODE {
    STOPPED,
    ENERGY_SAVING,
    LOAD_SHEDDING,
    BOTH,
  };

  using getter = std::function<bool()>;
  using setter = std::function<void(bool)>;
  // sets the desired value and returns the previous state
  using get_setter = std::function<bool(bool)>;
  using event = std::function<void()>;

  EnergyManagement(get_setter get_set_load_state,
                   setter set_es_restore_load_state,
                   getter get_es_restore_load_state,
                   setter set_ls_restore_load_state,
                   getter get_ls_restore_load_state,
                   event on_request_shedding_stop)
      : get_set_load_state(get_set_load_state),
        get_es_restore_load_state(get_es_restore_load_state),
        set_es_restore_load_state(set_es_restore_load_state),
        get_ls_restore_load_state(get_ls_restore_load_state),
        set_ls_restore_load_state(set_ls_restore_load_state),
        request_shedding_stop(on_request_shedding_stop) {}

  bool on_load_shed_on() {
    if (advance(mode, MODE::STOPPED, MODE::LOAD_SHEDDING) ||
        advance(mode, MODE::ENERGY_SAVING, MODE::BOTH)) {
      set_ls_restore_load_state(get_set_load_state(false));
      return true;
    }
    return false;
  }

  bool on_load_shed_off() {
    if (advance(mode, MODE::BOTH, MODE::ENERGY_SAVING) ||
        advance(mode, MODE::LOAD_SHEDDING, MODE::STOPPED)) {
      if (get_ls_restore_load_state()) {
        get_set_load_state(true);
      }
      return true;
    }
    return false;
  }

  [[nodiscard]] bool load_state_change(bool load_state) {
    if (mode == MODE::ENERGY_SAVING) {
      set_es_restore_load_state(false);
    }
    if (mode == MODE::BOTH || mode == MODE::LOAD_SHEDDING) {
      set_ls_restore_load_state(load_state);
    }
    if (device_can_turn_on()) {
      return load_state;
    } else {
      request_shedding_stop();
      return false;
    }
  }

  bool on_energy_saving_on() {
    if (advance(mode, MODE::STOPPED, MODE::ENERGY_SAVING)) {
      set_es_restore_load_state(get_set_load_state(false));
      return true;
    }
    if (advance(mode, MODE::LOAD_SHEDDING, MODE::BOTH)) {
      set_es_restore_load_state(get_ls_restore_load_state());
      return true;
    }
    return false;
  }

  bool on_energy_saving_off() {
    if (advance(mode, MODE::ENERGY_SAVING, MODE::STOPPED)) {
      if (get_es_restore_load_state()) {
        get_set_load_state(true);
      }
      return true;
    }
    if (advance(mode, MODE::BOTH, MODE::LOAD_SHEDDING)) {
      if (get_es_restore_load_state()) {
        set_ls_restore_load_state(true);
      }
      return true;
    }
    return false;
  }

  // energy saving is activated, load is turned on then
  // off, then load shedding is activated

  [[nodiscard]] bool is_load_shedding() const {
    return mode == MODE::BOTH || mode == MODE::LOAD_SHEDDING;
  }

  [[nodiscard]] bool is_energy_saving() const {
    return mode == MODE::BOTH || mode == MODE::ENERGY_SAVING;
  }

  [[nodiscard]] const MODE &get_mode() const { return mode; }

  [[nodiscard]] bool device_can_turn_on() const { return !is_load_shedding(); }

 private:
  MODE mode = MODE::STOPPED;
  const get_setter get_set_load_state;
  const setter set_ls_restore_load_state;
  const getter get_ls_restore_load_state;
  const setter set_es_restore_load_state;
  const getter get_es_restore_load_state;
  const event request_shedding_stop;
};

class EnergyManagementComponent : public Component {
 public:
  void setup() override {
    if (energy_management_ != nullptr) {
      ESP_LOGE(TAG, "Tried to setup initialized component");
      this->mark_failed();
    }
    turn_on_after_shedding_->publish_state(false);
    load_shed_->publish_state(false);
    energy_saving_->publish_state(false);
    requested_shedding_stop_->publish_initial_state(false);
    energy_saving_overwritten_->publish_initial_state(false);
    energy_management_ = new EnergyManagement(
        [this](bool new_state) {
          return this->device_state_lambda_->operator()(new_state);
        },
        // Negate this one since we're doing _overwritten_ instead of _should
        // restore_
        [this](bool new_state) {
          this->energy_saving_overwritten_->publish_state(!new_state);
        },
        [this]() { return !this->energy_saving_overwritten_->state; },
        // Keep this one as normal
        [this](bool new_state) {
          this->turn_on_after_shedding_->publish_state(new_state);
        },
        [this]() { return this->turn_on_after_shedding_->state; },
        [this]() { this->requested_shedding_stop_->publish_state(true); });
    if (this->initial_device_state_) {
      std::ignore = this->set_device_state(true);
    }
    load_shed_->add_on_state_callback([this](bool state) {
      state ? this->energy_management_->on_load_shed_on()
            : this->energy_management_->on_load_shed_off();
      if (!state) {
        this->requested_shedding_stop_->publish_state(false);
      }
    });
    energy_saving_->add_on_state_callback([this](bool state) {
      state ? this->energy_management_->on_energy_saving_on()
            : this->energy_management_->on_energy_saving_off();
      if (!state) {
        this->energy_saving_overwritten_->publish_state(false);
      }
    });
  }

  void dump_config() override{
      // ESP_LOGCONFIG(TAG, "EnergyManagement:");
  };
  void set_initial_device_state(bool state) {
    this->initial_device_state_ = state;
  }
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
  // Notifies the component of the desired state change and
  // returns the state the device should be at, knowing the new intentions
  [[nodiscard]] bool set_device_state(bool desired_state) {
    if (!this->is_ready()) {
      ESP_LOGW(TAG, "set_device_state was called before component was ready");
      this->set_initial_device_state(desired_state);
      return desired_state;
    }
    if (!this->energy_management_->is_load_shedding()) {
      desired_state ? this->turn_on_after_shedding_->turn_on()
                    : this->turn_on_after_shedding_->turn_off();
    }
    return this->energy_management_->load_state_change(desired_state);
  }

  [[nodiscard]] bool device_can_turn_on() {
    if (!this->is_ready()) {
      ESP_LOGW(TAG, "device_can_turn_on was called before component was ready");
      return true;
    }
    return this->energy_management_->device_can_turn_on();
  }

 private:
  sw *turn_on_after_shedding_;
  sw *load_shed_;
  sw *energy_saving_;
  bs *requested_shedding_stop_;
  bs *energy_saving_overwritten_;
  bool initial_device_state_ = false;
  optional<std::function<bool(bool)>> device_state_lambda_;
  EnergyManagement *energy_management_{nullptr};
};

class OptimisticSwitch : public switch_::Switch, public Component {
 public:
  void setup() override{};
  void dump_config() override { LOG_SWITCH("", "Optimistic Switch", this); };

 protected:
  void write_state(bool state) { this->publish_state(state); };
};

template <typename... Ts>
class EnergyManagementSetDeviceStateCondition : public Condition<Ts...> {
 public:
  EnergyManagementSetDeviceStateCondition(EnergyManagementComponent *parent)
      : parent_(parent) {}
  TEMPLATABLE_VALUE(bool, state)

  bool check(Ts... x) override {
    return this->parent_->set_device_state(this->state_.value(x...));
  }

 protected:
  EnergyManagementComponent *parent_;
};

template <typename... Ts>
class EnergyManagementDeviceCanTurnOnCondition : public Condition<Ts...> {
 public:
  EnergyManagementDeviceCanTurnOnCondition(EnergyManagementComponent *parent)
      : parent_(parent) {}

  bool check(Ts... x) override { return this->parent_->device_can_turn_on(); }

 protected:
  EnergyManagementComponent *parent_;
};

}  // namespace energy_management
}  // namespace esphome