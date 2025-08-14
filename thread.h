/**
 * message-thread is open source and released under the Apache License,
 * Version 2.0. You can find a copy of this license in the
 * `https://www.apache.org/licenses/LICENSE-2.0`.
 *
 * For those wishing to use message-thread under terms other than those of the
 * Apache License, a commercial license is available. For more information on
 * the commercial license terms and how to obtain a commercial license, please
 * contact me.
 */

#ifndef MESSAGE_THREAD_THREAD_H_
#define MESSAGE_THREAD_THREAD_H_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace mt {

class Message;

class Runnable {
 public:
  virtual ~Runnable() = default;

  virtual void Run() = 0;
};

template <typename F>
class RunnableHolder final : public Runnable {
 public:
  explicit RunnableHolder(F&& f) : _f(std::forward<F>(f)) {}
  ~RunnableHolder() override = default;

  void Run() override { _f(); }

 private:
  F _f;
};

class MessageHandler {
 public:
  virtual ~MessageHandler() = default;

  virtual void DispatchMessage(const std::shared_ptr<Message>& msg) = 0;
};

class Message final {
 public:
  Message() : send_time_(std::chrono::steady_clock::now()) {}
  ~Message() = default;

  template <typename F>
  void SetCallback(F&& f) {
    send_time_ = std::chrono::steady_clock::now();
    runnable_ = std::make_shared<RunnableHolder<F>>(std::forward<F>(f));
  }

  template <typename F>
  void SetCallback(F&& f, std::chrono::milliseconds delay) {
    send_time_ = std::chrono::steady_clock::now() + delay;
    runnable_ = std::make_shared<RunnableHolder<F>>(std::forward<F>(f));
  }

  [[nodiscard]] const std::chrono::steady_clock::time_point& send_time() const {
    return send_time_;
  }

  [[nodiscard]] const std::shared_ptr<Runnable>& runnable() const {
    return runnable_;
  }

  void set_target(const std::weak_ptr<MessageHandler>& target) {
    target_ = target;
  }

  [[nodiscard]] const std::weak_ptr<MessageHandler>& target() const {
    return target_;
  }

 private:
  std::chrono::steady_clock::time_point send_time_;
  std::shared_ptr<Runnable> runnable_;
  std::weak_ptr<MessageHandler> target_;
};

using MessagePtr = std::shared_ptr<Message>;

struct Compare {
  bool operator()(const MessagePtr& f1, const MessagePtr& f2) {
    return f1->send_time().time_since_epoch().count() >
           f2->send_time().time_since_epoch().count();
  }
};

class MessageQueue final {
 public:
  MessageQueue() = default;
  ~MessageQueue() = default;

  bool Enqueue(const MessagePtr& msg) {
    {
      std::lock_guard lock(mutex_);
      if (quit_) {
        return false;
      }
      queue_.push(msg);
    }
    cv_.notify_all();
    return true;
  }

  MessagePtr Next() {
    std::unique_lock lock(mutex_);
    while (true) {
      if (quit_) {
        return nullptr;
      }

      if (queue_.empty()) {
        cv_.wait(lock);
        continue;
      }

      auto next_message_time = queue_.top()->send_time();
      if (next_message_time <= std::chrono::steady_clock::now()) {
        break;
      }

      cv_.wait_until(lock, next_message_time);
    }

    if (queue_.empty()) {
      return nullptr;
    }

    auto msg = queue_.top();
    queue_.pop();
    return msg;
  }

  void Quit() {
    std::lock_guard lock(mutex_);
    quit_ = true;
    cv_.notify_all();
  }

  void QuitSafely() {
    // TODO: 待实现安全退出
    Quit();
  }

 private:
  bool quit_ = false;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::priority_queue<MessagePtr, std::vector<MessagePtr>, Compare> queue_;
};

class Looper final : public std::enable_shared_from_this<Looper> {
 public:
  Looper() = default;
  ~Looper() = default;

  static std::shared_ptr<Looper> MyLooper() {
    static thread_local std::shared_ptr<Looper> my_looper =
        std::make_shared<Looper>();
    return my_looper;
  }

  void Loop() {
    while (!quit_) {
      auto msg = queue_->Next();
      if (!msg) {
        break;
      }
      auto target = msg->target().lock();
      if (target) {
        target->DispatchMessage(msg);
      }
    }
  }

  void Quit() {
    quit_ = true;
    queue_->Quit();
  }

  void QuitSafely() {
    quit_ = true;
    queue_->QuitSafely();
  }

  std::shared_ptr<MessageQueue> queue() { return queue_; }

 private:
  std::atomic_bool quit_ = false;
  std::shared_ptr<MessageQueue> queue_ = std::make_shared<MessageQueue>();
};

class Handler final : public MessageHandler,
                      public std::enable_shared_from_this<Handler> {
 public:
  class Callback {
   public:
    virtual bool HandleMessage(const std::shared_ptr<Message>& msg) = 0;
  };

  explicit Handler(const std::shared_ptr<Looper>& looper,
                   const std::shared_ptr<Callback>& callback = nullptr)
      : looper_(looper), callback_(callback) {}
  ~Handler() override = default;

  template <typename F>
  bool Post(F f) {
    return PostDelay(std::forward<F>(f), std::chrono::milliseconds(0));
  }

  template <typename F>
  bool PostDelay(F f, std::chrono::milliseconds delay) {
    auto msg = std::make_shared<Message>();
    msg->SetCallback(std::forward<F>(f), delay);
    msg->set_target(shared_from_this());
    return looper_->queue()->Enqueue(msg);
  }

  void DispatchMessage(const std::shared_ptr<Message>& msg) final {
    if (msg->runnable()) {
      msg->runnable()->Run();
    } else {
      if (callback_) {
        if (callback_->HandleMessage(msg)) {
          return;
        }
      }
      HandleMessage(msg);
    }
  }

  virtual void HandleMessage(const std::shared_ptr<Message>& msg) {}

 private:
  std::shared_ptr<Looper> looper_;
  std::shared_ptr<Callback> callback_;
};

class HandlerThread final {
 public:
  explicit HandlerThread(std::string name) : name_(std::move(name)) {}

  virtual ~HandlerThread() {
    Quit();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void Start() { thread_ = std::thread(&HandlerThread::Run, this); }

  void Run() {
    {
      std::lock_guard lock(mutex_);
      looper_ = Looper::MyLooper();
      cv_.notify_all();
    }
    looper_->Loop();
  }

  [[nodiscard]] std::shared_ptr<Looper> looper() {
    std::unique_lock lock(mutex_);
    while (!looper_) {
      cv_.wait(lock);
    }
    return looper_;
  }

  [[nodiscard]] std::shared_ptr<Handler> handler() {
    auto looper = this->looper();
    if (!handler_) {
      handler_ = std::make_shared<Handler>(looper);
    }
    return handler_;
  }

  [[nodiscard]] const std::string& name() const { return name_; }

  bool Quit() {
    auto looper = this->looper();
    if (looper) {
      looper->Quit();
      return true;
    }
    return false;
  }

  bool QuitSafely() {
    auto looper = this->looper();
    if (looper) {
      looper->QuitSafely();
      return true;
    }
    return false;
  }

 private:
  std::string name_;
  std::thread thread_;
  std::shared_ptr<Looper> looper_;
  std::shared_ptr<Handler> handler_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

}  // namespace mt

#endif  // MESSAGE_THREAD_THREAD_H_