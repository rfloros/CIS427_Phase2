// Carries replies from the reader thread to the main thread. Every command
// gets exactly one reply, in order, so the main thread just takes the next.
#ifndef CLIENT_REPLY_QUEUE_H
#define CLIENT_REPLY_QUEUE_H

#include <condition_variable>
#include <deque>
#include <mutex>

#include "common/message_assembler.h"

class ReplyQueue {
public:
    void push(const Message& reply);
    // Wakes every waiting pop(); they return false once the queue is empty.
    void close();
    // Blocks for the next reply. False means the queue was closed first.
    bool pop(Message& reply);

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Message> replies_;
    bool closed_ = false;
};

#endif
