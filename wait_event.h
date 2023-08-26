#pragma once

#include <cassert>
#include <iostream>
#include <optional>
#include <thread>
#include <type_traits>
#include <webview2.h>
#include <wil/com.h>
#include <wrl.h>

// Class to check if methods are called on the same thread where the object is
// created.
class SequenceCheckerImpl {
public:
  SequenceCheckerImpl() : id_(std::this_thread::get_id()) {}

  bool CalledOnValidSequence() const {
    return id_ == std::this_thread::get_id();
  }

private:
  const std::thread::id id_;
};
// Define macros to create and verify sequence checkers.
#define DEFINE_SEQUENCE(name) SequenceCheckerImpl name
#define VERIFY_SEQUENCE_CALL(checker) assert((checker).CalledOnValidSequence())

// Class to pump events and manage a condition variable for stopping the pump.
class EventPumper {
public:
  EventPumper() : stop_pump_(false), mutex_(), cond_() {}
  void PumpMessagesWithTimeout(int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                   [this]() { return stop_pump_; });
  }
  void StopPump() {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_pump_ = true;
    cond_.notify_one();
  }

private:
  bool stop_pump_;
  std::mutex mutex_;
  std::condition_variable cond_;
};
constexpr double kFutureTimeout = 30;
EventPumper loop_pumper;

#define DEFINE_WEBVIEW_EVENT_BASE(name, sender_type, add_method,               \
                                  remove_method, handler_type, args_type)      \
  struct name {                                                                \
    static constexpr const char *EventName = #name;                            \
    using Sender = sender_type;                                                \
    static constexpr auto AddMethod = &Sender::add_method;                     \
    static constexpr auto RemoveMethod = &Sender::remove_method;               \
    using Handler = handler_type;                                              \
    using Args = args_type;                                                    \
  };

#define DEFINE_WEBVIEW_EVENT(name, sender)                                     \
  DEFINE_WEBVIEW_EVENT_BASE(name, sender, add_##name, remove_##name,           \
                            ICoreWebView2##name##EventHandler,                 \
                            ICoreWebView2##name##EventArgs)

#define DEFINE_WEBVIEW_EVENT_WITH_ARGS(name, sender, arg_interface)            \
  DEFINE_WEBVIEW_EVENT_BASE(name, sender, add_##name, remove_##name,           \
                            ICoreWebView2##name##EventHandler, arg_interface)

template <class Event>
using SenderPtr = Microsoft::WRL::ComPtr<typename Event::Sender>;
template <class Event>
using ArgsPtr = Microsoft::WRL::ComPtr<typename Event::Args>;

/// EventResultHolder is a generic class that can hold event results and also
/// support asynchronous waiting.
template <class Event, class Result = ArgsPtr<Event>> class EventResultHolder {
public:
  // This constructor will only be enabled if Event is not of type void.
  // sender: Object that sends the event.
  // callback: Function to call when the event is received.
  template <typename = std::enable_if_t<!std::is_same<Event, void>::value>>
  EventResultHolder(
      const SenderPtr<Event> &sender,
      const std::function<void(typename Event::Args *)> &callback = nullptr)
      : sender_(sender) {
    // Register the event handler using AddMethod of Event and obtain a token.
    HRESULT hr = (sender_.Get()->*Event::AddMethod)(
        Microsoft::WRL::Callback<typename Event::Handler>(
            [this, callback](auto got_sender,
                             typename Event::Args *got_args) -> HRESULT {
              // Type safety checks and sender validation.
              if constexpr (!std::is_same<Event, int>::value) {
                SenderPtr<Event> got_sender_type;
                assert(
                    got_sender->QueryInterface(IID_PPV_ARGS(&got_sender_type)));
                assert(sender_ == got_sender_type);
              }
              // Store the event result and call the optional callback.
              Set(got_args);
              if (callback) {
                callback(got_args);
              }
              assert((sender_.Get()->*Event::RemoveMethod)(token_));
              return 1;
            })
            .Get(),
        &token_);
    assert((hr));
  }
  ~EventResultHolder() {
    // If Event is not void, remove the event handler.
    if constexpr (!std::is_same<Event, void>::value) {
      (sender_.Get()->*Event::RemoveMethod)(token_);
    }
    // Assert that result_ has been set before destruction.
    assert(result_) << "FutureEvent was destructed without its result ever "
                       "having been set.";
  }

  // Retrieves the stored result. Throws if not set.
  Result &Get() {
    Result *result = TryGet();
    if (!result) {
      std::cerr << "Timed out or interrupted waiting for a result of type "
                << typeid(Result).name();
    }
    return *result;
  }

  // Attempts to retrieve the stored result, returns nullptr if not set.
  Result *TryGet() { return Wait() ? std::addressof(*result_) : nullptr; }

  // Waits for the result to be set. Returns true if set, false otherwise.
  bool Wait() {
    VERIFY_SEQUENCE_CALL(sequence_checker_);
    assert(!waiting_)
        << "FutureEvent::Wait cannot be called when it is already waiting.";
    if (waiting_)
      return false;
    if (result_)
      return true;
    waiting_ = true;
    loop_pumper.PumpMessagesWithTimeout(kFutureTimeout);
    waiting_ = false;
    return !!result_;
  }

  // Checks if the result is ready.
  bool IsReady() const { return !!result_; }

  // Sets the result. Asserts if it has already been set.
  template <class... Args> void Set(Args &&...args) {
    VERIFY_SEQUENCE_CALL(sequence_checker_);
    EXPECT_TRUE(!result_) << "FutureEvent::Set can only be called once.";
    if (result_)
      return;
    result_.emplace(std::forward<Args>(args)...);
    if (waiting_) {
      loop_pumper.StopPump();
    }
  }

private:
  // Sequence checker.
  DEFINE_SEQUENCE(sequence_checker_);

  // Optional to hold the event result.
  std::optional<Result> result_;

  // Flag to indicate whether it's waiting for the result.
  bool waiting_ = false;

  // Pointer to the event sender object.
  SenderPtr<Event> sender_ = nullptr;

  // Token for registering and unregistering the event handler.
  EventRegistrationToken token_ = {};
};

// Function to wait for a WebView event and return its result.
template <class Event>
ArgsPtr<Event> WaitEvent(
    const SenderPtr<Event> &sender,
    const std::function<void(typename Event::Args *)> &callback = nullptr) {
  return EventResultHolder<Event>(sender, callback).Get();
}