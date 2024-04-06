/**
 * message-thread is open source and released under the Apache License, Version 2.0.
 * You can find a copy of this license in the `https://www.apache.org/licenses/LICENSE-2.0`.
 *
 * For those wishing to use message-thread under terms other than those of the Apache License, a
 * commercial license is available. For more information on the commercial license terms and how to
 * obtain a commercial license, please contact me.
 */

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

class ICallback {
  public:
    virtual ~ICallback() = default;
    virtual void Execute() = 0;
};

template <typename F>
class CallbackHolder final : public ICallback {
  public:
    explicit CallbackHolder(F&& f) : _f(std::forward<F>(f)) {}
    ~CallbackHolder() override = default;

  public:
    void Execute() override { _f(); }

  private:
    F _f;
};

class Message final {
  public:
    Message() : send_time_(std::chrono::steady_clock::now()) {}
    ~Message() = default;

  public:
    template <typename F>
    void SetCallback(F&& f, std::chrono::milliseconds delay = std::chrono::milliseconds(0)) {
        callback_ = std::shared_ptr<ICallback>(new CallbackHolder<F>(std::forward<F>(f)));
        send_time_ = std::chrono::steady_clock::now() + delay;
    }

    void Execute() const {
        if (!callback_) {
            return;
        }
        callback_->Execute();
    }

    const std::chrono::steady_clock::time_point& GetSendTime() const { return send_time_; }

  private:
    std::shared_ptr<ICallback> callback_;
    std::chrono::steady_clock::time_point send_time_;
};

using MessagePtr = std::shared_ptr<Message>;

struct Compare {
    bool operator()(const MessagePtr& f1, const MessagePtr& f2) {
        return f1->GetSendTime().time_since_epoch().count() >
               f2->GetSendTime().time_since_epoch().count();
    }
};

class MessageQueue final {
  public:
    MessageQueue() = default;
    ~MessageQueue() = default;

  public:
    bool Enqueue(const MessagePtr& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (quit_) {
            return false;
        }
        queue_.push(message);
        cv_.notify_all();
        return true;
    }

    MessagePtr Next() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (queue_.empty() || queue_.top()->GetSendTime() > std::chrono::steady_clock::now()) {
            if (queue_.empty()) {
                if (quit_) return nullptr;
                cv_.wait(lock);
            } else {
                auto wait_time = queue_.top()->GetSendTime() - std::chrono::steady_clock::now();
                cv_.wait_for(lock, wait_time);
            }
        }

        auto message = queue_.top();
        queue_.pop();
        return message;
    }

    void Quit() {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
        cv_.notify_all();
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

  public:
    static std::shared_ptr<Looper> MyLooper() {
        static thread_local std::shared_ptr<Looper> my_looper = std::make_shared<Looper>();
        return my_looper;
    }

    void Loop() {
        while (true) {
            auto message = queue_->Next();
            if (quit_ || !message) {
                break;
            }
            message->Execute();
        }
    }

    void Quit() {
        quit_ = true;
        queue_->Quit();
    }

    std::shared_ptr<MessageQueue> GetMessageQueue() { return queue_; }

  private:
    std::atomic_bool quit_ = false;
    std::shared_ptr<MessageQueue> queue_ = std::make_shared<MessageQueue>();
};

class Handler final {
  public:
    Handler() = default;
    ~Handler() = default;

  public:
    explicit Handler(const std::shared_ptr<Looper>& looper) : looper_(looper) {}

    template <typename F>
    bool Post(F f, std::chrono::milliseconds delay = std::chrono::milliseconds(0)) {
        auto message = std::make_shared<Message>();
        message->SetCallback(std::forward<F>(f), delay);
        return looper_->GetMessageQueue()->Enqueue(message);
    }

  private:
    std::shared_ptr<Looper> looper_;
};

class MessageThread final {
  public:
    MessageThread() : looper_(Looper::MyLooper()), thread_(&MessageThread::Run, this) {}

    ~MessageThread() {
        looper_->Quit();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

  public:
    void Run() { looper_->Loop(); }

    void Braking() {
        looper_->GetMessageQueue()->Quit();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::shared_ptr<Looper> GetLooper() const { return looper_; }

  private:
    std::shared_ptr<Looper> looper_;
    std::thread thread_;
};