#pragma once
#include <functional>
#include <memory>

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
  UpdateChannel(update_func_t_ &&update_func)
      : update_func(std::make_shared(std::move(update_func))) {}

  // undo && and std::forward for now
  bool operator()(Args... args) {
    bool last_update_not_handled = waiting_for_response;
    waiting_for_response = true;
    (*update_func)([this]() { this->waiting_for_response = false; }, (args)...);
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

  UpdateChannel(const UpdateChannel<Args...> &other)
      : update_func(other.update_func), waiting_for_response(false) {}

 private:
  std::shared_ptr<update_func_t_> update_func;
  bool waiting_for_response = false;
};

template <typename... Args>
UpdateChannel<Args...> make_update_channel(update_func_t<Args...> &&func) {
  return UpdateChannel<Args...>(std::forward<update_func_t<Args...>>(func));
}

template <typename... Args>
UpdateChannel<Args...> make_update_channel(
    const UpdateChannel<Args...> &other) {
  return UpdateChannel<Args...>(other);
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

// The following was taken from https://stackoverflow.com/a/6512387

template <typename F, typename Ret, typename... Args>
std::function<Ret(Args...)> returns_func(Ret (F::*)(Args...) const);

template <typename F, typename Ret, typename... Args>
std::function<Ret(Args...)> returns_func(Ret (F::*)(Args...));

template <typename L, typename F = decltype(returns_func(&L::operator()))>
F make_function_from_lambda(L &&lambda) {
  F func{lambda};
  return func;
}

}  // namespace utils
}  // namespace fk_esphome
