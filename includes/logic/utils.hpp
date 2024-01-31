#pragma once
#include <functional>

namespace fk_esphome {
namespace utils {

using action_t = std::function<void()>;
using cancel_update_t = std::function<void()>;
template <typename... Args>
using update_func_t = std::function<void(cancel_update_t &&, Args...)>;

template <typename... Args>
class UpdateChannel {
 public:
  using update_func_t_ = update_func_t<Args...>;
  UpdateChannel(update_func_t_ &&update_func) : update_func(update_func) {}

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
  update_func_t_ update_func;
  bool waiting_for_response = false;
};

template <typename... Args>
UpdateChannel<Args...> make_update_channel(update_func_t<Args...> &&func) {
  return UpdateChannel<Args...>(std::forward<update_func_t<Args...>>(func));
}

template <typename F, typename... Args>
UpdateChannel<Args...> returns_update_chan(void (F::*)(cancel_update_t &&,
                                                       Args...) const);

template <typename F, typename... Args>
UpdateChannel<Args...> returns_update_chan(void (F::*)(cancel_update_t &&,
                                                       Args...));

template <typename L>
using channel_for_lambda = decltype(returns_update_chan(&L::operator()));

// overload that works for lambdas
template <typename L>
channel_for_lambda<L> make_update_channel(L &&lambda) {
  return channel_for_lambda<L>(std::forward<L>(lambda));
}

class UpdateChannelGuard {
  template <typename... Args>
  UpdateChannelGuard(UpdateChannel<Args...> &chann)
      : has_update(chann.check_update()) {}
  operator bool() const { return has_update; }
  bool has_update = false;
};
}  // namespace utils
}  // namespace fk_esphome
