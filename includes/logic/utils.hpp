#pragma once
#include <functional>

namespace fk_esphome {
namespace utils {

template <typename... Args>
class UpdateChannel {
 public:
  using action_t = std::function<void()>;
  using cancel_update_t = std::function<void()>;
  using update_func_t = std::function<void(cancel_update_t &&, Args...)>;

  UpdateChannel(update_func_t &&update_func) : update_func(update_func) {}

  bool operator()(Args &&...args) {
    bool last_update_not_handled = waiting_for_response;
    waiting_for_response = true;
    update_func([this]() { this->waiting_for_response = false; },
                std::forward<Args>(args)...);
    return last_update_not_handled;
  }

  bool check_update() {
    if (!waiting_for_response) {
      return false;
    }
    waiting_for_response = false;
    return true;
  }

  bool do_if(action_t &&action) {
    if (!check_update()) {
      return false;
    }
    action();
    return true;
  }

 private:
  update_func_t update_func;
  bool waiting_for_response = false;
};

class UpdateChannelGuard {
  template <typename... Args>
  UpdateChannelGuard(UpdateChannel<Args...> &chann)
      : has_update(chann.check_update()) {}
  operator bool() const { return has_update; }
  bool has_update = false;
};
}  // namespace utils
}  // namespace fk_esphome
