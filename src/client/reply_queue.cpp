#include "client/reply_queue.h"

void ReplyQueue::push(const Message& reply) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        replies_.push_back(reply);
    }
    ready_.notify_one();
}

void ReplyQueue::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    ready_.notify_all();
}

bool ReplyQueue::pop(Message& reply) {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [this] { return closed_ || !replies_.empty(); });
    if (replies_.empty()) return false;
    reply = replies_.front();
    replies_.pop_front();
    return true;
}
