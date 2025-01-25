#pragma once
#include <functional>

namespace action_queue {

template <typename T>
bool advance(T &current_value, const T &&expected_value, const T &&new_value) {
  if (current_value == expected_value) {
    current_value = new_value;
    return true;
  }
  return false;
}

class SingleDepthBinaryActionQueue {
 public:
  enum class STATE {
    DONE,
    SINGLE_TOGGLE,
    DOUBLE_TOGGLE,
  };
  using on_action_sent_t = std::function<void()>;
  using send_action_t = std::function<void(bool, on_action_sent_t &&)>;
  using cancel_action_t = std::function<void()>;
  using send_state_t = std::function<void(bool)>;
  SingleDepthBinaryActionQueue(send_action_t &&send_action,
                               cancel_action_t &&cancel_action,
                               send_state_t &&send_state,
                               bool initial_state = false)
      : send_action_(send_action),
        cancel_action_(cancel_action),
        send_state_(send_state),
        binary_state(initial_state) {}

  // default constructor so that it works with esphome::optional
  SingleDepthBinaryActionQueue()
      : send_action_([](bool, on_action_sent_t callback) { callback(); }),
        cancel_action_([]() {}),
        send_state_([](bool) {}),
        binary_state(false) {}

  ~SingleDepthBinaryActionQueue() {
    if (!action_sent && state != STATE::DONE) {
      cancel_action();
    }
  }

  void on_state_update(const bool turn_on) {
    if (expected_action() != turn_on) {
      return;
    }
    if (state == STATE::DONE) {
      update_binary_state(turn_on);
      return;
    }
    if (advance(state, STATE::SINGLE_TOGGLE, STATE::DONE)) {
      action_received();
      return;
    }
    if (advance(state, STATE::DOUBLE_TOGGLE, STATE::SINGLE_TOGGLE)) {
      action_received();
      send_action(expected_action());
      return;
    }
  }

  void toggle() {
    binary_state = !binary_state;
    if (advance(state, STATE::DONE, STATE::SINGLE_TOGGLE)) {
      send_action(expected_action());
      return;
    }
    if (!action_sent && advance(state, STATE::SINGLE_TOGGLE, STATE::DONE)) {
      cancel_action();
      return;
    }
    advance(state, STATE::SINGLE_TOGGLE, STATE::DOUBLE_TOGGLE) ||
        advance(state, STATE::DOUBLE_TOGGLE, STATE::SINGLE_TOGGLE);
  }

 private:
  void update_binary_state(bool turn_on) {
    binary_state = turn_on;
    send_state_(turn_on);
  }
  bool expected_action() const {
    // Double toggle and done:
    // off -> on -> off
    // ^      ^     ^ we're here
    // |      next state update
    // current state
    //
    // Single toggle:
    //        on -> off
    //        ^     ^ we're here, expecting the state to reach where we are
    //        current state
    if (state == STATE::SINGLE_TOGGLE) {
      return binary_state;
    }
    return !binary_state;
  }
  bool cancel_action() {
    cancel_action_();
    if (!action_sent) {
      return false;
    }
    action_sent = false;
    return true;
  }
  void action_received() { cancel_action(); }
  void send_action(bool turn_on) {
    send_action_(turn_on, [this]() { this->action_sent = true; });
  }
  send_action_t send_action_;
  cancel_action_t cancel_action_;
  send_state_t send_state_;
  STATE state = STATE::DONE;
  bool action_sent = false;
  bool binary_state = false;
};

}  // namespace action_queue
