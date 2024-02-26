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
class CallbackHolder : public ICallback {
  public:
    explicit CallbackHolder(F&& f) : _f(std::forward<F>(f)) {}
    void Execute() override { _f(); }

  private:
    F _f;
};

class Message {
  public:
    Message() : send_time_(std::chrono::steady_clock::now()) {}

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

class MessageQueue {
  public:
    void Enqueue(const std::shared_ptr<Message>& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(message);
        cv_.notify_all();
    }

    std::shared_ptr<Message> Next() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!quit_ &&
               (queue_.empty() || queue_.top()->GetSendTime() > std::chrono::steady_clock::now())) {
            if (queue_.empty()) {
                cv_.wait(lock);
            } else {
                auto wait_time = queue_.top()->GetSendTime() - std::chrono::steady_clock::now();
                cv_.wait_for(lock, wait_time);
            }
        }
        if (quit_) {
            return nullptr;
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
    std::priority_queue<std::shared_ptr<Message>> queue_;
};

class Looper : public std::enable_shared_from_this<Looper> {
  public:
    static std::shared_ptr<Looper> MyLooper() {
        static thread_local std::shared_ptr<Looper> my_looper = std::make_shared<Looper>();
        return my_looper;
    }

    void Loop() {
        while (true) {
            auto message = queue_->Next();
            if (!message) {
                break;
            }
            message->Execute();
        }
    }

    void Quit() { queue_->Quit(); }

    std::shared_ptr<MessageQueue> GetMessageQueue() { return queue_; }

  private:
    std::shared_ptr<MessageQueue> queue_ = std::make_shared<MessageQueue>();
};

class Handler {
  public:
    explicit Handler(const std::shared_ptr<Looper>& looper) : looper_(looper) {}

    template <typename F>
    void Post(F f, std::chrono::milliseconds delay = std::chrono::milliseconds(0)) {
        auto message = std::make_shared<Message>();
        message->SetCallback(std::forward<F>(f), delay);
        looper_->GetMessageQueue()->Enqueue(message);
    }

  private:
    std::shared_ptr<Looper> looper_;
};

class MessageThread {
  public:
    MessageThread() : looper_(Looper::MyLooper()), thread_(&MessageThread::Run, this) {}

    ~MessageThread() {
        looper_->Quit();
        thread_.join();
    }

    void Run() { looper_->Loop(); }

    std::shared_ptr<Looper> GetLooper() const { return looper_; }

  private:
    std::shared_ptr<Looper> looper_;
    std::thread thread_;
};